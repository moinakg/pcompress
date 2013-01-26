#ifndef __BLAKE2_DIGEST_H__
#define __BLAKE2_DIGEST_H__

#include "blake2.h"
#include <cpuid.h>

#if defined(__cplusplus)
extern "C" {
#endif

  typedef int (*blake2b_init_funcptr)( blake2b_state *S, const uint8_t outlen );
  typedef int (*blake2b_init_key_funcptr)( blake2b_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  typedef int (*blake2b_init_param_funcptr)( blake2b_state *S, const blake2b_param *P );
  typedef int (*blake2b_update_funcptr)( blake2b_state *S, const uint8_t *in, uint64_t inlen );
  typedef int (*blake2b_final_funcptr)( blake2b_state *S, uint8_t *out, uint8_t outlen );
  typedef int (*blake2bp_init_funcptr)( blake2bp_state *S, const uint8_t outlen );
  typedef int (*blake2bp_init_key_funcptr)( blake2bp_state *S, const uint8_t outlen, const void *key, const uint8_t keylen );
  typedef int (*blake2bp_update_funcptr)( blake2bp_state *S, const uint8_t *in, uint64_t inlen );
  typedef int (*blake2bp_final_funcptr)( blake2bp_state *S, uint8_t *out, uint8_t outlen );

  typedef int (*blake2b_funcptr)( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );
  typedef int (*blake2bp_funcptr)( uint8_t *out, const void *in, const void *key, const uint8_t outlen, const uint64_t inlen, uint8_t keylen );

  /*
   * BLAKE2 function pointers. These are set to the optimized routines
   * based on CPU capabilities.
   */
  struct blake2_dispatch {
	blake2b_init_funcptr		blake2b_init;
	blake2b_init_key_funcptr		blake2b_init_key;
	blake2b_init_param_funcptr	blake2b_init_param;
	blake2b_update_funcptr		blake2b_update;
	blake2b_final_funcptr		blake2b_final;
	blake2bp_init_funcptr		blake2bp_init;
	blake2bp_init_key_funcptr	blake2bp_init_key;
	blake2bp_update_funcptr		blake2bp_update;
	blake2bp_final_funcptr		blake2bp_final;
	blake2b_funcptr			blake2b;
	blake2bp_funcptr			blake2bp;
  };

  static void blake2_module_init(struct blake2_dispatch *dsp, processor_info_t *pc)
  {
    dsp->blake2b_init		= blake2b_init_sse2;
    dsp->blake2b_init_key	= blake2b_init_key_sse2;
    dsp->blake2b_init_param	= blake2b_init_param_sse2;
    dsp->blake2b_update		= blake2b_update_sse2;
    dsp->blake2b_final		= blake2b_final_sse2;
    dsp->blake2bp_init		= blake2bp_init_sse2;
    dsp->blake2bp_init_key	= blake2bp_init_key_sse2;
    dsp->blake2bp_update		= blake2bp_update_sse2;
    dsp->blake2bp_final 		= blake2bp_final_sse2;
    dsp->blake2b			= blake2b_sse2;
    dsp->blake2bp		= blake2bp_sse2;

    if (pc->sse_level == 3 && pc->sse_sub_level == 1) {
      dsp->blake2b_init		= blake2b_init_ssse3;
      dsp->blake2b_init_key	= blake2b_init_key_ssse3;
      dsp->blake2b_init_param	= blake2b_init_param_ssse3;
      dsp->blake2b_update	= blake2b_update_ssse3;
      dsp->blake2b_final		= blake2b_final_ssse3;
      dsp->blake2bp_init		= blake2bp_init_ssse3;
      dsp->blake2bp_init_key	= blake2bp_init_key_ssse3;
      dsp->blake2bp_update	= blake2bp_update_ssse3;
      dsp->blake2bp_final 	= blake2bp_final_ssse3;
      dsp->blake2b		= blake2b_ssse3;
      dsp->blake2bp		= blake2bp_ssse3;

    } else if (pc->sse_level == 4 && pc->sse_sub_level >= 1) {
      dsp->blake2b_init		= blake2b_init_sse41;
      dsp->blake2b_init_key	= blake2b_init_key_sse41;
      dsp->blake2b_init_param	= blake2b_init_param_sse41;
      dsp->blake2b_update	= blake2b_update_sse41;
      dsp->blake2b_final		= blake2b_final_sse41;
      dsp->blake2bp_init		= blake2bp_init_sse41;
      dsp->blake2bp_init_key	= blake2bp_init_key_sse41;
      dsp->blake2bp_update	= blake2bp_update_sse41;
      dsp->blake2bp_final 	= blake2bp_final_sse41;
      dsp->blake2b		= blake2b_sse41;
      dsp->blake2bp		= blake2bp_sse41;
    }
    if (pc->avx_level >= 1) {
      dsp->blake2b_init		= blake2b_init_avx;
      dsp->blake2b_init_key	= blake2b_init_key_avx;
      dsp->blake2b_init_param	= blake2b_init_param_avx;
      dsp->blake2b_update	= blake2b_update_avx;
      dsp->blake2b_final		= blake2b_final_avx;
      dsp->blake2bp_init		= blake2bp_init_avx;
      dsp->blake2bp_init_key	= blake2bp_init_key_avx;
      dsp->blake2bp_update	= blake2bp_update_avx;
      dsp->blake2bp_final 	= blake2bp_final_avx;
      dsp->blake2b		= blake2b_avx;
      dsp->blake2bp		= blake2bp_avx;
    }
  }

#if defined(__cplusplus)
}
#endif

#endif
