#ifndef _SHA2_UTILS_OLD_H_
#define	_SHA2_UTILS_OLD_H_

void ossl_SHA256_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512t256_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

void ossl_SHA512_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
void opt_SHA512_par_old(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

#endif

