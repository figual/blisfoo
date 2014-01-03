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
#include "test_libblis.h"


// Static variables.
static char*     op_str                    = "syrk";
static char*     o_types                   = "mm";  // a c
static char*     p_types                   = "uh";  // uploc transa
static thresh_t  thresh[BLIS_NUM_FP_TYPES] = { { 1e-04, 1e-05 },   // warn, pass for s
                                               { 1e-04, 1e-05 },   // warn, pass for c
                                               { 1e-13, 1e-14 },   // warn, pass for d
                                               { 1e-13, 1e-14 } }; // warn, pass for z

// Local prototypes.
void libblis_test_syrk_deps( test_params_t* params,
                             test_op_t*     op );

void libblis_test_syrk_experiment( test_params_t* params,
                                   test_op_t*     op,
                                   mt_impl_t      impl,
                                   num_t          datatype,
                                   char*          pc_str,
                                   char*          sc_str,
                                   unsigned int   p_cur,
                                   double*        perf,
                                   double*        resid );

void libblis_test_syrk_impl( mt_impl_t impl,
                             obj_t*    alpha,
                             obj_t*    a,
                             obj_t*    beta,
                             obj_t*    c );

void libblis_test_syrk_check( obj_t*  alpha,
                              obj_t*  a,
                              obj_t*  beta,
                              obj_t*  c,
                              obj_t*  c_orig,
                              double* resid );



void libblis_test_syrk_deps( test_params_t* params, test_op_t* op )
{
	libblis_test_randv( params, &(op->ops->randv) );
	libblis_test_randm( params, &(op->ops->randm) );
	libblis_test_setv( params, &(op->ops->setv) );
	libblis_test_fnormv( params, &(op->ops->fnormv) );
	libblis_test_subv( params, &(op->ops->subv) );
	libblis_test_scalv( params, &(op->ops->scalv) );
	libblis_test_copym( params, &(op->ops->copym) );
	libblis_test_scalm( params, &(op->ops->scalm) );
	libblis_test_gemv( params, &(op->ops->gemv) );
	libblis_test_symv( params, &(op->ops->symv) );
}



void libblis_test_syrk( test_params_t* params, test_op_t* op )
{

	// Return early if this test has already been done.
	if ( op->test_done == TRUE ) return;

	// Return early if operation is disabled.
	if ( op->op_switch == DISABLE_ALL ||
	     op->ops->l3_over == DISABLE_ALL ) return;

	// Call dependencies first.
	if ( TRUE ) libblis_test_syrk_deps( params, op );

	// Execute the test driver for each implementation requested.
	if ( op->front_seq == ENABLE )
	{
		libblis_test_op_driver( params,
		                        op,
		                        BLIS_TEST_SEQ_FRONT_END,
		                        op_str,
		                        p_types,
		                        o_types,
		                        thresh,
		                        libblis_test_syrk_experiment );
	}
}



void libblis_test_syrk_experiment( test_params_t* params,
                                   test_op_t*     op,
                                   mt_impl_t      impl,
                                   num_t          datatype,
                                   char*          pc_str,
                                   char*          sc_str,
                                   unsigned int   p_cur,
                                   double*        perf,
                                   double*        resid )
{
	unsigned int n_repeats = params->n_repeats;
	unsigned int i;

	double       time_min  = 1e9;
	double       time;

	dim_t        m, k;

	uplo_t       uploc;
	trans_t      transa;

	obj_t        kappa;
	obj_t        alpha, a, beta, c;
	obj_t        c_save;


	// Map the dimension specifier to actual dimensions.
	m = libblis_test_get_dim_from_prob_size( op->dim_spec[0], p_cur );
	k = libblis_test_get_dim_from_prob_size( op->dim_spec[1], p_cur );

	// Map parameter characters to BLIS constants.
	bli_param_map_char_to_blis_uplo( pc_str[0], &uploc );
	bli_param_map_char_to_blis_trans( pc_str[1], &transa );

	// Create test scalars.
	bli_obj_scalar_init_detached( datatype, &kappa );
	bli_obj_scalar_init_detached( datatype, &alpha );
	bli_obj_scalar_init_detached( datatype, &beta );

	// Create test operands (vectors and/or matrices).
	libblis_test_mobj_create( params, datatype, transa,
	                          sc_str[0], m, k, &a );
	libblis_test_mobj_create( params, datatype, BLIS_NO_TRANSPOSE,
	                          sc_str[1], m, m, &c );
	libblis_test_mobj_create( params, datatype, BLIS_NO_TRANSPOSE,
	                          sc_str[1], m, m, &c_save );

	// Set alpha and beta.
	if ( bli_obj_is_real( c ) )
	{
		bli_setsc(  1.2,  0.0, &alpha );
		bli_setsc( -1.0,  0.0, &beta );
	}
	else
	{
		// For syrk, both alpha and beta may be complex since, unlike herk,
		// C is symmetric in both the real and complex cases.
		bli_setsc(  1.2,  0.5, &alpha );
		bli_setsc( -1.0,  0.5, &beta );
	}

	// Randomize A.
	bli_randm( &a );

	// Set the structure and uplo properties of C.
	bli_obj_set_struc( BLIS_SYMMETRIC, c );
	bli_obj_set_uplo( uploc, c );

	// Randomize A, make it densely symmetric, and zero the unstored triangle
	// to ensure the implementation is reads only from the stored region.
	bli_randm( &c );
	bli_mksymm( &c );
	bli_mktrim( &c );

	// Save C and set its structure and uplo properties.
	bli_obj_set_struc( BLIS_SYMMETRIC, c_save );
	bli_obj_set_uplo( uploc, c_save );
	bli_copym( &c, &c_save );

	// Normalize by k.
	bli_setsc( 1.0/( double )k, 0.0, &kappa );
	bli_scalm( &kappa, &a );

	// Apply the remaining parameters.
	bli_obj_set_conjtrans( transa, a );

	// Repeat the experiment n_repeats times and record results. 
	for ( i = 0; i < n_repeats; ++i )
	{
		bli_copym( &c_save, &c );

		time = bli_clock();

		libblis_test_syrk_impl( impl, &alpha, &a, &beta, &c );

		time_min = bli_clock_min_diff( time_min, time );
	}

	// Estimate the performance of the best experiment repeat.
	*perf = ( 1.0 * m * m * k ) / time_min / FLOPS_PER_UNIT_PERF;
	if ( bli_obj_is_complex( c ) ) *perf *= 4.0;

	// Perform checks.
	libblis_test_syrk_check( &alpha, &a, &beta, &c, &c_save, resid );

	// Zero out performance and residual if output matrix is empty.
	libblis_test_check_empty_problem( &c, perf, resid );

	// Free the test objects.
	bli_obj_free( &a );
	bli_obj_free( &c );
	bli_obj_free( &c_save );
}



void libblis_test_syrk_impl( mt_impl_t impl,
                             obj_t*    alpha,
                             obj_t*    a,
                             obj_t*    beta,
                             obj_t*    c )
{
	switch ( impl )
	{
		case BLIS_TEST_SEQ_FRONT_END:
		bli_syrk( alpha, a, beta, c );
		break;

		default:
		libblis_test_printf_error( "Invalid implementation type.\n" );
	}
}



void libblis_test_syrk_check( obj_t*  alpha,
                              obj_t*  a,
                              obj_t*  beta,
                              obj_t*  c,
                              obj_t*  c_orig,
                              double* resid )
{
	num_t  dt      = bli_obj_datatype( *c );
	num_t  dt_real = bli_obj_datatype_proj_to_real( *c );

	dim_t  m       = bli_obj_length( *c );
	dim_t  k       = bli_obj_width_after_trans( *a );

	obj_t  at;
	obj_t  kappa, norm;
	obj_t  t, v, w, z;

	double junk;

	//
	// Pre-conditions:
	// - a is randomized.
	// - c_orig is randomized and symmetric.
	// Note:
	// - alpha and beta should have non-zero imaginary components in the
	//   complex cases in order to more fully exercise the implementation.
	//
	// Under these conditions, we assume that the implementation for
	//
	//   C := beta * C_orig + alpha * transa(A) * transa(A)^T
	//
	// is functioning correctly if
	//
	//   fnorm( v - z )
	//
	// is negligible, where
	//
	//   v = C * t
	//   z = ( beta * C_orig + alpha * transa(A) * transa(A)^T ) * t
	//     = beta * C_orig * t + alpha * transa(A) * transa(A)^T * t
	//     = beta * C_orig * t + alpha * transa(A) * w
	//     = beta * C_orig * t + z
	//

	bli_obj_alias_with_trans( BLIS_TRANSPOSE, *a, at );

	bli_obj_scalar_init_detached( dt,      &kappa );
	bli_obj_scalar_init_detached( dt_real, &norm );

	bli_obj_create( dt, m, 1, 0, 0, &t );
	bli_obj_create( dt, m, 1, 0, 0, &v );
	bli_obj_create( dt, k, 1, 0, 0, &w );
	bli_obj_create( dt, m, 1, 0, 0, &z );

	bli_randv( &t );
	bli_setsc( 1.0/( double )m, 0.0, &kappa );
	bli_scalv( &kappa, &t );

	bli_symv( &BLIS_ONE, c, &t, &BLIS_ZERO, &v );

	bli_gemv( &BLIS_ONE, &at, &t, &BLIS_ZERO, &w );
	bli_gemv( alpha, a, &w, &BLIS_ZERO, &z );
	bli_symv( beta, c_orig, &t, &BLIS_ONE, &z );

	bli_subv( &z, &v );
	bli_fnormv( &v, &norm );
	bli_getsc( &norm, resid, &junk );

	bli_obj_free( &t );
	bli_obj_free( &v );
	bli_obj_free( &w );
	bli_obj_free( &z );
}

