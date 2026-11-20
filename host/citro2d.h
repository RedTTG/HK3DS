#pragma once
#include "3ds.h"
typedef enum { GPU_RGBA8 = 0, GPU_RGB8 = 1, GPU_RGBA5551 = 2, GPU_RGB565 = 3, GPU_RGBA4 = 4, GPU_A8 = 8 } GPU_TEXCOLOR;
enum { GPU_NEAREST = 0, GPU_LINEAR = 1 };
enum { GPU_CLAMP_TO_EDGE = 0, GPU_REPEAT = 2 };
typedef struct { void *data; u16 width, height; GPU_TEXCOLOR fmt; u32 size; unsigned char linear; } C3D_Tex;
typedef struct { u16 width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
typedef struct { C3D_Tex *tex; const Tex3DS_SubTexture *subtex; } C2D_Image;
typedef struct { u32 color; float blend; } C2D_TintCorner;
typedef struct { C2D_TintCorner corners[4]; } C2D_ImageTint;
typedef enum { C2D_TintSolid, C2D_TintMult, C2D_TintLuma } C2D_TintMode;
extern int host_tintmode;
static inline bool C2D_SetTintMode(C2D_TintMode m) { host_tintmode = m; return true; }
typedef struct { int screen; } C3D_RenderTarget;
typedef struct { struct { float x, y, w, h; } pos; struct { float x, y; } center; float depth; float angle; } C2D_DrawParams;
bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams *params, const C2D_ImageTint *tint);
#define C3D_DEFAULT_CMDBUF_SIZE 0x40000
#define C2D_DEFAULT_MAX_OBJECTS 4096
#define C3D_FRAME_SYNCDRAW 1
bool C3D_TexInit(C3D_Tex *t, u16 w, u16 h, GPU_TEXCOLOR f);
void C3D_TexDelete(C3D_Tex *t);
static inline void C3D_TexSetFilter(C3D_Tex *t, int a, int b) { (void)b; t->linear = a == GPU_LINEAR; }
static inline void C3D_TexSetWrap(C3D_Tex *t, int a, int b) { (void)t; (void)a; (void)b; }
static inline bool C3D_Init(size_t s) { (void)s; return true; }
static inline void C3D_Fini(void) {}
void C3D_FrameBegin(int f); void C3D_FrameEnd(int f);
static inline bool C2D_Init(size_t n) { (void)n; return true; }
static inline void C2D_Prepare(void) {}
static inline void C2D_Fini(void) {}
C3D_RenderTarget *C2D_CreateScreenTarget(gfxScreen_t s, gfx3dSide_t side);
void C2D_TargetClear(C3D_RenderTarget *t, u32 c);
void C2D_SceneBegin(C3D_RenderTarget *t);
static inline u32 C2D_Color32(u8 r, u8 g, u8 b, u8 a) { return r | (g << 8) | (b << 16) | ((u32)a << 24); }
static inline u32 C2D_Color32f(float r, float g, float b, float a) {
    #define CL(x) ((u8)((x) < 0 ? 0 : (x) > 1 ? 255 : (x) * 255 + 0.5f))
    return C2D_Color32(CL(r), CL(g), CL(b), CL(a));
}
static inline void C2D_PlainImageTint(C2D_ImageTint *t, u32 c, float b) { for (int i = 0; i < 4; i++) { t->corners[i].color = c; t->corners[i].blend = b; } }
bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth, const C2D_ImageTint *tint, float sx, float sy);
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 clr);
bool C2D_DrawTriangle(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2, float depth);
