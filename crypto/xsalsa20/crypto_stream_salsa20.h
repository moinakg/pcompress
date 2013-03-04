#ifndef crypto_stream_salsa20_H
#define crypto_stream_salsa20_H

#define crypto_stream_salsa20_amd64_xmm6_KEYBYTES 32
#define crypto_stream_salsa20_amd64_xmm6_NONCEBYTES 8
#ifdef __cplusplus
extern "C" {
#endif
extern int crypto_stream_salsa20_amd64_xmm6(unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
extern int crypto_stream_salsa20_amd64_xmm6_xor(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
extern int crypto_stream_salsa20_ref(unsigned char *c,unsigned long long clen, const unsigned char *n, const unsigned char *k);
extern int crypto_stream_salsa20_ref_xor(unsigned char *,const unsigned char *,unsigned long long,const unsigned char *,const unsigned char *);
#ifdef __cplusplus
}
#endif

#ifndef SALSA20_DEBUG
#define crypto_stream_salsa20 crypto_stream_salsa20_amd64_xmm6
#define crypto_stream_salsa20_xor crypto_stream_salsa20_amd64_xmm6_xor
#else
#define crypto_stream_salsa20 crypto_stream_salsa20_ref
#define crypto_stream_salsa20_xor crypto_stream_salsa20_ref_xor
#endif
#define crypto_stream_salsa20_KEYBYTES crypto_stream_salsa20_amd64_xmm6_KEYBYTES
#define crypto_stream_salsa20_NONCEBYTES crypto_stream_salsa20_amd64_xmm6_NONCEBYTES

#endif
