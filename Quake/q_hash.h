// q_hash.h -- shared hash function interface

#ifndef Q_HASH_H
#define Q_HASH_H

#include <stddef.h>

#define DIGEST_MAXSIZE	(512/8)

typedef struct
{
	unsigned int digestsize;
	unsigned int contextsize;
	void (*init) (void *context);
	void (*process) (void *context, const void *data, size_t datasize);
	void (*terminate) (unsigned char *digest, void *context);
} hashfunc_t;

extern hashfunc_t hash_md5;
extern hashfunc_t hash_sha1;
extern hashfunc_t hash_sha2_224;
extern hashfunc_t hash_sha2_256;
extern hashfunc_t hash_sha2_384;
extern hashfunc_t hash_sha2_512;

#define hash_certfp hash_sha2_256

size_t CalcHMAC(const hashfunc_t *hashfunc, unsigned char *digest, size_t maxdigestsize,
				 const unsigned char *data, size_t datalen,
				 const unsigned char *key, size_t keylen);
size_t CalcHash(const hashfunc_t *func, unsigned char *digest, size_t maxdigestsize,
				 const unsigned char *string, size_t stringlen);

#endif /* Q_HASH_H */
