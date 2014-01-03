/*

   BLIS    
   An object-based framework for developing high-performance BLAS-like
   libraries.

   Copyright (C) 2014, The University of Texas

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:
    - Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    - Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    - Neither the name of The University of Texas nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include "blis.h"



void bli_strsm_u_opt_mxn(
                          float*    restrict a,
                          float*    restrict b,
                          float*    restrict c, inc_t rs_c, inc_t cs_c
                        )
{
	/* Just call the reference implementation. */
	bli_strsm_u_ref_mxn( a,
	                     b,
	                     c, rs_c, cs_c );
}



void bli_dtrsm_u_opt_mxn(
                          double*   restrict a,
                          double*   restrict b,
                          double*   restrict c, inc_t rs_c, inc_t cs_c
                        )
{
/*
  Template trsm_u micro-kernel implementation

  This function contains a template implementation for a double-precision
  real trsm micro-kernel, coded in C, which can serve as the starting point
  for one to write an optimized micro-kernel on an arbitrary architecture.
  (We show a template implementation for only double-precision real because
  the templates for the other three floating-point types would be nearly
  identical.)

  This micro-kernel performs the following operation:

    C11 := inv(A11) * B11

  where A11 is MR x MR and upper triangular, B11 is MR x NR, and C11 is
  MR x NR.

  Parameters:

  - a11:    The address of A11, which is the MR x MR upper triangular
            submatrix within the packed micro-panel of matrix A. A11 is
            stored by columns with leading dimension PACKMR, where
            typically PACKMR = MR. Note that A11 contains elements in both
            triangles, though elements in the unstored triangle are not
            guaranteed to be zero and thus should not be referenced.
  - b11:    The address of B11, which is an MR x NR submatrix of the
            packed micro-panel of B. B11 is stored by rows with leading
            dimension PACKNR, where typically PACKNR = NR.
  - c11:    The address of C11, which is an MR x NR submatrix of matrix C,
            stored according to rs_c and cs_c. C11 is the submatrix within
            C that corresponds to the elements which were packed into B11.
            Thus, C is the original input matrix B to the overall trsm
            operation.
  - rs_c:   The row stride of C11 (ie: the distance to the next row of C11,
            in units of matrix elements).
  - cs_c:   The column stride of C11 (ie: the distance to the next column of
            C11, in units of matrix elements).

  Diagrams for trsm

  Please see the diagram for gemmtrsm_u to see depiction of the trsm_u and
  where it fits in with its preceding gemm subproblem.

  Implementation Notes for trsm

  - Register blocksizes. See Implementation Notes for gemm.
  - Leading dimensions of a11 and b11: PACKMR and PACKNR. See
    Implementation Notes for gemm.
  - Edge cases in MR, NR dimensions. See Implementation Notes for gemm.
  - Alignment of a11 and b11. See Implementation Notes for gemmtrsm.
  - Unrolling loops. Most optimized implementations should unroll all
    three loops within the trsm micro-kernel.
  - Diagonal elements of A11. At the time this micro-kernel is called,
    the diagonal entries of triangular matrix A11 contain the inverse of
    the original elements. This inversion is done during packing so that
    we can avoid expensive division instructions within the micro-kernel
    itself. If the diag parameter to the higher level trsm operation was
    equal to BLIS_UNIT_DIAG, the diagonal elements will be explicitly
    unit.
  - Zero elements of A11. Since A11 is lower triangular (for trsm_l), the
    strictly upper triangle implicitly contains zeros. Similarly, the
    strictly lower triangle of A11 implicitly contains zeros when A11 is
    upper triangular (for trsm_u). However, the packing function may or
    may not actually write zeros to this region. Thus, the implementation
    should not reference these elements.
  - Output. This micro-kernel must write its result to two places: the
    submatrix B11 of the current packed micro-panel of B and the submatrix
    C11 of the output matrix C.

  For more info, please refer to the BLIS website and/or contact the
  blis-devel mailing list.

  -FGVZ
*/
	const dim_t        m     = bli_dmr;
	const dim_t        n     = bli_dnr;

	const inc_t        rs_a  = 1;
	const inc_t        cs_a  = bli_dpackmr;

	const inc_t        rs_b  = bli_dpacknr;
	const inc_t        cs_b  = 1;

	dim_t              iter, i, j, l;
	dim_t              n_behind;

	double*   restrict alpha11;
	double*   restrict a12t;
	double*   restrict alpha12;
	double*   restrict X2;
	double*   restrict x1;
	double*   restrict x21;
	double*   restrict chi21;
	double*   restrict chi11;
	double*   restrict gamma11;
	double             rho11;

	for ( iter = 0; iter < m; ++iter )
	{
		i        = m - iter - 1;
		n_behind = iter;
		alpha11  = a + (i  )*rs_a + (i  )*cs_a;
		a12t     = a + (i  )*rs_a + (i+1)*cs_a;
		x1       = b + (i  )*rs_b + (0  )*cs_b;
		X2       = b + (i+1)*rs_b + (0  )*cs_b;

		/* x1 = x1 - a12t * X2; */
		/* x1 = x1 / alpha11; */
		for ( j = 0; j < n; ++j )
		{
			chi11   = x1 + (0  )*rs_b + (j  )*cs_b;
			x21     = X2 + (0  )*rs_b + (j  )*cs_b;
			gamma11 = c  + (i  )*rs_c + (j  )*cs_c;

			/* chi11 = chi11 - a12t * x21; */
			bli_dset0s( rho11 );
			for ( l = 0; l < n_behind; ++l )
			{
				alpha12 = a12t + (l  )*cs_a;
				chi21   = x21  + (l  )*rs_b;

				bli_daxpys( *alpha12, *chi21, rho11 );
			}
			bli_dsubs( rho11, *chi11 );

			/* chi11 = chi11 / alpha11; */
			/* NOTE: The INVERSE of alpha11 (1.0/alpha11) is stored instead
			   of alpha11, so we can multiply rather than divide. We store
			   the inverse of alpha11 intentionally to avoid expensive
			   division instructions within the micro-kernel. */
			bli_dscals( *alpha11, *chi11 );

			/* Output final result to matrix C. */
			bli_dcopys( *chi11, *gamma11 );
		}
	}
}



void bli_ctrsm_u_opt_mxn(
                          scomplex* restrict a,
                          scomplex* restrict b,
                          scomplex* restrict c, inc_t rs_c, inc_t cs_c
                        )
{
	/* Just call the reference implementation. */
	bli_ctrsm_u_ref_mxn( a,
	                     b,
	                     c, rs_c, cs_c );
}



void bli_ztrsm_u_opt_mxn(
                          dcomplex* restrict a,
                          dcomplex* restrict b,
                          dcomplex* restrict c, inc_t rs_c, inc_t cs_c
                        )
{
	/* Just call the reference implementation. */
	bli_ztrsm_u_ref_mxn( a,
	                     b,
	                     c, rs_c, cs_c );
}

