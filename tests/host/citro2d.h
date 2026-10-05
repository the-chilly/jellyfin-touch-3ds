#ifndef TEST_CITRO_H
#define TEST_CITRO_H
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "3ds.h"
typedef struct { int dummy; } C3D_RenderTarget;
typedef struct { void *data; } C3D_Tex;
typedef struct { unsigned short width,height; float left,top,right,bottom; } Tex3DS_SubTexture;
typedef struct { C3D_Tex *tex; Tex3DS_SubTexture *subtex; } C2D_Image;
enum { GPU_RGB565, GPU_LINEAR };
#ifdef HOST_GPU_CHECK
extern pthread_t host_gpu_thread;
#include <assert.h>
#define CHECK_GPU() assert(pthread_equal(pthread_self(),host_gpu_thread))
#else
#define CHECK_GPU() ((void)0)
#endif
static inline bool C3D_TexInit(C3D_Tex *t,int w,int h,int format) { CHECK_GPU();(void)format;t->data=calloc(w*h,2);return t->data!=NULL; }
static inline void C3D_TexSetFilter(C3D_Tex *t,int a,int b) { CHECK_GPU();(void)t;(void)a;(void)b; }
static inline void C3D_TexFlush(C3D_Tex *t) { CHECK_GPU();(void)t; }
static inline void C3D_TexDelete(C3D_Tex *t) { CHECK_GPU();free(t->data); }
static inline void C2D_DrawImageAt(C2D_Image i,float x,float y,float z,void *t,float sx,float sy) { CHECK_GPU();(void)i;(void)x;(void)y;(void)z;(void)t;(void)sx;(void)sy; }
typedef void *C2D_TextBuf;
typedef void *C2D_Font;
typedef struct { const char *text; } C2D_Text;
enum { C2D_WithColor, C3D_FRAME_SYNCDRAW };
#ifdef HOST_RENDER_TRACE
void host_trace(int kind,float x,float y,float w,float h,u32 color,const char *text);
#define TRACE(k,x,y,w,h,c,t) host_trace(k,x,y,w,h,c,t)
#else
#define TRACE(k,x,y,w,h,c,t) ((void)0)
#endif
static inline u32 C2D_Color32(u8 r,u8 g,u8 b,u8 a) { return r|g<<8|b<<16|a<<24; }
static inline void C2D_TextParse(C2D_Text *t,C2D_TextBuf b,const char *s) { (void)b;t->text=s; }
static inline void C2D_TextOptimize(C2D_Text *t) { (void)t; }
static inline void C2D_TextGetDimensions(const C2D_Text *t,float x,float y,float *w,float *h) { if(w)*w=strlen(t->text)*12*x;if(h)*h=20*y; }
static inline void C2D_DrawText(const C2D_Text *t,int f,float x,float y,float z,float sx,float sy,u32 c) { TRACE(2,x,y,sx,sy,c,t->text);(void)t;(void)f;(void)x;(void)y;(void)z;(void)sx;(void)sy;(void)c; }
static inline void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c) { TRACE(1,x,y,w,h,c,NULL);(void)x;(void)y;(void)z;(void)w;(void)h;(void)c; }
static inline C3D_RenderTarget *C2D_CreateScreenTarget(int s,int e) { (void)s;(void)e;return (C3D_RenderTarget *)(uintptr_t)(s==GFX_TOP?1:2); }
static inline C2D_TextBuf C2D_TextBufNew(size_t n) { return malloc(n); }
static inline void C2D_TextBufDelete(C2D_TextBuf b) { free(b); }
static inline void C2D_TextBufClear(C2D_TextBuf b) { (void)b; }
static inline void C2D_TargetClear(C3D_RenderTarget *t,u32 c) { TRACE(0,(float)(uintptr_t)t,0,0,0,c,NULL);(void)t;(void)c; }
static inline void C2D_SceneBegin(C3D_RenderTarget *t) { TRACE(3,(float)(uintptr_t)t,0,0,0,0,NULL);(void)t; }
static inline void C3D_FrameBegin(int f) {
#ifdef HOST_APT_CLOSE
    assert(!host_apt_closing);
#endif
    (void)f;
}
static inline void C3D_FrameSync(void) {}
static inline void C3D_FrameEnd(int f) { (void)f; }
#endif
