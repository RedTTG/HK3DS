#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240
#define PPU   16.0f          // screen pixels per world unit on the gameplay plane

typedef struct {
    u16 page, x, y, w, h;
    s16 ox, oy;              // top-left of image relative to the pivot (px, y down)
    C2D_Image img;
    Tex3DS_SubTexture sub;
} Frame;

typedef struct {
    char name[32];
    float fps;
    u8 wrap, loopStart;
    u16 first, count;
} Anim;

enum { WRAP_LOOP = 0, WRAP_LOOPSECTION = 1, WRAP_ONCE = 2, WRAP_PINGPONG = 3, WRAP_RANDOMFRAME = 4, WRAP_RANDOMLOOP = 5, WRAP_SINGLE = 6 };

typedef struct {
    int npages, nframes, nanims, nnames;
    C3D_Tex *pages;
    Frame *frames;
    Anim *anims;
    u16 *animFrames, *animFlags;
    char (*names)[32];
    u16 *nameFrame;
    bool loaded;
} SpriteSet;

bool spr_load(SpriteSet *s, const char *path);
bool spr_load_meta(SpriteSet *s, const char *path);
bool spr_frame_alpha8(SpriteSet *s, int frame, const char *path, int w, int h);   // draw a frame from an A8 texture
extern bool g_lowmem;
void spr_free(SpriteSet *s);
int  spr_anim(const SpriteSet *s, const char *name);        // -1 if missing
int  spr_named(const SpriteSet *s, const char *name);       // frame index or -1
// draw a frame with its pivot at (x,y). sx<0 mirrors horizontally around the pivot.
void spr_draw(const SpriteSet *s, int frame, float x, float y, float sx, float sy, float alpha);
void spr_draw_ex(const SpriteSet *s, int frame, float x, float y, float sx, float sy, float angleDeg, float alpha, float flash);
void spr_draw_col(const SpriteSet *s, int frame, float x, float y, float sx, float sy, float angleDeg, float r, float g, float b, float alpha, float flash);
void spr_draw_fill(const SpriteSet *s, int frame, float x, float y, float sx, float sy, float frac, float alpha);   // bottom part only (liquid fill)
void gfx_additive(bool on);
void gfx_tint_mode(bool mult);      // citro2d tint: multiply (true) or blend towards the colour (false, the default)
void spr_draw_tint(const SpriteSet *s, int frame, float x, float y, float sx, float sy, u32 color, float blend);

// animation player
typedef struct {
    const SpriteSet *set;
    int anim;
    float t;
    int frame;          // current frame index within the anim
    bool done;
    bool stopped;       // tk2dSpriteAnimator.Stop: frozen on the current frame until the next Play
} AnimPlayer;
void anim_play(AnimPlayer *a, const SpriteSet *s, const char *name, bool restart);
void anim_play_idx(AnimPlayer *a, int idx, bool restart);
void anim_update(AnimPlayer *a, float dt);
int  anim_cur_frame(const AnimPlayer *a);     // global frame index into set
float anim_length(const AnimPlayer *a);
bool anim_is(const AnimPlayer *a, const char *name);

// text (pre-rendered glyph atlas)
typedef struct { SpriteSet set; int glyph[224]; int lineh; } Font;      // glyphs for bytes 32..255 (ASCII and the Latin-1 letters Spanish uses)
bool font_load(Font *f, const char *path);
float font_width(const Font *f, const char *txt);
float font_fit(const Font *f, const char *txt, float maxw, float scale);   // scale, smaller if the text would be wider than maxw
void font_draw(const Font *f, const char *txt, float x, float y, float scale, u32 color);   // color as RGBA via tint
void font_draw_centered(const Font *f, const char *txt, float cx, float y, float scale, u32 color);

void *read_file(const char *path, size_t *size);
void tex_upload(C3D_Tex *t, const void *data, size_t size);

// on-SD diagnostic log (sdmc:/3ds/hk3ds/log.txt): boot memory, every room load, allocation failures
void hk_log(const char *fmt, ...);
