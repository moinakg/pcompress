#ifndef crypto_core_hsalsa20_H
#define crypto_core_hsalsa20_H

#define HSALSA_CRYPTO_OUTPUTBYTES 32
#define HSALSA_CRYPTO_INPUTBYTES 16
#define HSALSA_CRYPTO_CONSTBYTES 16

#ifdef __cplusplus
extern "C" {
#endif
int crypto_core_hsalsa20(unsigned char *out, const unsigned char *in, const unsigned char *k, const unsigned char *c);
#ifdef __cplusplus
}
#endif

#endif
