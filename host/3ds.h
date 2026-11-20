// Host (PC) shim of the libctru/citro2d APIs used by HK3DS, for headless testing + screenshots.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <sys/stat.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef s32 Result; typedef u32 Handle;
#define R_SUCCEEDED(r) ((r) >= 0)
#define CUR_THREAD_HANDLE 0xFFFF8000
#define U64_MAX UINT64_MAX

enum {
    KEY_A = 1 << 0, KEY_B = 1 << 1, KEY_SELECT = 1 << 2, KEY_START = 1 << 3, KEY_DRIGHT = 1 << 4, KEY_DLEFT = 1 << 5,
    KEY_DUP = 1 << 6, KEY_DDOWN = 1 << 7, KEY_R = 1 << 8, KEY_L = 1 << 9, KEY_X = 1 << 10, KEY_Y = 1 << 11,
    KEY_ZL = 1 << 14, KEY_ZR = 1 << 15,
};
typedef struct { s16 dx, dy; } circlePosition;
typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;

void hidScanInput(void); u32 hidKeysDown(void); u32 hidKeysHeld(void); u32 hidKeysUp(void); void hidCircleRead(circlePosition *);
bool aptMainLoop(void);
void gfxInitDefault(void); void gfxSetWide(bool w); void gfxExit(void); void gspWaitForVBlank(void);
void *consoleInit(gfxScreen_t, void *);
Result romfsInit(void); Result romfsExit(void);
void osSetSpeedupEnable(bool); u64 osGetTime(void); u64 svcGetSystemTick(void);
Result svcGetThreadPriority(s32 *out, Handle h);
void *linearAlloc(size_t); void linearFree(void *);
Result GSPGPU_FlushDataCache(const void *, u32); Result DSP_FlushDataCache(const void *, u32);

typedef struct { int x; } LightLock;
typedef struct { int x; } LightEvent;
enum { RESET_ONESHOT = 0 };
static inline void LightLock_Init(LightLock *l) { (void)l; }
static inline void LightLock_Lock(LightLock *l) { (void)l; }
static inline void LightLock_Unlock(LightLock *l) { (void)l; }
static inline void LightEvent_Init(LightEvent *e, int t) { (void)e; (void)t; }
static inline void LightEvent_Signal(LightEvent *e) { (void)e; }
static inline void LightEvent_Wait(LightEvent *e) { (void)e; }
typedef void *Thread;
typedef void (*ThreadFunc)(void *);
static inline Thread threadCreate(ThreadFunc f, void *a, size_t s, int p, int c, bool d) { (void)f; (void)a; (void)s; (void)p; (void)c; (void)d; return NULL; }
static inline Result threadJoin(Thread t, u64 to) { (void)t; (void)to; return 0; }
static inline void threadFree(Thread t) { (void)t; }

// ndsp (silent)
typedef enum { NDSP_WBUF_FREE = 0, NDSP_WBUF_QUEUED, NDSP_WBUF_PLAYING, NDSP_WBUF_DONE } ndspWbufStatus;
typedef struct { union { s16 *data_pcm16; void *data_vaddr; }; u32 nsamples; void *adpcm; u32 offset; bool looping; u8 status; u16 sequence_id; void *next; } ndspWaveBuf;
enum { NDSP_OUTPUT_STEREO = 1, NDSP_INTERP_LINEAR = 1, NDSP_FORMAT_MONO_PCM16 = 5, NDSP_FORMAT_STEREO_PCM16 = 10 };
static inline Result ndspInit(void) { return -1; }
static inline void ndspExit(void) {}
static inline void ndspSetOutputMode(int m) { (void)m; }
static inline void ndspSetCallback(void (*cb)(void *), void *d) { (void)cb; (void)d; }
static inline void ndspChnReset(int c) { (void)c; }
static inline void ndspChnSetInterp(int c, int i) { (void)c; (void)i; }
static inline void ndspChnSetRate(int c, float r) { (void)c; (void)r; }
static inline void ndspChnSetFormat(int c, int f) { (void)c; (void)f; }
static inline void ndspChnSetMix(int c, float *m) { (void)c; (void)m; }
static inline void ndspChnWaveBufAdd(int c, ndspWaveBuf *b) { (void)c; (void)b; }
static inline void ndspChnWaveBufClear(int c) { (void)c; }
static inline bool ndspChnIsPlaying(int c) { (void)c; return false; }

// file path remap (romfs:/ -> ./romfs/, sdmc:/ -> ./sdmc/)
FILE *host_fopen(const char *p, const char *m);
int host_mkdir(const char *p, int mode);
#define fopen host_fopen
#define mkdir host_mkdir

// APT hooks (no-ops on the host)
typedef enum { APTHOOK_ONSUSPEND = 0, APTHOOK_ONRESTORE, APTHOOK_ONSLEEP, APTHOOK_ONWAKEUP, APTHOOK_ONEXIT, APTHOOK_COUNT } APT_HookType;
typedef void (*aptHookFn)(APT_HookType hook, void *param);
typedef struct tag_aptHookCookie { struct tag_aptHookCookie *next; aptHookFn callback; void *param; } aptHookCookie;
static inline void aptHook(aptHookCookie *c, aptHookFn fn, void *p) { (void)c; (void)fn; (void)p; }
static inline void aptUnhook(aptHookCookie *c) { (void)c; }
