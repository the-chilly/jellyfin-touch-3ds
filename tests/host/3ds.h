#ifndef TEST_3DS_H
#define TEST_3DS_H
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
typedef uint16_t u16; typedef int32_t s32;
typedef pthread_mutex_t LightLock;
static inline void LightLock_Init(LightLock *l) { pthread_mutex_init(l,NULL); }
static inline void LightLock_Lock(LightLock *l) { pthread_mutex_lock(l); }
static inline void LightLock_Unlock(LightLock *l) { pthread_mutex_unlock(l); }
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0
typedef struct { pthread_t thread; void (*fn)(void *); void *arg; } *Thread;
static inline void *host_thread(void *p) { Thread t=p;t->fn(t->arg);return NULL; }
static inline Thread threadCreate(void (*fn)(void *),void *arg,size_t stack,int priority,int core,bool detached) {
 (void)stack;(void)priority;(void)core;(void)detached;Thread t=malloc(sizeof(*t));if(!t)return NULL;t->fn=fn;t->arg=arg;
 if(pthread_create(&t->thread,NULL,host_thread,t)) {free(t);return NULL;} return t; }
static inline void threadJoin(Thread t,uint64_t n) { (void)n;pthread_join(t->thread,NULL); }
static inline void threadFree(Thread t) { free(t); }
static inline void svcGetThreadPriority(s32 *p,int h) { (void)h;*p=0; }
typedef uint32_t u32; typedef uint8_t u8; typedef uint64_t u64; typedef int64_t s64;
typedef struct { unsigned short px, py; } touchPosition;
typedef struct { int dummy; } SwkbdState;
typedef int SwkbdType; typedef int SwkbdButton;
enum { KEY_A=1, KEY_B=2, KEY_X=4, KEY_Y=8, KEY_DUP=16, KEY_DDOWN=32,
 KEY_DLEFT=64, KEY_DRIGHT=128, KEY_L=256, KEY_R=512, KEY_SELECT=1024, KEY_TOUCH=2048, KEY_ZR=4096 };
enum { SWKBD_TYPE_WESTERN, SWKBD_BUTTON_CONFIRM, SWKBD_PASSWORD_HIDE_DELAY,
 GFX_TOP, GFX_BOTTOM, GFX_LEFT, GFX_RIGHT };
static inline u64 svcGetSystemTick(void) { static u64 t; return ++t; }
static inline u64 osGetTime(void) { return 0; }
static inline float osGet3DSliderState(void) { return 0; }
static inline void svcSleepThread(s64 n) { struct timespec ts={n/1000000000LL,n%1000000000LL}; nanosleep(&ts,NULL); }
#ifdef HOST_APT_CLOSE
extern bool host_apt_closing;
static inline bool aptMainLoop(void) { return !host_apt_closing; }
#else
static inline bool aptMainLoop(void) { return true; }
#endif
static inline void hidScanInput(void) {}
static inline u32 hidKeysDown(void) { return 0; }
static inline void gfxSet3D(bool b) { (void)b; }
static inline void swkbdInit(SwkbdState *s, int t, int b, int m) { (void)s;(void)t;(void)b;(void)m; }
static inline void swkbdSetHintText(SwkbdState *s, const char *t) { (void)s;(void)t; }
static inline void swkbdSetInitialText(SwkbdState *s, const char *t) { (void)s;(void)t; }
static inline void swkbdSetPasswordMode(SwkbdState *s, int t) { (void)s;(void)t; }
static inline SwkbdButton swkbdInputText(SwkbdState *s, char *b, unsigned long n) { (void)s;(void)b;(void)n; return -1; }
#endif
