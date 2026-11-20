// Secret masks (Hollow Knight's "unmasker" FSM objects): black covers over hidden areas that fade out (iTweenFadeTo,
// 0.5 s mostly) once the Knight stands in their trigger, and stay uncovered afterwards (PersistentBoolItem). Remaskers
// cover again when he leaves; inverse remaskers show only while he's inside. Some have no trigger of their own: a
// Breakable sends them HIT (hitEventReciever), e.g. the black over the passages behind King's Pass's breakable walls.
#include "masks.h"
#include "gfx.h"
#include "pd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { float x0, y0, x1, y1, alpha, fade, fadeT; float bx[3][4]; int nb; float poly[2][24][2]; int npt[2], np; long evt; bool pers, done, valid; int mode; char name[48]; } MGroup;   // mode 0 unmasker, 1 remasker, 2 inverse remasker, 3 always shown
typedef struct { int g, frame; float x, y, F, dx, dy, a; } MSprite;
static MGroup groups[32]; static int ngroups;
static MSprite sprites[256]; static int nsprites;
static SpriteSet mset; static float MS = 4;
static char roomId[48];

static void key_of(MGroup *g, char *buf, int n) { snprintf(buf, n, "msk:%s:%s", roomId, g->name); }

void masks_unload(void) {
    if (mset.loaded) spr_free(&mset);
    ngroups = nsprites = 0;
    memset(groups, 0, sizeof groups);
}

void masks_load(const char *room) {
    masks_unload();
    snprintf(roomId, sizeof roomId, "%s", room);
    char path[96]; snprintf(path, sizeof path, "romfs:/rooms/%s.msk", room);
    size_t sz; char *txt = read_file(path, &sz);
    if (!txt) return;
    char *sv, *ln = strtok_r(txt, "\n", &sv);
    while (ln) {
        if (ln[0] == 'M') sscanf(ln, "M %f", &MS);
        else if (ln[0] == 'G') {
            MGroup t; memset(&t, 0, sizeof t); int id, pers, mode = -1; float ft = 0.5f;
            int n = sscanf(ln, "G %d %f %f %f %f %d %47s %d %f", &id, &t.x0, &t.y0, &t.x1, &t.y1, &pers, t.name, &mode, &ft);
            if (n >= 7 && id >= 0 && id < 32) {
                MGroup *g = &groups[id]; *g = t;
                g->pers = pers; g->alpha = 1; g->fade = 0; g->done = false; g->valid = true; g->fadeT = ft > 0.05f ? ft : 0.5f;
                // Remasker: covers its area again whenever the Knight is outside it. Inverse remasker: the other way round,
                // a cover over the rest of the room shown only while he is inside (hidden by default). Older files: by name
                g->mode = mode >= 0 ? mode : strstr(g->name, "Inverse") ? 2 : strstr(g->name, "Remask") ? 1 : 0;
                if (g->mode == 2) { g->alpha = 0; g->pers = false; }
                if (g->mode == 3) g->pers = false;     // scenery: always shown
                if (g->mode == 1) g->pers = false;
                char k[96]; key_of(g, k, sizeof k);
                if (g->pers && pd_bool(k)) { g->alpha = 0; g->done = true; }
                if (id >= ngroups) ngroups = id + 1;
            }
        } else if (ln[0] == 'B') {        // another trigger collider of the group
            int id; float b[4];
            if (sscanf(ln, "B %d %f %f %f %f", &id, &b[0], &b[1], &b[2], &b[3]) == 5 && id >= 0 && id < 32 && groups[id].nb < 3)
                memcpy(groups[id].bx[groups[id].nb++], b, sizeof b);
        } else if (ln[0] == 'P') {        // a polygon trigger: "P id n x y x y ..."
            int id, n, off; char *q = ln + 1;
            if (sscanf(q, "%d %d%n", &id, &n, &off) == 2 && id >= 0 && id < 32 && groups[id].np < 2 && n >= 3 && n <= 24) {
                MGroup *g = &groups[id]; int k = g->np, got = 0; q += off;
                for (int j = 0; j < n; j++) { float x, y; int o2; if (sscanf(q, "%f %f%n", &x, &y, &o2) != 2) break; g->poly[k][j][0] = x; g->poly[k][j][1] = y; q += o2; got++; }
                if (got == n) { g->npt[k] = n; g->np++; }
            }
        } else if (ln[0] == 'E') {        // the scene object (GameObject fileID) a Breakable's HIT reaches
            int id; long fid;
            if (sscanf(ln, "E %d %ld", &id, &fid) == 2 && id >= 0 && id < 32) groups[id].evt = fid;
        } else if (ln[0] == 'S' && nsprites < 256) {
            MSprite *s = &sprites[nsprites];
            if (sscanf(ln, "S %d %d %f %f %f %f %f %f", &s->g, &s->frame, &s->x, &s->y, &s->F, &s->dx, &s->dy, &s->a) == 8) nsprites++;
        }
        ln = strtok_r(NULL, "\n", &sv);
    }
    free(txt);
    snprintf(path, sizeof path, "romfs:/rooms/%s_msk.spr", room);
    spr_load(&mset, path);
}

static bool in_poly(const float (*p)[2], int n, float x, float y) {
    bool c = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if (((p[i][1] > y) != (p[j][1] > y)) && x < (p[j][0] - p[i][0]) * (y - p[i][1]) / (p[j][1] - p[i][1]) + p[i][0]) c = !c;
    return c;
}
static bool inside(MGroup *g, float hx0, float hy0, float hx1, float hy1) {
    if (hx0 < g->x1 && hx1 > g->x0 && hy0 < g->y1 && hy1 > g->y0) return true;
    for (int k = 0; k < g->nb; k++) { float *b = g->bx[k]; if (hx0 < b[2] && hx1 > b[0] && hy0 < b[3] && hy1 > b[1]) return true; }
    for (int k = 0; k < g->np; k++) {     // the Knight's box touches the polygon: a corner or the centre inside, or a vertex in the box
        float cx = (hx0 + hx1) / 2, cy = (hy0 + hy1) / 2;
        if (in_poly(g->poly[k], g->npt[k], cx, cy) || in_poly(g->poly[k], g->npt[k], hx0, hy0) || in_poly(g->poly[k], g->npt[k], hx1, hy0) ||
            in_poly(g->poly[k], g->npt[k], hx0, hy1) || in_poly(g->poly[k], g->npt[k], hx1, hy1)) return true;
        for (int j = 0; j < g->npt[k]; j++) { float x = g->poly[k][j][0], y = g->poly[k][j][1]; if (x > hx0 && x < hx1 && y > hy0 && y < hy1) return true; }
    }
    return false;
}
static void uncover(MGroup *g) {
    g->done = true; g->fade = g->fadeT;
    if (g->pers) { char k[96]; key_of(g, k, sizeof k); pd_set_bool(k, true); }
}
void masks_hit(long fid) {
    for (int i = 0; i < ngroups; i++) { MGroup *g = &groups[i]; if (g->valid && g->evt == fid && !g->done && !g->mode) {
        uncover(g);
#ifdef __HOST__
        if (getenv("HK_FSMTRACE")) fprintf(stderr, "[mask] HIT uncovers %s\n", g->name);
#endif
    } }
}
void masks_update(float dt, float hx0, float hy0, float hx1, float hy1) {
    for (int i = 0; i < ngroups; i++) {
        MGroup *g = &groups[i];
        if (!g->valid || g->mode == 3) continue;
        if (g->mode) {      // remaskers fade towards their state for where the Knight is
            bool in = inside(g, hx0, hy0, hx1, hy1);
            float target = (g->mode == 2) == in ? 1.0f : 0.0f;
            if (g->alpha < target) { g->alpha += dt / g->fadeT; if (g->alpha > target) g->alpha = target; }
            else if (g->alpha > target) { g->alpha -= dt / g->fadeT; if (g->alpha < target) g->alpha = target; }
            continue;
        }
        if (!g->done && inside(g, hx0, hy0, hx1, hy1)) uncover(g);   // Trigger2dEvent (tag Player) -> UNCOVER
        if (g->fade > 0) { g->fade -= dt; g->alpha = g->fade > 0 ? g->fade / g->fadeT : 0; }   // iTweenFadeTo alpha 0, linear
    }
}

void masks_draw(float camx, float camy) { masks_draw_range(camx, camy, -1e9f, 1e9f); }
// the masks whose parallax F is in [flo, fhi): drawn among the layers and objects at their depth (a black shape behind
// the gameplay plane mustn't cover the Knight)
void masks_draw_range(float camx, float camy, float flo, float fhi) {
    if (!mset.loaded) return;
#ifdef __HOST__
    { static int off = -1; if (off < 0) off = getenv("HK_NOMASKS") != NULL; if (off) return; }
#endif
    for (int i = 0; i < nsprites; i++) {
        MSprite *s = &sprites[i];
        if (s->F < flo || s->F >= fhi) continue;
        if (s->g < 0 || s->g >= ngroups || !groups[s->g].valid) continue;
        float a = groups[s->g].alpha * s->a;
        if (a <= 0.01f) continue;
        float k = PPU / MS;
        float x = (s->x - camx) * s->F * PPU + TOP_W / 2 + s->dx * k;
        float y = (camy - s->y) * s->F * PPU + TOP_H / 2 + s->dy * k;
        const Frame *fr = &mset.frames[s->frame];
        if (x > TOP_W || y > TOP_H || x + fr->w * k < 0 || y + fr->h * k < 0) continue;
        spr_draw(&mset, s->frame, x, y, k, k, a);
#ifdef __HOST__
        { static int tr = -1, n; if (tr < 0) tr = getenv("HK_MASKTRACE") != NULL; if (tr && n++ < 4000) fprintf(stderr, "[maskdraw] %s mode %d sprite %d at %.0f,%.0f size %dx%d alpha %.2f\n", groups[s->g].name, groups[s->g].mode, i, x, y, (int)(fr->w * k), (int)(fr->h * k), a); }
#endif
    }
}
