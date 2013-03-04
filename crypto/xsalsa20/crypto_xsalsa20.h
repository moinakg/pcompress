#ifndef crypto_xsalsa20_H
#define crypto_xsalsa20_H

#include <inttypes.h>
#include <utils.h>

#define XSALSA20_CRYPTO_KEYBYTES 32
#define XSALSA20_CRYPTO_NONCEBYTES 24

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	unsigned char nonce[XSALSA20_CRYPTO_NONCEBYTES];
	uchar_t key[XSALSA20_CRYPTO_KEYBYTES];
	int keylen;
	uchar_t pkey[XSALSA20_CRYPTO_KEYBYTES];
} salsa20_ctx_t;

int salsa20_init(salsa20_ctx_t *ctx, uchar_t *salt, int saltlen, uchar_t *pwd, int pwd_len, uchar_t *nonce, int enc);
int salsa20_encrypt(salsa20_ctx_t *ctx, uchar_t *plaintext, uchar_t *ciphertext, uint64_t len, uint64_t id);
int salsa20_decrypt(salsa20_ctx_t *ctx, uchar_t *ciphertext, uchar_t *plaintext, uint64_t len, uint64_t id);
uchar_t *salsa20_nonce(salsa20_ctx_t *ctx);
void salsa20_clean_pkey(salsa20_ctx_t *ctx);
void salsa20_cleanup(salsa20_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
