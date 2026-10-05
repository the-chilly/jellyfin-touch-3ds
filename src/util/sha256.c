/* SHA-256, FIPS 180-4. Portable streaming implementation. */
#include "util/sha256.h"
#include <string.h>
static const uint32_t k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t r(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void compress(jfin_sha256_t *s){
 uint32_t w[64];for(int i=0;i<16;i++)w[i]=(uint32_t)s->block[i*4]<<24|(uint32_t)s->block[i*4+1]<<16|(uint32_t)s->block[i*4+2]<<8|s->block[i*4+3];
 for(int i=16;i<64;i++){uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(r(x,7)^r(x,18)^(x>>3))+w[i-7]+(r(y,17)^r(y,19)^(y>>10));}
 uint32_t a=s->state[0],b=s->state[1],c=s->state[2],d=s->state[3],e=s->state[4],f=s->state[5],g=s->state[6],h=s->state[7];
 for(int i=0;i<64;i++){uint32_t t=h+(r(e,6)^r(e,11)^r(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(r(a,2)^r(a,13)^r(a,22))+((a&b)^(a&c)^(b&c));h=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
 s->state[0]+=a;s->state[1]+=b;s->state[2]+=c;s->state[3]+=d;s->state[4]+=e;s->state[5]+=f;s->state[6]+=g;s->state[7]+=h;
}
void jfin_sha256_init(jfin_sha256_t *s){
 const uint32_t initial[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memset(s,0,sizeof(*s));memcpy(s->state,initial,sizeof(initial));
}
void jfin_sha256_update(jfin_sha256_t *s,const void *data,size_t length){
 const unsigned char *p=data;s->bytes+=length;
 while(length){size_t n=64-s->used;if(n>length)n=length;memcpy(s->block+s->used,p,n);s->used+=n;p+=n;length-=n;if(s->used==64){compress(s);s->used=0;}}
}
void jfin_sha256_finish(jfin_sha256_t *s,unsigned char digest[32]){
 uint64_t bits=s->bytes*8;s->block[s->used++]=0x80;if(s->used>56){memset(s->block+s->used,0,64-s->used);compress(s);s->used=0;}
 memset(s->block+s->used,0,56-s->used);for(int i=0;i<8;i++)s->block[63-i]=(unsigned char)(bits>>(i*8));compress(s);
 for(int i=0;i<8;i++)for(int j=0;j<4;j++)digest[i*4+j]=(unsigned char)(s->state[i]>>(24-j*8));
}
