// Software implementation of the tiny subset of libctru/citro2d used by HK3DS.
// Runs the real game code headless, driven by a scripted input list, and writes PNG screenshots.
#include "3ds.h"
#include "citro2d.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#undef fopen
#undef mkdir

#ifdef HK_HD
#define TOPX 2           // the HD build's top screen: 800x240 pixels for citro2d's 400x240 coordinates
#else
#define TOPX 1
#endif
#define TOPW (400 * TOPX)
static float fbTop[240][TOPW][3], fbBot[240][320][3];
static int curScreen;
static long frameNo;
long host_frame(void) { return frameNo; }
static u32 keysNow, keysPrev;
static char *script; static int segFrames[4096]; static u32 segKeys[4096]; static int nseg; static long totalFrames;
static long shots[256]; static int nshots;
static const char *outDir = "shots";

FILE *host_fopen(const char *p, const char *m) {
    char buf[512];
    if (!strncmp(p, "romfs:/", 7)) { snprintf(buf, sizeof buf, "romfs/%s", p + 7); return fopen(buf, m); }
    if (!strncmp(p, "sdmc:/", 6)) { snprintf(buf, sizeof buf, "sdmc/%s", p + 6); return fopen(buf, m); }
    return fopen(p, m);
}
int host_mkdir(const char *p, int mode) {
    char buf[512];
    if (!strncmp(p, "sdmc:/", 6)) { mkdir("sdmc", 0777); snprintf(buf, sizeof buf, "sdmc/%s", p + 6); return mkdir(buf, mode); }
    return mkdir(p, mode);
}

static u32 parse_keys(const char *s) {
    u32 k = 0;
    if (strstr(s, "A")) { if (strstr(s, "A")) k |= KEY_A; }
    const char *names[] = { "B", "Y", "X", "R", "L", "START", "UP", "DOWN", "LEFT", "RIGHT", "SELECT", "ZL", "ZR" };
    u32 vals[] = { KEY_B, KEY_Y, KEY_X, KEY_R, KEY_L, KEY_START, KEY_DUP, KEY_DDOWN, KEY_DLEFT, KEY_DRIGHT, KEY_SELECT, KEY_ZL, KEY_ZR };
    k = 0;
    char tmp[128]; strncpy(tmp, s, 127); tmp[127] = 0;
    char *sv2; for (char *t = strtok_r(tmp, "+", &sv2); t; t = strtok_r(NULL, "+", &sv2)) {
        if (!strcmp(t, "A")) k |= KEY_A;
        for (int i = 0; i < 13; i++) if (!strcmp(t, names[i])) k |= vals[i];
    }
    return k;
}

static void init_script(void) {
    const char *s = getenv("HK_SCRIPT");
    if (!s) s = "300:";
    script = strdup(s);
    char *sv; for (char *seg = strtok_r(script, ";", &sv); seg && nseg < 4096; seg = strtok_r(NULL, ";", &sv)) {
        char *c = strchr(seg, ':');
        segFrames[nseg] = atoi(seg);
        segKeys[nseg] = c ? parse_keys(c + 1) : 0;
        totalFrames += segFrames[nseg]; nseg++;
    }
    const char *sh = getenv("HK_SHOTS");
    if (sh) { char *t = strdup(sh); for (char *x = strtok(t, ","); x && nshots < 256; x = strtok(NULL, ",")) shots[nshots++] = atol(x); }
    if (getenv("HK_OUT")) outDir = getenv("HK_OUT");
    mkdir(outDir, 0777);
}

void hidScanInput(void) {
    keysPrev = keysNow;
    long f = frameNo, acc = 0; keysNow = 0;
    for (int i = 0; i < nseg; i++) { if (f < acc + segFrames[i]) { keysNow = segKeys[i]; break; } acc += segFrames[i]; }
}
u32 hidKeysDown(void) { return keysNow & ~keysPrev; }
u32 hidKeysHeld(void) { return keysNow; }
u32 hidKeysUp(void) { return keysPrev & ~keysNow; }
void hidCircleRead(circlePosition *c) { c->dx = c->dy = 0; }
bool aptMainLoop(void) { if (!script) init_script(); return frameNo < totalFrames; }
void gfxInitDefault(void) {} void gfxExit(void) {} void gfxSetWide(bool w) { (void)w; } void gspWaitForVBlank(void) { frameNo++; }
void *consoleInit(gfxScreen_t s, void *c) { (void)s; (void)c; return NULL; }
Result romfsInit(void) { return 0; } Result romfsExit(void) { return 0; }
void osSetSpeedupEnable(bool b) { (void)b; }
// HK_JITTER=1: frames take 1, 2 or 3 vblanks at random (a 3DS frame that runs long waits for the next vblank)
u64 osGetTime(void) { static double fps = 0, t = 0; static long lastF = 0; static int jit = -1;
    if (fps == 0) fps = getenv("HK_FPS") ? atof(getenv("HK_FPS")) : 60;
    if (jit < 0) { jit = getenv("HK_JITTER") != NULL; srand(1234); }
    if (!jit) return (u64)(frameNo * 1000.0 / fps);
    while (lastF < frameNo) { lastF++; t += (1 + rand() % 3) * 1000.0 / fps; }
    return (u64)t; }
u64 svcGetSystemTick(void) { return (u64)(frameNo * 268111856.0 / 60.0); }
Result svcGetThreadPriority(s32 *o, Handle h) { (void)h; *o = 0x30; return 0; }
void *linearAlloc(size_t n) { return calloc(1, n); }
void linearFree(void *p) { free(p); }
Result GSPGPU_FlushDataCache(const void *p, u32 n) { (void)p; (void)n; return 0; }
Result DSP_FlushDataCache(const void *p, u32 n) { (void)p; (void)n; return 0; }

static int bpp_of(GPU_TEXCOLOR f) { return f == GPU_RGBA8 ? 4 : f == GPU_RGB8 ? 3 : f == GPU_A8 ? 1 : 2; }
bool C3D_TexInit(C3D_Tex *t, u16 w, u16 h, GPU_TEXCOLOR f) {
    t->width = w; t->height = h; t->fmt = f; t->linear = 0; t->size = w * h * bpp_of(f); t->data = calloc(1, t->size); return t->data != NULL;
}
void C3D_TexDelete(C3D_Tex *t) { free(t->data); t->data = NULL; }

static C3D_RenderTarget targets[2] = { { 0 }, { 1 } };
C3D_RenderTarget *C2D_CreateScreenTarget(gfxScreen_t s, gfx3dSide_t side) { (void)side; return &targets[s]; }
void C2D_SceneBegin(C3D_RenderTarget *t) { curScreen = t->screen; }
void C2D_TargetClear(C3D_RenderTarget *t, u32 c) {
    float r = (c & 255) / 255.f, g = (c >> 8 & 255) / 255.f, b = (c >> 16 & 255) / 255.f;
    int W = t->screen ? 320 : TOPW;
    for (int y = 0; y < 240; y++) for (int x = 0; x < W; x++) {
        float *p = t->screen ? fbBot[y][x] : fbTop[y][x]; p[0] = r; p[1] = g; p[2] = b;
    }
}
static int skipDraw = 0;
// HK_QUADTRACE: quads citro2d would have to hold this frame (C2D_Init(4096) on the 3DS: draws past that are dropped)
long g_quads; static long quadMax, quadOver; static int quadFrames;
extern double g_fill, g_fillTiles;
void C3D_FrameBegin(int f) {
    (void)f;
    skipDraw = 0;
    if (getenv("HK_FAST")) { skipDraw = 1; for (int i = 0; i < nshots; i++) if (shots[i] == frameNo) skipDraw = 0; }
}

// game code asks for shots from now on (every `every` frames, `count` of them): a test can catch a moment whose frame
// number it doesn't know (HK_FAST skips drawing other frames, and that changes the run)
void host_request_shots(int every, int count) {
    for (int i = 0; i < count && nshots < 256; i++) shots[nshots++] = frameNo + 1 + (long)i * every;
}
// camera tour helpers (game.c, HK_CAMTOUR)
float (*host_fbtop(void))[400][3] { return (float (*)[400][3])fbTop; }     // (the camera tour: standard build only)
void host_force_draw(int on) { skipDraw = on ? 0 : 1; curScreen = 0; }
void host_write_top_png(const char *path) {
    static u8 out[240 * TOPW * 3];
    for (int y = 0; y < 240; y++) for (int x = 0; x < TOPW; x++) for (int c = 0; c < 3; c++) {
        float v = fbTop[y][x][c]; out[(y * TOPW + x) * 3 + c] = (u8)(v < 0 ? 0 : v > 1 ? 255 : v * 255); }
    stbi_write_png(path, TOPW, 240, 3, out, TOPW * 3);
}
static void save_png(void) {
    char path[256];
    enum { OW = TOPW + 320 };
    static u8 out[240 * OW * 3];
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < OW; x++) {
            float *p = x < TOPW ? fbTop[y][x] : fbBot[y][x - TOPW];
            for (int c = 0; c < 3; c++) { float v = p[c]; out[(y * OW + x) * 3 + c] = (u8)(v < 0 ? 0 : v > 1 ? 255 : v * 255); }
        }
    }
    snprintf(path, sizeof path, "%s/f%05ld.png", outDir, frameNo);
    stbi_write_png(path, OW, 240, 3, out, OW * 3);
}
void C3D_FrameEnd(int f) {
    (void)f;
    if (getenv("HK_QUADTRACE")) { if (g_quads > quadMax) quadMax = g_quads; if (g_quads > 4096) quadOver++;
        if (++quadFrames % 60 == 0) { fprintf(stderr, "[quads] frame %ld max %ld over4096 %ld\n", frameNo, quadMax, quadOver); quadMax = quadOver = 0; } }
    g_quads = 0;
    if (getenv("HK_FILLTRACE")) { static double fs, ft, fm; static int fn; fs += g_fill; ft += g_fillTiles; if (g_fill > fm) fm = g_fill; fn++;
        if (fn % 60 == 0) { double S = TOPW * 240.0; fprintf(stderr, "[fill] frame %ld overdraw avg %.2f (tiles %.2f) max %.2f\n", frameNo, fs / fn / S, ft / fn / S, fm / S); fs = ft = fm = 0; fn = 0; } }
    g_fill = g_fillTiles = 0;
    for (int i = 0; i < nshots; i++) if (shots[i] == frameNo) save_png();
    frameNo++;
}

static int morton(int x, int y) { return (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3); }
static void texel(const C3D_Tex *t, int x, int y, float out[4]) {
    // y here is the texture memory row (row 0 is sampled at v=1 on real hardware)
    if (x < 0) x = 0; if (y < 0) y = 0; if (x >= t->width) x = t->width - 1; if (y >= t->height) y = t->height - 1;
    int tile = (y >> 3) * (t->width >> 3) + (x >> 3);
    int idx = tile * 64 + morton(x & 7, y & 7);
    const u8 *d = t->data;
    switch (t->fmt) {
        case GPU_RGBA8: { const u8 *p = d + idx * 4; out[0] = p[3] / 255.f; out[1] = p[2] / 255.f; out[2] = p[1] / 255.f; out[3] = p[0] / 255.f; break; }
        case GPU_A8: { out[0] = out[1] = out[2] = 0; out[3] = d[idx] / 255.f; break; }
        case GPU_RGBA4: { u16 v = ((const u16 *)d)[idx]; out[0] = (v >> 12 & 15) / 15.f; out[1] = (v >> 8 & 15) / 15.f; out[2] = (v >> 4 & 15) / 15.f; out[3] = (v & 15) / 15.f; break; }
        case GPU_RGB565: { u16 v = ((const u16 *)d)[idx]; out[0] = (v >> 11) / 31.f; out[1] = (v >> 5 & 63) / 63.f; out[2] = (v & 31) / 31.f; out[3] = 1; break; }
        case GPU_RGBA5551: { u16 v = ((const u16 *)d)[idx]; out[0] = (v >> 11) / 31.f; out[1] = (v >> 6 & 31) / 31.f; out[2] = (v >> 1 & 31) / 31.f; out[3] = v & 1; break; }
        default: out[0] = out[1] = out[2] = out[3] = 1;
    }
}

// the GPU's bilinear filter (textures set to GPU_LINEAR): u, v in texture pixels, memory rows (v = 0 at row 0)
static void sample(const C3D_Tex *t, float u, float v, float out[4]) {
    static int nearest = -1; if (nearest < 0) nearest = getenv("HK_NEARESTTEX") != NULL;
    if (!t->linear || nearest) { texel(t, (int)u, (int)v, out); return; }
    u -= 0.5f; v -= 0.5f;
    int x0 = (int)floorf(u), y0 = (int)floorf(v); float fx = u - x0, fy = v - y0;
    float a[4], b[4], c[4], d[4];
    texel(t, x0, y0, a); texel(t, x0 + 1, y0, b); texel(t, x0, y0 + 1, c); texel(t, x0 + 1, y0 + 1, d);
    for (int i = 0; i < 4; i++) out[i] = (a[i] * (1 - fx) + b[i] * fx) * (1 - fy) + (c[i] * (1 - fx) + d[i] * fx) * fy;
}

int host_additive;
int host_tintmode;
static void blend_px(int x, int y, const float c[4]) {
    int W = curScreen ? 320 : TOPW;
    if (x < 0 || y < 0 || x >= W || y >= 240) return;
    float *p = curScreen ? fbBot[y][x] : fbTop[y][x];
    if (host_additive) { for (int i = 0; i < 3; i++) { p[i] += c[i] * c[3]; if (p[i] > 1) p[i] = 1; } return; }
    for (int i = 0; i < 3; i++) p[i] = c[i] * c[3] + p[i] * (1 - c[3]);
}

#define XK (curScreen ? 1 : TOPX)
// HK_FILLTRACE: top-screen pixels drawn per frame (quad areas clipped to the screen, in device pixels): GPU fill cost
double g_fill, g_fillTiles; int g_fillIsTile;
static void add_fill(float x0, float y0, float x1, float y1) {
    if (curScreen) return;
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > 400) x1 = 400; if (y1 > 240) y1 = 240;
    if (x1 <= x0 || y1 <= y0) return;
    double a = (double)(x1 - x0) * (y1 - y0) * TOPX; g_fill += a; if (g_fillIsTile) g_fillTiles += a;
}
bool C2D_DrawImageAt(C2D_Image img, float x, float y, float depth, const C2D_ImageTint *tint, float sx, float sy) {
    g_quads++;
    { float fw = fabsf(img.subtex->width * sx), fh = fabsf(img.subtex->height * sy); add_fill(x, y, x + fw, y + fh); }
    if (skipDraw) return true;
    (void)depth;
    const Tex3DS_SubTexture *s = img.subtex;
    // like citro2d: the quad always spans [x, x+|w|] x [y, y+|h|]; a negative scale only flips the texture
    float w = s->width * sx, h = s->height * sy;
    float x0 = x, x1 = x + fabsf(w), y0 = y, y1 = y + fabsf(h);
    float tc[4] = { 1, 1, 1, 1 }; float bl = 0;
    if (tint) { u32 c = tint->corners[0].color; tc[0] = (c & 255) / 255.f; tc[1] = (c >> 8 & 255) / 255.f; tc[2] = (c >> 16 & 255) / 255.f; tc[3] = (c >> 24) / 255.f; bl = tint->corners[0].blend; }
    for (int py = (int)floorf(y0); py < (int)ceilf(y1); py++) {
        for (int px = (int)floorf(x0 * XK); px < (int)ceilf(x1 * XK); px++) {
            float fx = ((px + 0.5f) / XK - x) / fabsf(w), fy = (py + 0.5f - y) / fabsf(h);
            if (w < 0) fx = 1 - fx;      // flipped texture
            if (h < 0) fy = 1 - fy;
            if (fx < 0 || fx > 1 || fy < 0 || fy > 1) continue;
            float u, v;
            if (s->top < s->bottom) {   // citro2d/tex3ds: top < bottom means a rotated subtexture
                u = s->top + (s->bottom - s->top) * fy;
                v = s->left + (s->right - s->left) * fx;
            } else {
                u = s->left + (s->right - s->left) * fx;
                v = s->top + (s->bottom - s->top) * fy;
            }
            float c[4];
            sample(img.tex, u * img.tex->width, (1.0f - v) * img.tex->height, c);   // like the real PICA200: v=1 is memory row 0
            for (int i = 0; i < 3; i++) c[i] = host_tintmode == 1 ? c[i] * (1 - bl + tc[i] * bl) : c[i] * (1 - bl) + tc[i] * bl;
            c[3] *= tc[3];
            if (c[3] > 0.002f) blend_px(px, py, c);
        }
    }
    return true;
}


// citro2d C2D_DrawImage: quad corners relative to center, rotated by angle (screen space), flips by sign of w/h
bool C2D_DrawImage(C2D_Image img, const C2D_DrawParams *pr, const C2D_ImageTint *tint) {
    g_quads++;
    { float fw = fabsf(pr->pos.w), fh = fabsf(pr->pos.h); add_fill(pr->pos.x - pr->center.x, pr->pos.y - pr->center.y, pr->pos.x - pr->center.x + fw, pr->pos.y - pr->center.y + fh); }
    if (skipDraw) return true;
    const Tex3DS_SubTexture *s = img.subtex;
    float w = fabsf(pr->pos.w), h = fabsf(pr->pos.h);
    if (w <= 0 || h <= 0) return true;
    float cs = cosf(pr->angle), sn = sinf(pr->angle);
    // bounding box of the rotated quad
    float qx[4] = { -pr->center.x, -pr->center.x + w, -pr->center.x, -pr->center.x + w }, qy[4] = { -pr->center.y, -pr->center.y, -pr->center.y + h, -pr->center.y + h };
    float mnx = 1e9, mny = 1e9, mxx = -1e9, mxy = -1e9;
    for (int i = 0; i < 4; i++) { float X = qx[i] * cs - qy[i] * sn + pr->pos.x, Y = qx[i] * sn + qy[i] * cs + pr->pos.y; if (X < mnx) mnx = X; if (X > mxx) mxx = X; if (Y < mny) mny = Y; if (Y > mxy) mxy = Y; }
    float tc[4] = { 1, 1, 1, 1 }; float bl = 0;
    if (tint) { u32 c = tint->corners[0].color; tc[0] = (c & 255) / 255.f; tc[1] = (c >> 8 & 255) / 255.f; tc[2] = (c >> 16 & 255) / 255.f; tc[3] = (c >> 24) / 255.f; bl = tint->corners[0].blend; }
    for (int py = (int)floorf(mny); py < (int)ceilf(mxy); py++)
        for (int px = (int)floorf(mnx * XK); px < (int)ceilf(mxx * XK); px++) {
            float dx = (px + 0.5f) / XK - pr->pos.x, dy = py + 0.5f - pr->pos.y;
            float lx = dx * cs + dy * sn + pr->center.x, ly = -dx * sn + dy * cs + pr->center.y;   // inverse rotation
            float fx = lx / w, fy = ly / h;
            if (fx < 0 || fx > 1 || fy < 0 || fy > 1) continue;
            if (pr->pos.w < 0) fx = 1 - fx;
            if (pr->pos.h < 0) fy = 1 - fy;
            float u, v;
            if (s->top < s->bottom) { u = s->top + (s->bottom - s->top) * fy; v = s->left + (s->right - s->left) * fx; }
            else { u = s->left + (s->right - s->left) * fx; v = s->top + (s->bottom - s->top) * fy; }
            float c[4];
            sample(img.tex, u * img.tex->width, (1.0f - v) * img.tex->height, c);
            for (int i = 0; i < 3; i++) c[i] = host_tintmode == 1 ? c[i] * (1 - bl + tc[i] * bl) : c[i] * (1 - bl) + tc[i] * bl;
            c[3] *= tc[3];
            if (c[3] > 0.002f) blend_px(px, py, c);
        }
    return true;
}

bool C2D_DrawTriangle(float x0, float y0, u32 c0, float x1, float y1, u32 c1, float x2, float y2, u32 c2, float depth) {
    g_quads++;
    if (skipDraw) return true;
    (void)depth;
    float mnx = fminf(x0, fminf(x1, x2)), mxx = fmaxf(x0, fmaxf(x1, x2)), mny = fminf(y0, fminf(y1, y2)), mxy = fmaxf(y0, fmaxf(y1, y2));
    if (mnx < 0) mnx = 0; if (mny < 0) mny = 0; if (mxx > 400) mxx = 400; if (mxy > 240) mxy = 240;
    mnx *= XK; mxx *= XK;
    float d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2); if (fabsf(d) < 1e-6f) return true;
    u32 cc[3] = { c0, c1, c2 };
    for (int py = (int)floorf(mny); py < (int)ceilf(mxy); py++)
        for (int px = (int)floorf(mnx); px < (int)ceilf(mxx); px++) {
            float X = (px + 0.5f) / XK, Y = py + 0.5f;
            float l0 = ((y1 - y2) * (X - x2) + (x2 - x1) * (Y - y2)) / d, l1 = ((y2 - y0) * (X - x2) + (x0 - x2) * (Y - y2)) / d, l2 = 1 - l0 - l1;
            if (l0 < 0 || l1 < 0 || l2 < 0) continue;
            float c[4] = { 0, 0, 0, 0 }; float l[3] = { l0, l1, l2 };
            for (int k = 0; k < 3; k++) { c[0] += l[k] * (cc[k] & 255) / 255.f; c[1] += l[k] * (cc[k] >> 8 & 255) / 255.f; c[2] += l[k] * (cc[k] >> 16 & 255) / 255.f; c[3] += l[k] * (cc[k] >> 24) / 255.f; }
            blend_px(px, py, c);
        }
    return true;
}
bool C2D_DrawRectSolid(float x, float y, float z, float w, float h, u32 clr) {
    g_quads++;
    if ((clr >> 24) != 0) add_fill(x, y, x + w, y + h);
    if (skipDraw) return true;
    (void)z;
    float c[4] = { (clr & 255) / 255.f, (clr >> 8 & 255) / 255.f, (clr >> 16 & 255) / 255.f, (clr >> 24) / 255.f };
    for (int py = (int)floorf(y); py < (int)ceilf(y + h); py++)
        for (int px = (int)floorf(x * XK); px < (int)ceilf((x + w) * XK); px++) blend_px(px, py, c);
    return true;
}

#include <malloc.h>
size_t host_heap_used(void) { struct mallinfo2 m = mallinfo2(); return m.uordblks + m.hblkhd; }
