#ifndef JFIN_SHA256_H
#define JFIN_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t state[8]; uint64_t bytes; unsigned char block[64]; size_t used; } jfin_sha256_t;
void jfin_sha256_init(jfin_sha256_t *s);
void jfin_sha256_update(jfin_sha256_t *s,const void *data,size_t length);
void jfin_sha256_finish(jfin_sha256_t *s,unsigned char digest[32]);
#endif
