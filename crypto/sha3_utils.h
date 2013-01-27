#ifndef _SHA3_UTILS_H_
#define	_SHA3_UTILS_H_

int Keccak256(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
int Keccak256_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

int Keccak512(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);
int Keccak512_par(uchar_t *cksum_buf, uchar_t *buf, uint64_t bytes);

#endif

