#ifndef _SHA2_UTILS_H_
#define	_SHA2_UTILS_H_

void ossl_SHA256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void ossl_SHA256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512t256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512t256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

void ossl_SHA512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void ossl_SHA512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

#endif

