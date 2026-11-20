#include "uo.h"
#include "particles.h"
#include <ctype.h>
#include "fsm.h"
#include "dialogue.h"
#include "menus.h"
#include "shop.h"
#include "lang.h"
#include "game.h"
#include "comps.h"
#include "comps2.h"
#include "world.h"
#include "hero.h"
#include "pd.h"
#include "audio.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define PHYS_G 60.0f

int uo_frame; float uo_time; float uo_ambient[3] = { 1, 1, 1 };
SpriteSet *uo_spriteset;               // the room bank's sprites (shops / item messages use it directly)
static UObj objs[UO_MAX_OBJS];
static int nobjs;                       // high-water mark
static bool loaded;

typedef struct { const char *key; JVal *objects, *fsms; const char *name; } Tmpl;
typedef struct { const char *guid; char key[12]; } Lib;
// asset banks: 0 = the current room's objects / prefabs / sprites, 1 = the Knight's (loaded once, kept across rooms)
#define NBANK 3                         // 2 = the room's additive scene (boss scenes...)
#define TMPL_BANK 1000                  // template index = bank * TMPL_BANK + index in the bank
typedef struct { JArena arena; JVal *root; Tmpl tmpls[160]; int ntmpl; Lib libs[64]; int nlibs; SpriteSet set; JVal *tkbox, *tkpoly, *tksid; bool loaded; } Bank;
static Bank banks[NBANK];
static int curBank;                     // bank of the object whose FSMs / components are running (prefab lookups)
static bool persistReady;
UObj *uo_door_fsm(const char *name) {      // a live door object running its Door Control FSM
    for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i]; if (!o->used || o->destroyed || strcmp(o->name, name)) continue;
        for (int k = 0; k < o->nfsm; k++) if (!strcmp(o->fsm[k]->name, "Door Control")) return o; }
    return NULL;
}
bool uo_has_knight(void) { return persistReady && banks[1].loaded; }
const SpriteSet *uo_set_of(const UObj *o) { const Bank *b = &banks[o ? o->bank : 0]; return b->set.loaded ? &b->set : NULL; }

static char roomName[48];
static UObj *heroObj, *heroBoxObj, *slashObj;
static bool heroSynced; static float hsx, hsy, hvx, hvy, hscale;   // hero stand-in values at the last sync
static int lastHeroClip = -1; static bool lastHeroDone; static int heroReqClip = -1;   // clip an FSM asked the Knight to play
static UObj terrainObj;                 // stands in for the room's terrain colliders in trigger callbacks
const char *uo_room_name(void) { return roomName; }
UObj *uo_hero(void) { return heroObj; }
static UObj *knight_child(const char *name) {
    if (!heroObj || !heroObj->treeArr) return NULL;
    for (int i = 0; i < heroObj->treeN; i++) { UObj *q = heroObj->treeArr[i]; if (q && !q->destroyed && !strcmp(q->name, name)) return q; }
    return NULL;
}
bool uo_has_vignette(void) { UObj *v = knight_child("Vignette"); return v && fsm_find_on(v, "Darkness Control"); }
void uo_set_darkness(int level) {
    UObj *v = knight_child("Vignette"); Fsm *f = v ? fsm_find_on(v, "Darkness Control") : NULL; if (!f) return;
    FVar *d = fsm_var(f, "Darkness Level"); if (d) d->i = level;
    fsm_event(f, "RESET");          // SceneManager.Start (a scene with noLantern sends SCENE RESET NO LANTERN)
    static bool smooth;             // its gradient from an 8-bit alpha texture (16-bit sprite pages band it)
    if (!smooth && v->sr.frame >= 0) { SpriteSet *ss = (SpriteSet *)uo_set_of(v); bool ok = ss && spr_frame_alpha8(ss, v->sr.frame, "romfs:/gfx/vignette.a8", 256, 170); hk_log("vignette frame %d: %s", v->sr.frame, ok ? "8-bit alpha" : "sheet"); smooth = true; }
}
void hero_sharp_shadow(bool on) {
    static UObj *ss; static int lookedFrame = -1;
    if (!heroObj) return;
    if (!ss || ss->destroyed || !ss->used) { if (lookedFrame == uo_frame && !ss) return; lookedFrame = uo_frame; ss = NULL;
        for (int i = 0; i < heroObj->treeN && heroObj->treeArr; i++) { UObj *q = heroObj->treeArr[i]; if (q && !strcmp(q->name, "Sharp Shadow")) { ss = q; break; } } }
    if (ss) uo_set_active(ss, on);
}
int uo_play_at(UObj *o, const char *clip, float vol, float pitch) {
    if (!clip || !clip[0] || vol <= 0) return -1;
    if (o && !o->hud && !o->persist) {
        float dx = uo_wx(o) - uo_hero_x(), dy = uo_wy(o) - uo_hero_y(), d = sqrtf(dx * dx + dy * dy);
        if (d > 32) return -1;
        if (d > 14) vol *= (32 - d) / 18;
    }
    return sfx_play_pitch(clip, vol, pitch > 0 ? pitch : 1);
}
int uo_source_play(UObj *o) {
    if (!o || !o->as.has || !o->as.clip) return -1;
    if (o->as.loop) { o->as.on = true; return -1; }       // uo_update_loops plays it
    if (o->as.h >= 0) sfx_stop(o->as.h);
    o->as.h = uo_play_at(o, o->as.clip, o->as.vol, o->as.pitch);
    return o->as.h;
}
void uo_source_stop(UObj *o) { if (o && o->as.loop) o->as.on = false; if (o && o->as.h >= 0) { sfx_stop(o->as.h); o->as.h = -1; } }
/* Looping AudioSources (Myla's humming, machinery, water, lifts...). Unity's listener is on the camera, CAM_D in front
   of the gameplay plane, so even a sound right there is ~38 units away: their min/max distances are set for that.
   Rolloff: 0 logarithmic (min/d), 1 linear, 2 custom curve (approximated as linear from min to max). */
#define LOOP_SLOTS 4
static float g_camx, g_camy;     // camera centre (set while drawing)
static UObj *loopObj[LOOP_SLOTS];
static int loopSlot[LOOP_SLOTS] = { -1, -1, -1, -1 };
static float loop_gain(UObj *o) {
    UAs *a = &o->as; float v = a->vol;
    if (a->sb < 0.5f) return v;                     // 2D: same volume anywhere
    uo_world(o);
    float z = 0; for (UObj *q = o; q; q = q->parent) z += q->lz;
    float dx = o->wx - g_camx, dy = o->wy - g_camy, dz = 38.1f + z;
    float d = sqrtf(dx * dx + dy * dy + dz * dz), mn = a->min > 0.01f ? a->min : 1, mx = a->max > mn ? a->max : mn + 1;
    if (a->roll == 0) { if (d > mx) d = mx; return d <= mn ? v : v * mn / d; }
    if (d <= mn) return v;
    if (d >= mx) return 0;
    return v * (1 - (d - mn) / (mx - mn));
}
static void loops_stop_all(void) {
    for (int i = 0; i < LOOP_SLOTS; i++) { if (loopSlot[i] >= 0) sfx_loop_stop(loopSlot[i]); loopSlot[i] = -1; if (loopObj[i]) loopObj[i]->as.slot = -1; loopObj[i] = NULL; }
}
void uo_update_loops(void) {
    if (!loaded) { loops_stop_all(); return; }
    UObj *best[LOOP_SLOTS] = {0}; float bv[LOOP_SLOTS] = {0};
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || !o->as.has || !o->as.loop || !o->as.on || !o->as.en || !o->as.clip || !uo_active(o)) continue;
        float v = loop_gain(o); if (v < 0.02f) continue;
        for (int k = 0; k < LOOP_SLOTS; k++) if (!best[k] || v > bv[k]) {
            for (int m = LOOP_SLOTS - 1; m > k; m--) { best[m] = best[m - 1]; bv[m] = bv[m - 1]; }
            best[k] = o; bv[k] = v; break;
        }
    }
    // stop the ones that dropped out (or that the system stopped)
    for (int i = 0; i < LOOP_SLOTS; i++) {
        if (!loopObj[i]) continue;
        bool keep = false; for (int k = 0; k < LOOP_SLOTS; k++) if (best[k] == loopObj[i]) keep = true;
        if (!keep || !sfx_loop_playing(loopSlot[i])) { sfx_loop_stop(loopSlot[i]); loopObj[i]->as.slot = -1; loopObj[i] = NULL; loopSlot[i] = -1; }
    }
    for (int k = 0; k < LOOP_SLOTS && best[k]; k++) {
        int i = 0; while (i < LOOP_SLOTS && loopObj[i] != best[k]) i++;
        if (i == LOOP_SLOTS) {          // start it
            int f = 0; while (f < LOOP_SLOTS && loopObj[f]) f++;
            if (f == LOOP_SLOTS) break;
            int h = sfx_loop_start(best[k]->as.clip, bv[k], best[k]->as.pitch);
            if (h < 0) continue;
            loopObj[f] = best[k]; loopSlot[f] = h; best[k]->as.slot = f;
#ifdef __HOST__
            if (getenv("HK_LOOPDBG")) fprintf(stderr, "[loop] start %s on %s vol %.2f\n", best[k]->as.clip, best[k]->name, bv[k]);
#endif
        } else sfx_loop_volume(loopSlot[i], bv[k]);
    }
}

/* ---------------- PlayMaker globals ---------------- */
static const char *GLOBAL_GOS[] = { "GameManager", "DialogueManager", "DialogueText", "DialogueTextYN", "DialogueBox", "AreaTitle",
    "HUD Canvas", "HUD Camera", "MainCamera", "CameraParent", "AudioManager", "Inventory", "UIManager", "Enemy Dream Msg",
    "Orbit Shield", "Geo Counter", "Charm Icons", "Game Map", "Quick Map", "MenuStagMap", "Orb Target", "Atmos", "Track 1",
    "Acid Armour", "2dtk Blanker", "Blocker HUD", "HUD Blanker", "HUD Blanker White", "MenuHolder" };
#define NGLOBAL (int)(sizeof GLOBAL_GOS / sizeof *GLOBAL_GOS)
static UObj *globalObj[NGLOBAL];
UObj *uo_global(const char *name) {
    if (!strcmp(name, "Hero")) return heroObj;
    for (int i = 0; i < NGLOBAL; i++) if (!strcmp(GLOBAL_GOS[i], name)) return globalObj[i];
    return NULL;
}
static void global_native(UObj *o, const char *ev) {
    if (o == uo_global("DialogueManager")) dlg_box_event(ev);
    else if (o == uo_global("DialogueText")) dlg_text_event(0, ev);
    else if (o == uo_global("DialogueTextYN")) dlg_text_event(1, ev);
    else if (o == uo_global("MenuHolder")) {
        if (!strcmp(ev, "OPEN STAG MENU")) { Fsm *f = fsm_find_on(o, "Open Stag"); FVar *r = f ? fsm_var(f, "Requester") : NULL; stag_menu_open(r ? r->go : NULL); }
    }
    else if (o == uo_global("MainCamera")) {
        if (!strcmp(ev, "FADE OUT") || !strcmp(ev, "FADE OUT INSTANT") || !strcmp(ev, "FADE SCENE OUT INSTANT")) game_camera_fade(true, strstr(ev, "INSTANT") != NULL);
        else if (!strcmp(ev, "FADE IN") || !strcmp(ev, "FADE SCENE IN")) game_camera_fade(false, false);
    }
    else if (o == uo_global("Enemy Dream Msg")) {
        if (!strcmp(ev, "DISPLAY ENEMY DREAM")) { Fsm *f = fsm_find_on(o, "Display"); FVar *t = f ? fsm_var(f, "Convo Title") : NULL, *n = f ? fsm_var(f, "Convo Amount") : NULL; if (t) dlg_dream_msg(t->s, n ? n->i : 1); }
    }
    else if (o == uo_global("HUD Canvas")) { if (!strcmp(ev, "OUT")) game_hud_out(true); else if (!strcmp(ev, "IN")) game_hud_out(false); }
    else if (o == uo_global("AreaTitle")) {
        if (!strcmp(ev, "__ENABLE")) { Fsm *f = o->nfsm ? o->fsm[0] : NULL; if (f) title_npc(f); }
        else if (!strcmp(ev, "NPC TITLE DOWN")) { title_event(ev); o->activeSelf = false; Fsm *f = o->nfsm ? o->fsm[0] : NULL; FVar *v = f ? fsm_var(f, "NPC Title") : NULL; if (v) v->b = false; }
    }
}
void uo_native_event(UObj *o, const char *ev) { if (o && o->native && ev) o->native(o, ev); }
UObj *uo_alloc_pseudo(const char *name);
static Fsm *add_var_fsm(UObj *o, const char *fsmName, const char **names, const VType *types, int n) {
    Fsm *f = fsm_create_vars(fsmName, o, names, types, n);
    o->fsm = realloc(o->fsm, (o->nfsm + 1) * sizeof(Fsm *)); o->fsm[o->nfsm++] = f;
    return f;
}
static void make_globals(void) {
    for (int i = 0; i < NGLOBAL; i++) {
        UObj *o = uo_alloc_pseudo(GLOBAL_GOS[i]);
        globalObj[i] = o;
        if (o) o->native = global_native;
    }
    UObj *dt = uo_global("DialogueText"), *dy = uo_global("DialogueTextYN"), *at = uo_global("AreaTitle");
    static const char *n1[] = { "Use Stop", "Stop Pause" }; static const VType t1[] = { V_BOOL, V_FLOAT };
    static const char *n2[] = { "Toll Cost", "Requester", "Toll Cost String" }; static const VType t2[] = { V_INT, V_GO, V_STRING };
    static const char *n3[] = { "NPC Title", "Area Event", "Visited", "Display Right" }; static const VType t3[] = { V_BOOL, V_STRING, V_BOOL, V_BOOL };
    Fsm *fn = dt ? add_var_fsm(dt, "Dialogue Page Control", n1, t1, 2) : NULL;
    if (fn) fsm_var(fn, "Use Stop")->b = true;
    Fsm *fy = dy ? add_var_fsm(dy, "Dialogue Page Control", n2, t2, 3) : NULL;
    if (at) { add_var_fsm(at, "Area Title Control", n3, t3, 4); at->activeSelf = false; }
    // the nail's damages_enemy FSM (ReceivedDamage receivers read damageDealt / direction from it)
    static const char *n5[] = { "damageDealt", "direction", "attackType", "magnitudeMult", "moveDirection", "circleDirection" };
    static const VType t5[] = { V_INT, V_FLOAT, V_INT, V_FLOAT, V_BOOL, V_BOOL };
    if (slashObj) { Fsm *df = add_var_fsm(slashObj, "damages_enemy", n5, t5, 6); fsm_var(df, "damageDealt")->i = 5; fsm_var(df, "magnitudeMult")->f = 1; }
    UObj *dm = uo_global("Enemy Dream Msg");
    static const char *n6[] = { "Convo Amount", "Convo Title" }; static const VType t6[] = { V_INT, V_STRING };
    if (dm) add_var_fsm(dm, "Display", n6, t6, 2);
    UObj *mh = uo_global("MenuHolder");
    static const char *n4[] = { "Requester" }; static const VType t4[] = { V_GO };
    if (mh) add_var_fsm(mh, "Open Stag", n4, t4, 1);
    dlg_set_vars(fn, fy);
    fsm_globals_bind_go("Hero", heroObj);
    for (int i = 0; i < NGLOBAL; i++) fsm_globals_bind_go(GLOBAL_GOS[i], globalObj[i]);
}
UObj *uo_hero_slash(void) { return slashObj; }
bool uo_room_active(void) { return loaded; }
UObj *uo_obj(int i) { return (i >= 0 && i < nobjs && objs[i].used) ? &objs[i] : NULL; }
int uo_count(void) { return nobjs; }
static const char *lib_key_b(int bank, int lib) { Bank *b = &banks[bank]; return lib >= 0 && lib < b->nlibs ? b->libs[lib].key : ""; }
const char *uo_lib_key(int lib) { return lib_key_b(0, lib); }

/* ---------------- transforms ---------------- */
void uo_world(UObj *o) {
    if (!o->parent) { o->wx = o->lx; o->wy = o->ly; o->wsx = o->sx; o->wsy = o->sy; o->wrz = o->rz; return; }
    UObj *p = o->parent; uo_world(p);
    float a = p->wrz * (float)M_PI / 180.0f, ca = cosf(a), sa = sinf(a);
    float lx = o->lx * p->wsx, ly = o->ly * p->wsy;
    o->wx = p->wx + ca * lx - sa * ly;
    o->wy = p->wy + sa * lx + ca * ly;
    o->wsx = p->wsx * o->sx; o->wsy = p->wsy * o->sy;
    o->wrz = p->wrz + ((p->wsx * p->wsy < 0) ? -o->rz : o->rz);
}
float uo_wx(UObj *o) { uo_world(o); return o->wx; }
float uo_wy(UObj *o) { uo_world(o); return o->wy; }
float uo_wsx(UObj *o) { uo_world(o); return o->wsx; }
float uo_wsy(UObj *o) { uo_world(o); return o->wsy; }
float uo_wrz(UObj *o) { uo_world(o); return o->wrz; }
void uo_set_world_pos(UObj *o, float x, float y) {
    if (!o->parent) { o->lx = x; o->ly = y; return; }
    UObj *p = o->parent; uo_world(p);
    float a = -p->wrz * (float)M_PI / 180.0f, ca = cosf(a), sa = sinf(a);
    float dx = x - p->wx, dy = y - p->wy;
    float lx = ca * dx - sa * dy, ly = sa * dx + ca * dy;
    o->lx = p->wsx != 0 ? lx / p->wsx : lx; o->ly = p->wsy != 0 ? ly / p->wsy : ly;
}
static void xform(UObj *o, float px, float py, float *ox, float *oy) {
    uo_world(o);
    float a = o->wrz * (float)M_PI / 180.0f, ca = cosf(a), sa = sinf(a);
    float lx = px * o->wsx, ly = py * o->wsy;
    *ox = o->wx + ca * lx - sa * ly; *oy = o->wy + sa * lx + ca * ly;
}

bool uo_active(const UObj *o) {
    for (; o; o = o->parent) if (!o->activeSelf || o->destroyed) return false;
    return true;
}
UObj *uo_root(UObj *o) { while (o && o->parent) o = o->parent; return o; }

UObj *uo_find_child(UObj *o, const char *name) {
    if (!o) return NULL;
    for (UObj *c = o->child; c; c = c->next) if (!c->destroyed && !strcmp(c->name, name)) return c;
    return NULL;
}
UObj *uo_find(const char *name) {
    for (int i = 0; i < nobjs; i++) if (objs[i].used && !objs[i].destroyed && !strcmp(objs[i].name, name)) return &objs[i];
    // Unity names instantiated objects "Name(Clone)" (Elder Baldur counts its "Spawn Roller v2(Clone)"); ours keep the
    // prefab's name, so look for a spawned root of that name
    size_t n = strlen(name);
    if (n > 7 && !strcmp(name + n - 7, "(Clone)")) {
        for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i];
            if (o->used && !o->destroyed && o->spawned && !o->parent && strlen(o->name) == n - 7 && !strncmp(o->name, name, n - 7)) return o; }
    }
    return NULL;
}
UComp *uo_comp(UObj *o, CompType t) {
    if (!o) return NULL;
    for (int i = 0; i < o->ncomp; i++) if (o->comp[i].type == t) return &o->comp[i];
    return NULL;
}

/* ---------------- colliders ---------------- */
static const Anim *clipp(UObj *o);
// tk2dBaseSprite.UpdateCollider: a sprite whose frames carry box collider data sets its BoxCollider2D to the current
// frame's box (the Goam's grows as it bursts out of the ground). Only the object's first box collider follows it.
static JVal *tk_entry(UObj *o, JVal *tk, int *cClip, void **cRow) {      // this frame's entry in a per-clip table
    if (!tk || !o->an.has || o->an.clip < 0 || o->pseudo) return NULL;
    const Anim *a = clipp(o); if (!a) return NULL;
    if (*cClip != o->an.clip + 1) {
        *cClip = o->an.clip + 1; *cRow = NULL;
        for (JVal *r = tk->child; r; r = r->next) if (r->key && !strncmp(r->key, a->name, 31) && (strlen(r->key) >= 31 || !a->name[strlen(r->key)])) { *cRow = r; break; }
    }
    return *cRow ? jat((JVal *)*cRow, o->an.frame) : NULL;
}
static UCol *tk_col(UObj *o, int type) {     // the collider tk2d drives: the object's first box (or polygon) collider
    for (int i = 0; i < o->ncol; i++) if (o->col[i].t == type && !o->col[i].sprBox) return &o->col[i];
    return NULL;
}
static bool tk_frame_box(UObj *o, const UCol *c, float *out) {
    if (tk_col(o, COL_BOX) != c) return false;
    JVal *e = tk_entry(o, banks[o->bank].tkbox, &o->an.tkClip, &o->an.tkRow);
    if (!e || e->t != J_ARR) return false;
    for (int k = 0; k < 4; k++) { JVal *v = jat(e, k); out[k] = v ? (float)jnum(v, 0) : 0; }
    return true;
}
static bool tk_frame_poly(UObj *o, const UCol *c, float *out) {     // bounds of this frame's mesh outline
#ifdef HK_OLD_TK
    return false;
#endif
    if (tk_col(o, COL_POLY) != c) return false;
    JVal *e = tk_entry(o, banks[o->bank].tkpoly, &o->an.tkpClip, &o->an.tkpRow);
    if (!e || e->t != J_ARR) return false;
    for (int k = 0; k < 4; k++) { JVal *v = jat(e, k); out[k] = v ? (float)jnum(v, 0) : 0; }
    return true;
}
// ...and on every sprite change tk2d switches that collider on for a frame with a collider of its kind and off for a
// frame with none (Physics2D). Attack hitboxes rely on it: the False Knight's swing turns its Hitter's collider off and
// plays the "Attack" clip, whose frames turn it back on; the Husk Guard's club swipe has no collider on its wind-up
// frame. Only for trigger colliders (hitboxes): bodies keep what their scripts set.
static void tk_sprite_changed(UObj *o) {
#ifdef HK_OLD_TK
    return;      /* comparison builds: the collider tables only size boxes */
#endif
    if (!o->an.has || o->an.clip < 0 || o->pseudo) return;
    UCol *c = banks[o->bank].tkbox ? tk_col(o, COL_BOX) : NULL;
    if (c && c->trig) { JVal *e = tk_entry(o, banks[o->bank].tkbox, &o->an.tkClip, &o->an.tkRow);
        if (e && e->t == J_ARR) c->en = true; else if (e && jnum(e, 0) == 1) c->en = false; }
    c = banks[o->bank].tkpoly ? tk_col(o, COL_POLY) : NULL;
    if (c && c->trig) { JVal *e = tk_entry(o, banks[o->bank].tkpoly, &o->an.tkpClip, &o->an.tkpRow);
        if (e && e->t == J_ARR) c->en = true; else if (e && jnum(e, 0) == 1) c->en = false; }
}
bool uo_col_aabb(UObj *o, const UCol *c, float *x0, float *y0, float *x1, float *y1) {
    uo_world(o);
    if (c->sprBox) {
        const Anim *a = clipp(o); if (!a || a->count == 0 || (o->mrSet && !o->mr)) return false;
        const SpriteSet *ss = uo_set_of(o); int fr = o->an.frame < a->count ? o->an.frame : a->count - 1;
        int gf = ss->animFrames[a->first + fr]; if (gf < 0 || gf >= ss->nframes) return false;
        const Frame *f = &ss->frames[gf];
        float sx = fabsf(o->wsx), sy = fabsf(o->wsy), w = f->w * sx / PPU, h = f->h * sy / PPU;
        float left = o->wsx >= 0 ? o->wx + f->ox * sx / PPU : o->wx - (f->ox + f->w) * sx / PPU, top = o->wy - f->oy * sy / PPU;
        *x0 = left + w * 0.15f; *x1 = left + w * 0.85f; *y1 = top - h * 0.05f; *y0 = top - h * 0.95f;   // a bit inside the drawn sprite
        return w > 0 && h > 0;
    }
    if (c->t == COL_BOX || c->t == COL_CIRCLE) {
        float tb[4];
        if (c->t == COL_BOX && tk_frame_box(o, c, tb)) {   // tk2d resizes the sprite's BoxCollider2D to each frame's box
            UCol t = *c; t.ox = tb[0]; t.oy = tb[1]; t.w = 2 * tb[2]; t.h = 2 * tb[3]; c = &t;
            float cx, cy; xform(o, c->ox, c->oy, &cx, &cy);
            float hw = fabsf(c->w * o->wsx) / 2, hh = fabsf(c->h * o->wsy) / 2;
            if (fabsf(o->wrz) > 1) { float a = o->wrz * (float)M_PI / 180.0f, ca = fabsf(cosf(a)), sa = fabsf(sinf(a)); float nw = hw * ca + hh * sa, nh = hw * sa + hh * ca; hw = nw; hh = nh; }
            *x0 = cx - hw; *x1 = cx + hw; *y0 = cy - hh; *y1 = cy + hh;
            return true;
        }
        float cx, cy; xform(o, c->ox, c->oy, &cx, &cy);
        float hw, hh;
        if (c->t == COL_BOX) { hw = fabsf(c->w * o->wsx) / 2; hh = fabsf(c->h * o->wsy) / 2; }
        else { float m = fmaxf(fabsf(o->wsx), fabsf(o->wsy)); hw = hh = c->r * m; }
        if (fabsf(o->wrz) > 1 && c->t == COL_BOX) {   // rotated box: AABB of the corners
            float a = o->wrz * (float)M_PI / 180.0f, ca = fabsf(cosf(a)), sa = fabsf(sinf(a));
            float nw = hw * ca + hh * sa, nh = hw * sa + hh * ca; hw = nw; hh = nh;
        }
        *x0 = cx - hw; *x1 = cx + hw; *y0 = cy - hh; *y1 = cy + hh;
        return true;
    }
    if (c->npts <= 0) return false;
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
    { float tb[4];      // tk2d swaps the outline each frame: the bounds of this frame's one
      if (tk_frame_poly(o, c, tb)) {
          float px[4] = { tb[0], tb[2], tb[0], tb[2] }, py[4] = { tb[1], tb[1], tb[3], tb[3] };
          for (int i = 0; i < 4; i++) { float wx, wy; xform(o, px[i], py[i], &wx, &wy);
              if (wx < mnx) mnx = wx; if (wx > mxx) mxx = wx; if (wy < mny) mny = wy; if (wy > mxy) mxy = wy; }
          *x0 = mnx; *y0 = mny; *x1 = mxx; *y1 = mxy; return true;
      } }
    for (int i = 0; i < c->npts; i++) {
        float wx, wy; xform(o, c->pts[i * 2], c->pts[i * 2 + 1], &wx, &wy);
        if (wx < mnx) mnx = wx; if (wx > mxx) mxx = wx; if (wy < mny) mny = wy; if (wy > mxy) mxy = wy;
    }
    *x0 = mnx; *y0 = mny; *x1 = mxx; *y1 = mxy;
    return true;
}
static bool seg_box(float ax, float ay, float bx, float by, float x0, float y0, float x1, float y1) {   // Liang-Barsky
    float t0 = 0, t1 = 1, p[4] = { ax - bx, bx - ax, ay - by, by - ay }, q[4] = { ax - x0, x1 - ax, ay - y0, y1 - ay };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) { if (q[i] < 0) return false; continue; }
        float r = q[i] / p[i];
        if (p[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
        else { if (r < t0) return false; if (r < t1) t1 = r; }
    }
    return true;
}
bool uo_col_overlap_box(UObj *o, const UCol *c, float x0, float y0, float x1, float y1) {
    float a, b, cc, d;
    if (!c->en || !uo_col_aabb(o, c, &a, &b, &cc, &d)) return false;
    if (!(x0 < cc && x1 > a && y0 < d && y1 > b)) return false;
    // polygons and edges: their own outline, not its bounding box (Greenpath's thorn vines are long diagonal polygons
    // whose bounds cover the gaps you jump through). Animated outlines (tk2d's per-frame mesh) keep their frame bounds.
    if ((c->t != COL_POLY && c->t != COL_EDGE) || c->npts < 2 || c->sprBox) return true;
    { float tb[4]; if (tk_frame_poly(o, c, tb)) return true; }
    float px = 0, py = 0, fx = 0, fy = 0, mx = (x0 + x1) / 2, my = (y0 + y1) / 2; bool inside = false;
    int n = c->npts, segs = c->t == COL_POLY ? n : n - 1;
    xform(o, c->pts[0], c->pts[1], &fx, &fy); px = fx; py = fy;
    for (int i = 1; i <= segs; i++) {
        float qx, qy;
        if (i < n) xform(o, c->pts[i * 2], c->pts[i * 2 + 1], &qx, &qy); else { qx = fx; qy = fy; }
        if (seg_box(px, py, qx, qy, x0, y0, x1, y1)) return true;
        if ((py > my) != (qy > my) && mx < px + (my - py) * (qx - px) / (qy - py)) inside = !inside;   // box wholly inside
        px = qx; py = qy;
    }
    return c->t == COL_POLY && inside;
}
#ifdef __HOST__
void uo_polytest(void) {      // HK_POLYTEST=name: a box at the object's polygon's centre, its bounds' corners, and its points
    const char *nm = getenv("HK_POLYTEST"); if (!nm) return;
    for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i]; if (!o->used || o->destroyed || strcmp(o->name, nm) || !uo_active(o)) continue;
        for (int k = 0; k < o->ncol; k++) { UCol *c = &o->col[k]; if (c->t != COL_POLY) continue;
            float a, b, cc, d; uo_col_aabb(o, c, &a, &b, &cc, &d);
            float mx = (a + cc) / 2, my = (b + d) / 2;
            fprintf(stderr, "[poly %d] %s aabb %.2f,%.2f..%.2f,%.2f centre box %d corner boxes %d %d %d %d pts:", uo_frame, o->name, a, b, cc, d,
                uo_col_overlap_box(o, c, mx - 0.3f, my - 0.3f, mx + 0.3f, my + 0.3f),
                uo_col_overlap_box(o, c, a, b, a + 0.2f, b + 0.2f), uo_col_overlap_box(o, c, cc - 0.2f, b, cc, b + 0.2f),
                uo_col_overlap_box(o, c, a, d - 0.2f, a + 0.2f, d), uo_col_overlap_box(o, c, cc - 0.2f, d - 0.2f, cc, d));
            for (int q = 0; q < c->npts; q++) { float wx, wy; xform(o, c->pts[q * 2], c->pts[q * 2 + 1], &wx, &wy); fprintf(stderr, " (%.2f,%.2f)", wx, wy); }
            fprintf(stderr, " sx %.2f sy %.2f rz %.1f\n", o->wsx, o->wsy, o->wrz); } }
}
#endif
bool uo_body_aabb(UObj *o, float *x0, float *y0, float *x1, float *y1) {
    for (int i = 0; i < o->ncol; i++) if (!o->col[i].trig && o->col[i].en) return uo_col_aabb(o, &o->col[i], x0, y0, x1, y1);
    for (int i = 0; i < o->ncol; i++) if (o->col[i].en) return uo_col_aabb(o, &o->col[i], x0, y0, x1, y1);
    return false;
}

/* ---------------- sprite animator (tk2dSpriteAnimator) ---------------- */
#define SETOF(o) ((SpriteSet *)uo_set_of(o))
int uo_anim_clip(UObj *o, const char *clip) {
    SpriteSet *ss = SETOF(o);
    if (!o->an.has || !clip || !ss) return -1;
    char nm[72]; snprintf(nm, sizeof nm, "%s:%s", lib_key_b(o->bank, o->an.lib), clip); nm[31] = 0;
    for (int n = (int)strlen(nm); n > 0 && (nm[n - 1] == '\n' || nm[n - 1] == '\r' || nm[n - 1] == ' '); n--) nm[n - 1] = 0;   // "Slash Impact 1\n" in a script
    return spr_anim(ss, nm);
}
static const Anim *clipp(UObj *o) { SpriteSet *ss = SETOF(o); return (o->an.clip >= 0 && ss && o->an.clip < ss->nanims) ? &ss->anims[o->an.clip] : NULL; }
static bool frame_trig(UObj *o, int fr) {
    const Anim *a = clipp(o); if (!a || fr < 0 || fr >= a->count) return false;
    SpriteSet *ss = SETOF(o);
    return ss->animFlags && (ss->animFlags[a->first + fr] & 1);
}
static void anim_trigger(UObj *o, int fr);
static void anim_complete(UObj *o);
static void warp(UObj *o, float t) {
    const Anim *a = clipp(o); if (!a || a->count == 0) return;
    o->an.clipTime = t;
    int f = ((int)t) % a->count;
    o->an.frame = f;
    tk_sprite_changed(o);
    if (frame_trig(o, f)) anim_trigger(o, f);
    o->an.prevFrame = f;
}
static void anim_play_idx_(UObj *o, int idx, float startTime, bool restartIfSame) {
    if (idx < 0) { o->an.playing = false; anim_complete(o); return; }
    const Anim *a = &SETOF(o)->anims[idx];
    if (a->count == 0) { anim_complete(o); o->an.playing = false; return; }
    if (startTime == 0 && !restartIfSame && o->an.playing && o->an.clip == idx) return;   // tk2d: already playing
    o->an.playing = true; o->an.clip = idx; o->an.fps = a->fps;
    if (a->wrap == WRAP_SINGLE) { warp(o, 0); o->an.playing = false; }
    else if (a->wrap == WRAP_RANDOMFRAME || a->wrap == WRAP_RANDOMLOOP) { warp(o, (float)(rand() % a->count)); if (a->wrap == WRAP_RANDOMFRAME) { o->an.prevFrame = -1; o->an.playing = false; } }
    else {
        float t = startTime * a->fps;
        if (a->wrap == WRAP_ONCE && t >= a->count) { warp(o, (float)(a->count - 1)); o->an.playing = false; }
        else warp(o, t);
    }
}
// Tk2dSpriteSetId on a sprite with no animator: show another frame of its collection (by id, or by name via "tkname")
bool uo_sprite_set(UObj *o, int id, const char *name) {
    if (!o || !o->sr.has || !o->sr.tk || !SETOF(o)) return false;
    if (name && name[0]) {
        JVal *t = jget(banks[o->bank].root, "tkname"); char k[72]; snprintf(k, sizeof k, "%s:%s", o->sr.coll, name);
        JVal *v = t ? jget(t, k) : NULL; if (!v) return false;
        id = (int)jnum(v, 0);
    }
    char nm[40]; snprintf(nm, sizeof nm, "tk:%s:%d", o->sr.coll, id);
    int fr = spr_named(SETOF(o), nm); if (fr < 0) return false;
    o->sr.frame = fr; return true;
}
// tk2dBaseSprite.spriteId: the current frame's index in its sprite collection (Tk2dSpriteGetId); the clip frame if unknown
int uo_sprite_id(UObj *o) {
    if (!o || !o->an.has || o->an.clip < 0) return o ? o->an.frame : 0;
    JVal *t = banks[o->bank].tksid; const Anim *a = clipp(o);
    if (t && a) for (JVal *r = t->child; r; r = r->next)
        if (r->key && !strncmp(r->key, a->name, 31) && (strlen(r->key) >= 31 || !a->name[strlen(r->key)])) { JVal *e = jat(r, o->an.frame); return e ? (int)jnum(e, 0) : o->an.frame; }
    return o->an.frame;
}
void uo_anim_play(UObj *o, const char *clip, bool restartIfSame) {
    if (o == heroObj) { hero_anim_play(clip, restartIfSame); lastHeroDone = false; heroReqClip = clip && spr_anim(&A.knight, clip) >= 0 ? P.anim.anim : 9999; return; }   // 9999: missing clip, finishes next frame
    int idx = uo_anim_clip(o, clip);
#ifdef __HOST__
    if (idx < 0 && clip && clip[0] && o->an.has && getenv("HK_CHECK")) {   // sweep: a clip the room's sprites don't have
        static char seen[64][64]; static int ns; char k[64]; snprintf(k, sizeof k, "%s:%s", o->name, clip);
        bool dup = false; for (int i = 0; i < ns; i++) if (!strcmp(seen[i], k)) dup = true;
        if (!dup && ns < 64) { strcpy(seen[ns++], k); fprintf(stderr, "WARN missing clip %s on %s\n", clip, o->name); }
    }
#endif
    anim_play_idx_(o, idx, 0, restartIfSame);
}
void uo_anim_play_from(UObj *o, const char *clip, int frame) {
    if (o == heroObj) { hero_anim_play_from(clip, frame); lastHeroDone = false; return; }
    int idx = uo_anim_clip(o, clip); if (idx < 0) return;
    const Anim *a = &SETOF(o)->anims[idx];
    anim_play_idx_(o, idx, a->fps > 0 ? (frame + 0.0001f) / a->fps : 0, true);
}
void uo_anim_play_frame(UObj *o, int frame) {     // tk2dSpriteAnimator.PlayFromFrame on the current clip
    if (o == heroObj) { hero_anim_play_from(NULL, frame); return; }
    if (!o->an.has || o->an.clip < 0 || !SETOF(o)) return;
    const Anim *a = &SETOF(o)->anims[o->an.clip];
    anim_play_idx_(o, o->an.clip, a->fps > 0 ? (frame + 0.0001f) / a->fps : 0, true);
}
bool uo_anim_is(UObj *o, const char *clip) { if (o == heroObj) return hero_anim_is(clip); return o->an.clip >= 0 && o->an.clip == uo_anim_clip(o, clip); }
float uo_anim_clip_len(UObj *o, const char *clip) {
    if (o == heroObj) return hero_anim_clip_len(clip);
    int i = uo_anim_clip(o, clip); if (i < 0) return 0;
    const Anim *a = &SETOF(o)->anims[i]; return a->fps > 0 ? a->count / a->fps : 0;
}

// delegates (AnimationCompleted / AnimationEventTriggered) are set by FSM actions and stay until replaced
typedef struct { UObj *o; Fsm *fc, *ft; const char *complete, *trigger; } AnimCb;
static AnimCb cbs[UO_MAX_OBJS];
void uo_anim_set_callbacks(UObj *o, Fsm *f, const char *complete, const char *trigger, bool setComplete, bool setTrigger) {
    AnimCb *c = &cbs[o - objs];
    c->o = o;
    if (setComplete) { c->complete = complete; c->fc = f; }
    if (setTrigger) { c->trigger = trigger; c->ft = f; }
}
static void anim_trigger(UObj *o, int fr) {
    AnimCb *c = &cbs[o - objs];
    o->an.trig = fr;
    if (c->o == o && c->ft && c->trigger && c->ft->started) fsm_event(c->ft, c->trigger);
}
static void anim_complete(UObj *o) {
    AnimCb *c = &cbs[o - objs];
    o->an.done = true;
    o->an.prevFrame = -1;
    if (c->o == o && c->fc && c->complete && c->fc->started) fsm_event(c->fc, c->complete);
}
static void process_events(UObj *o, int start, int last) {
    if (start == last) return;
    const Anim *a = clipp(o); if (!a) return;
    for (int i = start + 1; i <= last && i < a->count; i++) if (frame_trig(o, i)) anim_trigger(o, i);
}
/* Mecanim Animator clips on sprites (tools/uanim.py): float curves on the SpriteRenderer colour, the transform's scale and
   position and the object's active flag, plus a sprite flip-book; keys are linear, a clip that doesn't loop holds its end */
static int sprite_frame_b(int bank, const char *guid);
static float uan_eval(JVal *k, float t) {
    JVal *a = k ? k->child : NULL; if (!a) return 0;
    float t0 = jnum(jat(a, 0), 0), v0 = jnum(jat(a, 1), 0);
    if (t <= t0) return v0;
    for (JVal *b = a->next; b; a = b, b = b->next) {
        float t1 = jnum(jat(b, 0), 0), v1 = jnum(jat(b, 1), 0);
        if (t <= t1) return t1 > t0 ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v1;
        t0 = t1; v0 = v1;
    }
    return v0;
}
static UObj *uan_target(UObj *o, const char *path) {
    if (!path || !*path) return o;
    char buf[96]; snprintf(buf, sizeof buf, "%s", path);
    for (char *s = strtok(buf, "/"); s && o; s = strtok(NULL, "/")) { UObj *c = o->child; while (c && strcmp(c->name, s)) c = c->next; o = c; }
    return o;
}
static void uan_update(UObj *o, float dt) {
    JVal *u = o->uan; float len = jnum(jget(u, "len"), 0);
    o->uanT += dt;
    float t = o->uanT;
    if (len > 0) { if (jbool(jget(u, "loop"), false)) t = fmodf(t, len); else if (t > len) t = len; }
    JVal *cv = jget(u, "cv");
    for (JVal *c = cv ? cv->child : NULL; c; c = c->next) {
        UObj *q = uan_target(o, jstr(jget(c, "p"), "")); if (!q) continue;
        const char *a = jstr(jget(c, "a"), ""); float v = uan_eval(jget(c, "k"), t);
        switch (a[0]) {
            case 'r': if (!a[1]) q->sr.r = v; break;
            case 'g': q->sr.g = v; break;
            case 'b': q->sr.b = v; break;
            case 'a': if (!a[1]) q->sr.a = v; else if (!strcmp(a, "act") && q != o) uo_set_active(q, v > 0.5f); break;
            case 'e': q->sr.en = v > 0.5f; break;
            case 's': if (a[1] == 'x') q->sx = v; else if (a[1] == 'y') q->sy = v; break;
            case 'p': if (a[1] == 'x') q->lx = v; else if (a[1] == 'y') q->ly = v; break;
        }
    }
    JVal *sp = jget(u, "spr");
    if (sp && sp->child) {
        JVal *pick = sp->child;
        for (JVal *e = sp->child; e; e = e->next) if (jnum(jat(e, 0), 0) <= t + 1e-4f) pick = e;
        int f = sprite_frame_b(o->bank, jstr(jat(pick, 1), NULL));
        if (f >= 0) o->sr.frame = f;          // frames not packed for this room keep the current sprite
    }
}

static void anim_update_obj_(UObj *o, float dt);
static void anim_update_obj(UObj *o, float dt) {
    int f0 = o->an.frame, c0 = o->an.clip;
    anim_update_obj_(o, dt);
    if (o->an.frame != f0 || o->an.clip != c0) tk_sprite_changed(o);
}
static void anim_update_obj_(UObj *o, float dt) {
    UAnim *an = &o->an;
    an->done = false; an->trig = -1;
    if (!an->playing) return;
    const Anim *a = clipp(o);
    if (!a || a->count == 0 || (an->fps <= 0 && a->wrap != WRAP_LOOP && a->wrap != WRAP_RANDOMLOOP)) {   // missing or frozen clip: it ends at once
        an->playing = false; anim_complete(o); return;
    }
    an->clipTime += dt * an->fps * (an->speed > 0 ? an->speed : 1);
    int prev = an->prevFrame;
    switch (a->wrap) {
        case WRAP_LOOP: case WRAP_RANDOMLOOP: {
            int f = ((int)an->clipTime) % a->count;
            if (f < prev) { process_events(o, prev, a->count - 1); process_events(o, -1, f); } else process_events(o, prev, f);
            an->frame = f; an->prevFrame = f; break; }
        case WRAP_LOOPSECTION: {
            int n = (int)an->clipTime, ls = a->loopStart < a->count ? a->loopStart : 0;
            int f = n < a->count ? n : ls + (n - a->count) % (a->count - ls > 0 ? a->count - ls : 1);
            if (f < prev) { process_events(o, prev, a->count - 1); process_events(o, ls - 1, f); } else process_events(o, prev, f);
            an->frame = f; an->prevFrame = f; break; }
        case WRAP_PINGPONG: {
            int n = a->count > 1 ? ((int)an->clipTime) % (a->count * 2 - 2) : 0;
            int f = n < a->count ? n : (a->count * 2 - 2 - n);
            an->frame = f; an->prevFrame = f; break; }
        case WRAP_ONCE: default: {
            int n = (int)an->clipTime;
            if (n >= a->count) {
                an->frame = a->count - 1; an->playing = false;
                process_events(o, prev, a->count - 1);
                anim_complete(o);
            } else { process_events(o, prev, n); an->frame = n; an->prevFrame = n; }
            break; }
    }
}


/* ---------------- creation ---------------- */
static UCol *add_col(UObj *o) {
    if (!o->col) { o->col = o->colIn; o->capcol = UO_MAX_COLS; }
    if (o->ncol >= o->capcol) {
        int cap = o->capcol * 2;
        UCol *n = o->col == o->colIn ? malloc(sizeof(UCol) * cap) : realloc(o->col, sizeof(UCol) * cap);
        if (!n) return NULL;
        if (o->col == o->colIn) memcpy(n, o->colIn, sizeof(UCol) * o->ncol);
        o->col = n; o->capcol = cap;
    }
    UCol *c = &o->col[o->ncol++]; memset(c, 0, sizeof *c);
    return c;
}

static UObj *alloc_obj(void) {
    for (int i = 0; i < UO_MAX_OBJS; i++) if (!objs[i].used) {
        memset(&objs[i], 0, sizeof(UObj)); memset(&cbs[i], 0, sizeof(AnimCb));
        objs[i].col = objs[i].colIn; objs[i].capcol = UO_MAX_COLS;
        objs[i].used = true; objs[i].bank = (unsigned char)curBank; objs[i].persist = curBank == 1; if (i >= nobjs) nobjs = i + 1;
        return &objs[i];
    }
    return NULL;
}

UObj *uo_find_tag(const char *tag) {
    if (!tag) return NULL;
    if (!strcmp(tag, "Player")) return heroObj;
    for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i]; if (o->used && !o->destroyed && !strcmp(o->tag, tag)) return o; }
    return NULL;
}
UObj *uo_alloc_pseudo(const char *name) {
    UObj *o = alloc_obj(); if (!o) return NULL;
    snprintf(o->name, sizeof o->name, "%s", name); o->activeSelf = true; o->sx = o->sy = 1; o->tmpl = -1; o->started = true; o->pseudo = true;
    return o;
}
static UObj *evSender;
UObj *uo_event_sender(void) { return evSender; }
void uo_set_event_sender(UObj *o) { evSender = o; }
UObj *uo_slash_obj(void) { return slashObj; }
UComp *uo_comp_by_cls(UObj *o, const char *cls) {
    if (!o) return NULL;
    for (int k = 0; k < o->ncomp; k++) if (o->comp[k].cls && !strcmp(o->comp[k].cls, cls)) return &o->comp[k];
    return NULL;
}
bool uo_has_component_named(UObj *o, const char *cls) {
    if (!o || !cls) return false;
    const char *d = strrchr(cls, '.'); if (d) cls = d + 1;
    for (int i = 0; i < o->ncomp; i++) if (o->comp[i].cls && !strcmp(o->comp[i].cls, cls)) return true;
    if (!strcmp(cls, "Rigidbody2D")) return o->rb.has;
    if (!strcmp(cls, "PlayMakerFSM")) return o->nfsm > 0;
    return false;
}
void uo_send_event(UObj *o, const char *ev) {
    if (!o || o->destroyed || !ev) return;
    for (int k = 0; k < o->nfsm; k++) fsm_event(o->fsm[k], ev);
    uo_native_event(o, ev);
}
void game_global_event(const char *ev);
void uo_broadcast(const char *ev) {
    game_global_event(ev);
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || !uo_active(o)) continue;
        for (int k = 0; k < o->nfsm; k++) fsm_event(o->fsm[k], ev);
        uo_native_event(o, ev);
    }
}

static Tmpl *tmpl_at(int ti) { int b = ti / TMPL_BANK, i = ti % TMPL_BANK; return (ti >= 0 && b < NBANK && i < banks[b].ntmpl) ? &banks[b].tmpls[i] : NULL; }
JVal *uo_template_objects(int ti) { Tmpl *t = tmpl_at(ti); return t ? t->objects : NULL; }
int uo_template_count(void) { return banks[0].ntmpl; }
const char *uo_template_key(int ti) { Tmpl *t = tmpl_at(ti); return t ? t->key : NULL; }
JVal *uo_comp_fields(UObj *o, const char *cls) {
    for (UObj *q = o; q; q = q->child) {       // the object itself, then down its first children
        for (int k = 0; k < q->ncomp; k++) if (q->comp[k].cls && !strcmp(q->comp[k].cls, cls)) return q->comp[k].f;
        if (q != o) break;
    }
    for (UObj *c = o->child; c; c = c->next) for (int k = 0; k < c->ncomp; k++) if (c->comp[k].cls && !strcmp(c->comp[k].cls, cls)) return c->comp[k].f;
    return NULL;
}
static int sprite_frame_b(int bank, const char *guid) {
    SpriteSet *ss = banks[bank].set.loaded ? &banks[bank].set : NULL;
    if (!guid || !ss) return -1;
    char nm[24]; snprintf(nm, sizeof nm, "sp:%.12s", guid);
    return spr_named(ss, nm);
}
int uo_sprite_frame(const char *guid) { return sprite_frame_b(curBank, guid); }
void uo_set_text(UObj *o, const char *text) {
    if (!o) return;
#ifdef __HOST__
    if (getenv("HK_TEXTTRACE")) fprintf(stderr, "[text %d] %s: %s\n", uo_frame, o->name, text ? text : "");
#endif
    if (!o->tmp.has) { o->tmp.has = true; o->tmp.en = true; o->tmp.size = 8; o->tmp.r = o->tmp.g = o->tmp.b = o->tmp.a = 1; }
    free(o->tmp.text); o->tmp.text = strdup(text ? text : "");
    for (char *c = o->tmp.text; *c; c++) if (*c == '\f') *c = '\n';
}

static int lib_index(int bank, const char *guid) {
    if (!guid) return -1;
    Bank *b = &banks[bank];
    for (int i = 0; i < b->nlibs; i++) if (!strcmp(b->libs[i].guid, guid)) return i;
    return -1;
}

// prefab lookup: the running object's own bank first (the Knight's spells spawn the Knight's prefabs)
int uo_template_by_key(const char *key) {
    if (!key) return -1;
    int order[NBANK + 1], no = 0; order[no++] = curBank;
    for (int k = 0; k < NBANK; k++) if (k != curBank) order[no++] = k;
    for (int q = 0; q < no; q++) {
        int b = order[q];
        Bank *bk = &banks[b];
        for (int i = 0; i < bk->ntmpl; i++) if (bk->tmpls[i].key && !strcmp(bk->tmpls[i].key, key)) return b * TMPL_BANK + i;
    }
    return -1;
}

static void setup_comp(UObj *o, JVal *c) {
    const char *k = jstr(jget(c, "k"), "");
    if (!strcmp(k, "col")) {
        UCol *u = add_col(o); if (!u) return;
        const char *t = jstr(jget(c, "t"), "box");
        u->trig = jbool(jget(c, "trig"), false); u->en = jbool(jget(c, "en"), true);
        if (!strcmp(t, "box")) { u->t = COL_BOX; u->ox = jnum(jget(c, "ox"), 0); u->oy = jnum(jget(c, "oy"), 0); u->w = jnum(jget(c, "w"), 1); u->h = jnum(jget(c, "h"), 1); }
        else if (!strcmp(t, "circle")) { u->t = COL_CIRCLE; u->ox = jnum(jget(c, "ox"), 0); u->oy = jnum(jget(c, "oy"), 0); u->r = jnum(jget(c, "r"), 0.5); }
        else {
            JVal *pts = !strcmp(t, "poly") ? jat(jget(c, "paths"), 0) : jget(c, "pts");
            u->t = !strcmp(t, "poly") ? COL_POLY : COL_EDGE;
            u->npts = pts ? pts->n : 0;
            u->pts = malloc(sizeof(float) * 2 * (u->npts ? u->npts : 1));
            int i = 0; for (JVal *p = pts ? pts->child : NULL; p; p = p->next, i++) { u->pts[i * 2] = jnum(jat(p, 0), 0); u->pts[i * 2 + 1] = jnum(jat(p, 1), 0); }
        }
    } else if (!strcmp(k, "rb")) {
        o->rb.has = true; o->rb.g = jnum(jget(c, "g"), 1);
        o->rb.kin = jint(jget(c, "type"), 0) == 1 || jbool(jget(c, "kin"), false);
        o->rb.sim = jint(jget(c, "type"), 0) != 2;      // static bodies don't move
        if (!strncmp(o->name, "Corpse", 6)) o->rb.fric = 0.4f;   // corpses without a Corpse script (Husk Guard...) stop sliding too
    } else if (!strcmp(k, "mr")) {
        o->mr = jbool(jget(c, "en"), true); o->mrSet = true;
    } else if (!strcmp(k, "as")) {
        o->as.has = true; o->as.clip = jstr(jget(c, "clip"), NULL); o->as.vol = jnum(jget(c, "vol"), 1); o->as.pitch = jnum(jget(c, "pitch"), 1);
        o->as.loop = jbool(jget(c, "loop"), false); o->as.awake = jbool(jget(c, "awake"), false); o->as.en = jbool(jget(c, "en"), true); o->as.h = -1;
        o->as.min = jnum(jget(c, "min"), 1); o->as.max = jnum(jget(c, "max"), 500); o->as.roll = jint(jget(c, "roll"), 0); o->as.sb = jnum(jget(c, "sb"), 1);
        o->as.on = o->as.loop && o->as.awake; o->as.slot = -1;
    } else if (!strcmp(k, "sr") || !strcmp(k, "sprite")) {
        o->sr.has = true; o->sr.en = jbool(jget(c, "en"), true); o->sr.set = 0;
        o->sr.r = o->sr.g = o->sr.b = o->sr.a = 1;
        JVal *col = jget(c, !strcmp(k, "sr") ? "col" : "color");
        if (col && col->t == J_ARR && col->n >= 4) { o->sr.r = jnum(jat(col, 0), 1); o->sr.g = jnum(jat(col, 1), 1); o->sr.b = jnum(jat(col, 2), 1); o->sr.a = jnum(jat(col, 3), 1); }
        else if (col && col->t == J_OBJ) { o->sr.r = jnum(jget(col, "r"), 1); o->sr.g = jnum(jget(col, "g"), 1); o->sr.b = jnum(jget(col, "b"), 1); o->sr.a = jnum(jget(col, "a"), 1); }
        if (!strcmp(k, "sr")) { o->sr.frame = sprite_frame_b(o->bank, jstr(jget(c, "sp"), NULL)); o->sr.fx = jbool(jget(c, "fx"), false); o->sr.fy = jbool(jget(c, "fy"), false); o->sr.order = jint(jget(c, "ord"), 0);
            // the sprite's material (tools/obj_particles.py): additive glows, or the lit material the ambient light tints.
            // Where it's known it replaces the guess from the object's name.
            o->sr.lit = jbool(jget(c, "lit"), false);
            o->sr.cutout = jbool(jget(c, "cut"), false) || !strncmp(o->name, "Vignette Cutout", 15);
            if (jbool(jget(c, "add"), false)) o->additive = true; else if (o->sr.lit) o->additive = false; }
        else {   // static tk2dSprite frame "tk:<coll8>:<id>"
            o->sr.tk = true;
            snprintf(o->sr.coll, sizeof o->sr.coll, "%.8s", jstr(jget(c, "coll"), ""));
            char nm[40]; snprintf(nm, sizeof nm, "tk:%.8s:%d", jstr(jget(c, "coll"), ""), jint(jget(c, "id"), 0));
            o->sr.frame = SETOF(o) ? spr_named(SETOF(o), nm) : -1;
        }
    } else if (!strcmp(k, "tmp")) {
        o->tmp.has = true; o->tmp.en = jbool(jget(c, "en"), true);
        free(o->tmp.text); o->tmp.text = strdup(jstr(jget(c, "text"), ""));
        o->tmp.size = jnum(jget(c, "size"), 8); o->tmp.align = jint(jget(c, "align"), 0); o->tmp.font = jint(jget(c, "font"), 0);
        o->tmp.rw = jnum(jget(c, "rw"), 0); o->tmp.px = jnum(jget(c, "px"), 0.5);
        o->tmp.rh = jnum(jget(c, "rh"), 0); o->tmp.py = jnum(jget(c, "py"), 0.5);
        JVal *col = jget(c, "col"); o->tmp.r = o->tmp.g = o->tmp.b = o->tmp.a = 1;
        if (col && col->n >= 4) { o->tmp.r = jnum(jat(col, 0), 1); o->tmp.g = jnum(jat(col, 1), 1); o->tmp.b = jnum(jat(col, 2), 1); o->tmp.a = jnum(jat(col, 3), 1); }
    } else if (!strcmp(k, "anim")) {
        o->an.has = true; o->an.en = jbool(jget(c, "en"), true);
        o->an.lib = lib_index(o->bank, jstr(jget(c, "lib"), NULL)); o->an.clip = -1; o->an.speed = 1;
        const char *clip = jstr(jget(c, "clip"), NULL);
        if (clip) { int idx = uo_anim_clip(o, clip); o->an.clip = idx; o->an.frame = 0; }
        o->an.prevFrame = -1;
        // tk2d "play automatically": the clip runs from the start at its own speed. (The speed used to be left at 0, so
        // every auto-played idle - Myla mining, NPCs, critters, decorations - sat on its first frame until a script played
        // something else, e.g. Myla's talk animation.)
        if (jbool(jget(c, "auto"), false) && clip) { int idx = uo_anim_clip(o, clip); o->an.playing = false; if (idx >= 0) anim_play_idx_(o, idx, 0, true); }
    } else if (!strcmp(k, "uan")) {
        o->uan = c; o->uanT = 0;
    } else if (!strcmp(k, "ps")) {     // a particle system (tools/obj_particles.py)
        o->comp = realloc(o->comp, sizeof(UComp) * (o->ncomp + 1));
        UComp *u = &o->comp[o->ncomp++]; memset(u, 0, sizeof *u);
        u->type = C_PS; u->cls = "ParticleSystem"; u->en = true; u->f = c;
    } else if (!strcmp(k, "mb")) {
        const char *cls = jstr(jget(c, "cls"), "");
        if (!strcmp(cls, "SetTextMeshProGameText")) {      // Awake: the localized text
            JVal *f = jget(c, "f");
            const char *t = lang_get(jstr(jget(f, "convName"), NULL), jstr(jget(f, "sheetName"), NULL));
            if (t) uo_set_text(o, t);
            return;
        }
        CompType t = comp_type_of(cls);   // unknown classes are kept for their fields (ShopMenuStock, ShopItemStats...)
        o->comp = realloc(o->comp, sizeof(UComp) * (o->ncomp + 1));
        UComp *u = &o->comp[o->ncomp++]; memset(u, 0, sizeof *u);
        u->type = t; u->cls = cls; u->en = jbool(jget(c, "en"), true); u->f = jget(c, "f");
    }
}

static void obj_enable(UObj *o);
static void start_tree(UObj **tree, int n);

static UObj *knightRoot;    // when set, the Knight bank's scene root maps onto this (the HeroController stand-in)
static UObj *instantiate(int ti, bool isScene, float x, float y, float rz, UObj ***outTree, int *outN) {
    Tmpl *t = tmpl_at(ti);
    if (!t) return NULL;
    int bank = ti / TMPL_BANK, saveBank = curBank; curBank = bank;
    int n = t->objects ? t->objects->n : 0;
    if (n <= 0) { curBank = saveBank; return NULL; }
    UObj **tree = calloc(n, sizeof(UObj *));
    int i = 0;
    for (JVal *jo = t->objects->child; jo; jo = jo->next, i++) {
        bool isRoot = isScene && i == 0 && knightRoot && bank == 1;
        UObj *o = isRoot ? knightRoot : alloc_obj(); if (!o) break;
        tree[i] = o;
        if (isRoot) {        // keep the stand-in's collider / body; take the prefab's components and FSMs
            for (JVal *c = jget(jo, "c") ? jget(jo, "c")->child : NULL; c; c = c->next) {
                const char *k = jstr(jget(c, "k"), "");
                if (!strcmp(k, "mb")) setup_comp(o, c);
            }
            continue;
        }
        strncpy(o->name, jstr(jget(jo, "n"), ""), 39);
        { char low[40]; int k = 0; for (; o->name[k] && k < 39; k++) low[k] = (char)tolower((unsigned char)o->name[k]); low[k] = 0;
          o->additive = (strstr(low, "glow") || (strstr(low, "light") && !strstr(low, "lightning") && !strstr(low, "lighthouse"))) && !strstr(low, "lever"); }
        o->lx = jnum(jget(jo, "x"), 0); o->ly = jnum(jget(jo, "y"), 0); o->lz = jnum(jget(jo, "z"), 0);
        o->sx = jnum(jget(jo, "sx"), 1); o->sy = jnum(jget(jo, "sy"), 1); o->rz = jnum(jget(jo, "rz"), 0);
        o->activeSelf = jbool(jget(jo, "act"), true); o->layer = jint(jget(jo, "layer"), 0);
        JVal *tg = jget(jo, "tag"); if (tg && tg->t == J_STR) strncpy(o->tag, tg->s, 23);
        o->tmpl = isScene ? -1 : ti; o->lastSwing = -1; o->spawned = !isScene;
        o->tintR = o->tintG = o->tintB = o->tintA = 1;
        int p = jint(jget(jo, "p"), -1);
        if (p >= 0 && p < i && tree[p]) {
            o->parent = tree[p];
            // append as last child
            UObj **pp = &tree[p]->child; while (*pp) pp = &(*pp)->next; *pp = o;
        }
        for (JVal *c = jget(jo, "c") ? jget(jo, "c")->child : NULL; c; c = c->next) setup_comp(o, c);
    }
    int made = i;
    // UI-layer roots (prefab messages, scene menus like the Shop Menu) belong to the HUD camera
    for (int q = 0; q < made; q++) { UObj *r = tree[q]; while (r->parent) r = r->parent; if (r->layer == 5) tree[q]->hud = true; }
    // FSMs (after all objects so references resolve)
    i = 0;
    for (JVal *jo = t->objects->child; jo && i < made; jo = jo->next, i++) {
        UObj *o = tree[i];
        for (JVal *c = jget(jo, "c") ? jget(jo, "c")->child : NULL; c; c = c->next) {
            if (strcmp(jstr(jget(c, "k"), ""), "fsm")) continue;
            JVal *def = jat(t->fsms, jint(jget(c, "i"), -1));
            if (!def) continue;
            Fsm *f = fsm_create(def, o, tree, made);
            f->enabled = jbool(jget(c, "en"), true);
            o->fsm = realloc(o->fsm, sizeof(Fsm *) * (o->nfsm + 1));
            o->fsm[o->nfsm++] = f;
        }
    }
    if (!isScene && made > 0) {
        tree[0]->lx = x; tree[0]->ly = y; tree[0]->rz = rz;
        tree[0]->activeSelf = true;          // ObjectPool.Spawn / Instantiate hand back an active object
    }
    for (i = 0; i < made; i++) { tree[i]->treeArr = tree; tree[i]->treeN = made; }
    for (i = 0; i < made; i++) {   // enemies whose hurtbox comes from their tk2d sprite (Mantis Lords...): use the frame's bounds
        UObj *o = tree[i];
        if (!o->an.has || !uo_comp(o, C_HEALTH)) continue;
        bool any = false; for (int k = 0; k < o->ncol; k++) if (o->col[k].t != COL_EDGE) any = true;
        if (any) continue;
        UCol *c = add_col(o); if (!c) continue;
        c->t = COL_BOX; c->trig = true; c->en = true; c->sprBox = true;
    }
    for (i = 0; i < made; i++) for (int k = 0; k < tree[i]->ncomp; k++) comp_awake(tree[i], &tree[i]->comp[k]);
    if (outTree) { *outTree = tree; *outN = made; }
    curBank = saveBank;
    return made > 0 ? tree[0] : NULL;
}

static UObj ***trees; static bool *treePersist; static int ntrees, captrees;   // keep tree arrays alive (FSM references)
static void keep_tree(UObj **t) {
    if (ntrees >= captrees) { captrees = captrees ? captrees * 2 : 512; trees = realloc(trees, captrees * sizeof *trees); treePersist = realloc(treePersist, captrees * sizeof *treePersist); }
    treePersist[ntrees] = curBank == 1; trees[ntrees++] = t;
}

static void start_tree(UObj **tree, int n) {
    for (int i = 0; i < n; i++) {
        UObj *o = tree[i];
        if (!uo_active(o)) continue;
        o->started = true;
        if (o->as.has && o->as.en && o->as.awake && !o->as.loop) uo_source_play(o);
        for (int k = 0; k < o->ncomp; k++) { comp_enable(o, &o->comp[k]); comp_start(o, &o->comp[k]); }
        for (int k = 0; k < o->nfsm; k++) if (o->fsm[k]->enabled) fsm_start_new(o->fsm[k]);
    }
}

UObj *uo_spawn(int ti, float x, float y, float rz) {
    Tmpl *tp = tmpl_at(ti); if (!tp || !tp->objects) return NULL;
    UObj **tree; int n;
    int save = curBank; curBank = ti / TMPL_BANK;
    UObj *r = instantiate(ti, false, x, y, rz, &tree, &n);
    if (!r) { curBank = save; return NULL; }
    keep_tree(tree);
    start_tree(tree, n);
    curBank = save;
    return r;
}

static void obj_disable_tree(UObj *o) {
    for (UObj *c = o->child; c; c = c->next) if (c->activeSelf) obj_disable_tree(c);
    for (int k = 0; k < o->ncomp; k++) comp_disable(o, &o->comp[k]);
    for (int k = 0; k < o->nfsm; k++) fsm_stop(o->fsm[k]);
}
static void obj_enable(UObj *o) {
    bool first = !o->started;          // MonoBehaviour.Start runs on the first enable
    o->started = true;
    o->uanT = 0;                       // an Animator starts its default state again when enabled
    if (o->as.has && o->as.en && o->as.awake && !o->as.loop) uo_source_play(o);
    for (int k = 0; k < o->ncomp; k++) { comp_enable(o, &o->comp[k]); if (first) comp_start(o, &o->comp[k]); }
    for (int k = 0; k < o->nfsm; k++) if (o->fsm[k]->enabled) {
        Fsm *f = o->fsm[k];
        if (jbool(jget(f->def, "restart"), true) || !f->started) { fsm_stop(f); fsm_start(f); }
    }
    for (UObj *c = o->child; c; c = c->next) if (c->activeSelf && !c->destroyed) obj_enable(c);
}

void uo_set_active(UObj *o, bool a) {
    if (!o || o->activeSelf == a) return;
    if (a && o->ncomp) { extern bool hm_saved_dead(UObj *); if (hm_saved_dead(o)) return; }
    bool was = uo_active(o);
    o->activeSelf = a;
    bool now = uo_active(o);
    if (o->native) { o->native(o, now ? "__ENABLE" : "__DISABLE"); return; }
    if (was && !now) obj_disable_tree(o);
    else if (!was && now) obj_enable(o);
}

void uo_destroy(UObj *o) {
    if (!o || o->destroyed) return;
    for (UObj *c = o->child; c; c = c->next) uo_destroy(c);
    if (uo_active(o)) { for (int k = 0; k < o->nfsm; k++) fsm_stop(o->fsm[k]); }
    for (int k = 0; k < o->ncomp; k++) if (o->comp[k].type == C_PS) { particles_obj_forget(o); break; }
    o->destroyed = true;
}

void uo_destroy_after(UObj *o, float t) { if (o && !o->destroyed) o->destroyT = t; }

void uo_set_parent(UObj *o, UObj *np, bool resetLocal) {
    if (!o || o->parent == np) return;
    float x = uo_wx(o), y = uo_wy(o), sx = uo_wsx(o), sy = uo_wsy(o), rz = uo_wrz(o);
    if (o->parent) { UObj **pp = &o->parent->child; while (*pp && *pp != o) pp = &(*pp)->next; if (*pp) *pp = o->next; }
    o->next = NULL; o->parent = np;
    if (np) { UObj **pp = &np->child; while (*pp) pp = &(*pp)->next; *pp = o; }
    if (resetLocal) { o->lx = o->ly = 0; return; }
    if (np) {    // keep the world transform
        float psx = uo_wsx(np), psy = uo_wsy(np);
        o->sx = psx != 0 ? sx / psx : sx; o->sy = psy != 0 ? sy / psy : sy; o->rz = rz - uo_wrz(np);
        uo_set_world_pos(o, x, y);
    } else { o->lx = x; o->ly = y; o->sx = sx; o->sy = sy; o->rz = rz; }
    // a UI-layer object detached to the scene root is drawn by the HUD camera from then on (King's Pass focus prompt)
    if (!np && o->layer == 5 && !o->hud) { UObj *st[256]; int n = 0; st[n++] = o;
        while (n) { UObj *q = st[--n]; q->hud = true; for (UObj *c = q->child; c && n < 256; c = c->next) st[n++] = c; } }
}

static void free_obj(UObj *o) {
    for (int k = 0; k < o->nfsm; k++) fsm_free(o->fsm[k]);
    free(o->fsm);
    for (int k = 0; k < o->ncomp; k++) comp_free(&o->comp[k]);
    free(o->comp);
    for (int k = 0; k < o->ncol; k++) free(o->col[k].pts);
    if (o->col != o->colIn) free(o->col);
    free(o->tmp.text);
    // unlink from parent
    if (o->parent) { UObj **pp = &o->parent->child; while (*pp && *pp != o) pp = &(*pp)->next; if (*pp) *pp = o->next; }
    for (UObj *c = o->child; c; c = c->next) c->parent = NULL;
    memset(o, 0, sizeof *o);
}

static void reap(void) {
    for (int i = 0; i < nobjs; i++) if (objs[i].used && objs[i].destroyed) {
        // tree arrays may still point at it; FSM lookups check destroyed via uo_active
        free_obj(&objs[i]);
    }
}

/* ---------------- room load ---------------- */
// a room unload keeps the persistent objects (the Knight's tree, the global stand-ins) and clears their references
// to the room's objects (FSM variables, trigger overlaps, delayed events, animation callbacks)
static void terrain_reset(void);
static UObj *hcOwner[32]; static int nhc;
static bool is_room_obj(const UObj *o) { return o && o >= objs && o < objs + UO_MAX_OBJS && o->used && !o->persist; }
static void scrub_persistent(void) {
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i]; if (!o->used || !o->persist) continue;
        for (int k = 0; k < o->nfsm; k++) {
            Fsm *f = o->fsm[k];
            for (int v = 0; v < f->nvar; v++) if (f->var[v].go && is_room_obj(f->var[v].go)) f->var[v].go = NULL;
            int nd = 0;
            for (int d = 0; d < f->ndelayed; d++) if (!f->delayed[d].target || !is_room_obj(f->delayed[d].target->owner)) f->delayed[nd++] = f->delayed[d];
            f->ndelayed = nd;
        }
        int nov = 0; for (int q = 0; q < o->nov; q++) if (!is_room_obj(o->ov[q]) && o->ov[q] != &terrainObj) o->ov[nov++] = o->ov[q];
        o->nov = nov;
        AnimCb *c = &cbs[i];
        if (c->fc && is_room_obj(c->fc->owner)) { c->fc = NULL; c->complete = NULL; }
        if (c->ft && is_room_obj(c->ft->owner)) { c->ft = NULL; c->trigger = NULL; }
        if (o->parent && is_room_obj(o->parent)) uo_set_parent(o, NULL, false);
    }
}
static void unload_bank(int b) {
    Bank *bk = &banks[b];
    json_free(&bk->arena); bk->root = NULL; bk->tkbox = bk->tkpoly = bk->tksid = NULL; bk->ntmpl = 0; bk->nlibs = 0;
    if (bk->set.loaded) spr_free(&bk->set);
    bk->loaded = false;
}
void uo_unload(void) {
    loops_stop_all();
    comps_save_persistent();
    scrub_persistent();
    for (int i = 0; i < nobjs; i++) if (objs[i].used && !objs[i].persist) objs[i].destroyed = true;
    for (int i = 0; i < nobjs; i++) if (objs[i].used && !objs[i].persist) free_obj(&objs[i]);
    int hw = 0; for (int i = 0; i < nobjs; i++) if (objs[i].used) hw = i + 1;
    nobjs = hw;
    int nt = 0;
    for (int i = 0; i < ntrees; i++) { if (treePersist[i]) { trees[nt] = trees[i]; treePersist[nt++] = true; } else free(trees[i]); }
    ntrees = nt;
    unload_bank(0); unload_bank(2);
    terrain_reset();
    uo_spriteset = NULL;
    fsm_globals_clear();
    if (persistReady) { fsm_globals_bind_go("Hero", heroObj); for (int i = 0; i < NGLOBAL; i++) fsm_globals_bind_go(GLOBAL_GOS[i], globalObj[i]); }
    loaded = persistReady;
}
void uo_reset(void) { uo_unload(); }
void uo_shutdown(void) {
    for (int i = 0; i < nobjs; i++) if (objs[i].used) objs[i].persist = false;
    persistReady = false;
    uo_unload();
    for (int i = 0; i < ntrees; i++) free(trees[i]);
    ntrees = 0;
    unload_bank(1);
    heroObj = heroBoxObj = slashObj = NULL; knightRoot = NULL;
    memset(globalObj, 0, sizeof globalObj);
    dlg_set_vars(NULL, NULL);
    loaded = false;
}

// the room's object sprite pages (1 MB each) are loaded first thing when the room changes, into the holes the old room's
// pages left: loaded after the room's art, its smaller allocations could split those holes and leave no room for a
// page (on the console, after a few rooms: the page's sprites - Dirtmouth's Elderbug - went missing until a restart)
static SpriteSet preSet; static char preName[48];
void uo_preload_sprites(const char *room) {
    if (preSet.loaded) spr_free(&preSet);
    preName[0] = 0;
    char path[96]; snprintf(path, sizeof path, "romfs:/rooms/%s_objs.spr", room);
    FILE *f = fopen(path, "rb"); if (!f) return; fclose(f);
    if (spr_load(&preSet, path)) snprintf(preName, sizeof preName, "%s", room);
}
static bool load_bank(int b, const char *name) {
    Bank *bk = &banks[b];
    char path[96]; snprintf(path, sizeof path, "romfs:/rooms/%s.obj", name);
    size_t sz; char *txt = read_file(path, &sz);
    if (!txt) return false;
    bk->root = json_parse(&bk->arena, txt, sz);
    free(txt);
    if (!bk->root) { json_free(&bk->arena); return false; }
    snprintf(path, sizeof path, "romfs:/rooms/%s_objs.spr", name);
    if (preSet.loaded && !strcmp(preName, name)) { bk->set = preSet; memset(&preSet, 0, sizeof preSet); preName[0] = 0; }
    else spr_load(&bk->set, path);
    JVal *lb = jget(bk->root, "libs");
    for (JVal *l = lb ? lb->child : NULL; l && bk->nlibs < 64; l = l->next) {
        bk->libs[bk->nlibs].guid = l->key; snprintf(bk->libs[bk->nlibs].key, sizeof bk->libs[bk->nlibs].key, "%.8s", l->key); bk->nlibs++;
    }
    bk->tmpls[0].key = "scene"; bk->tmpls[0].objects = jget(bk->root, "objects"); bk->tmpls[0].fsms = jget(bk->root, "fsms"); bk->tmpls[0].name = "scene";
    bk->ntmpl = 1;
    JVal *pf = jget(bk->root, "prefabs");
    for (JVal *p = pf ? pf->child : NULL; p && bk->ntmpl < 160; p = p->next) {
        if (p->t != J_OBJ) continue;
        bk->tmpls[bk->ntmpl].key = p->key; bk->tmpls[bk->ntmpl].objects = jget(p, "objects"); bk->tmpls[bk->ntmpl].fsms = jget(p, "fsms");
        bk->tmpls[bk->ntmpl].name = jstr(jget(p, "name"), ""); bk->ntmpl++;
    }
    bk->tkbox = jget(bk->root, "tkbox"); bk->tkpoly = jget(bk->root, "tkpoly"); bk->tksid = jget(bk->root, "tksid");
    { JVal *au = jget(bk->root, "audio"); for (JVal *a = au ? au->child : NULL; a; a = a->next) if (a->t == J_STR) sfx_preload(a->s); }
    bk->loaded = true;
    return true;
}

// the stand-ins every FSM can see: the Knight (HeroController), its HeroBox, the nail slash, the PlayMaker globals
static void make_pseudos(void) {
    heroObj = alloc_obj(); strcpy(heroObj->name, "Knight"); heroObj->activeSelf = true; heroObj->layer = 9; strcpy(heroObj->tag, "Player");
    heroObj->sx = heroObj->sy = 1; heroObj->rb.has = true; heroObj->rb.sim = false; heroObj->ncol = 1; heroObj->col[0].t = COL_BOX; heroObj->col[0].en = true;
    heroObj->tmpl = -1; heroObj->started = true; heroObj->pseudo = true;
    heroBoxObj = alloc_obj(); strcpy(heroBoxObj->name, "HeroBox"); heroBoxObj->activeSelf = true; heroBoxObj->layer = 20; strcpy(heroBoxObj->tag, "HeroBox");
    heroBoxObj->sx = heroBoxObj->sy = 1; heroBoxObj->ncol = 1; heroBoxObj->col[0].t = COL_BOX; heroBoxObj->col[0].en = true; heroBoxObj->col[0].trig = true;
    heroBoxObj->tmpl = -1; heroBoxObj->started = true; heroBoxObj->pseudo = true;
    slashObj = alloc_obj(); strcpy(slashObj->name, "Slash"); slashObj->activeSelf = false; slashObj->layer = 17; strcpy(slashObj->tag, "Nail Attack");
    slashObj->sx = slashObj->sy = 1; slashObj->ncol = 1; slashObj->col[0].t = COL_BOX; slashObj->col[0].en = true; slashObj->col[0].trig = true;
    slashObj->tmpl = -1; slashObj->started = true; slashObj->pseudo = true;
    heroObj->an.has = true; heroObj->an.clip = -1; heroSynced = false;
    make_globals();
}

void uo_init_persistent(void) {
    if (persistReady) return;
    memset(&terrainObj, 0, sizeof terrainObj); terrainObj.col = terrainObj.colIn; terrainObj.capcol = UO_MAX_COLS; terrainObj.used = true; terrainObj.activeSelf = true; terrainObj.layer = 8; strcpy(terrainObj.tag, "Untagged");
    strcpy(terrainObj.name, "Terrain"); terrainObj.pseudo = true; terrainObj.sx = terrainObj.sy = 1;
    curBank = 1;
    make_pseudos();
    if (load_bank(1, "_Knight")) {
        knightRoot = heroObj;
        UObj **tree; int n;
        if (instantiate(1 * TMPL_BANK + 0, true, 0, 0, 0, &tree, &n)) { keep_tree(tree); persistReady = true; loaded = true; start_tree(tree, n); }
        knightRoot = NULL;
    }
    persistReady = true; loaded = true;
    curBank = 0;
}

// SceneAdditiveLoadConditional (romfs/text/additive.txt, tools/additive.py): which extra scene loads with this room
static bool additive_scene(const char *room, char *out, int n) {
    FILE *f = fopen("romfs:/text/additive.txt", "rb"); if (!f) return false;
    char ln[1024]; bool found = false;
    while (!found && fgets(ln, sizeof ln, f)) {
        char *tab = strchr(ln, '\t'); if (!tab) continue; *tab = 0;
        if (strcmp(ln, room)) continue;
        JArena ar = { 0 }; JVal *d = json_parse(&ar, tab + 1, strlen(tab + 1));
        if (!d) continue;
        bool fail = false;
        const char *b = jstr(jget(d, "bool"), "");
        if (b[0] && pd_bool(b) != (jint(jget(d, "boolValue"), 0) != 0)) fail = true;
        for (JVal *e = jget(d, "extraBool") ? jget(d, "extraBool")->child : NULL; e && !fail; e = e->next)
            if (pd_bool(jstr(jat(e, 0), "")) != (jint(jat(e, 1), 0) != 0)) fail = true;
        const char *iv = jstr(jget(d, "int"), "");
        if (!fail && iv[0] && ((pd_int(iv) == jint(jget(d, "intValue"), 0)) != (jint(jget(d, "isIntValue"), 0) != 0))) fail = true;
        JVal *xi = jget(d, "extraInt");
        if (!fail && xi && xi->n > 1) for (JVal *e = xi->child; e && !fail; e = e->next) {
            int a = pd_int(jstr(jat(e, 0), "")); const char *on = jstr(jat(e, 2), "");
            int v = on[0] ? pd_int(on) : jint(jat(e, 1), 0); bool ok = false;
            switch (jint(jat(e, 3), 0)) { case 0: ok = a == v; break; case 1: ok = a != v; break; case 2: ok = a < v; break; case 3: ok = a > v; break; case 4: ok = a <= v; break; case 5: ok = a >= v; break; }
            if (!ok) fail = true;
        }
        JVal *pb = jget(d, "pbi");
        if (!fail && pb && pb->t == J_ARR) { char k[96]; snprintf(k, sizeof k, "pbi:%s:%s", jstr(jat(pb, 1), ""), jstr(jat(pb, 0), "")); if (pd_bool(k)) fail = true; }
        const char *door = jstr(jget(d, "door"), "");
        if (!fail && door[0] && strcmp(door, game_entry_gate())) fail = true;
        const char *pick = fail ? jstr(jget(d, "alt"), "") : jstr(jget(d, "scene"), "");
        if (pick[0]) { snprintf(out, n, "%s", pick); found = true; }
        json_free(&ar);
    }
    fclose(f);
    return found;
}

bool uo_hero_ready(void) { return heroSynced; }
bool uo_load_room(const char *room) {
    if (!persistReady) uo_init_persistent();
    uo_unload();
    heroSynced = false;      // the stand-in takes the Knight's new position at the end of this room's first update
    curBank = 0;
    snprintf(roomName, sizeof roomName, "%s", room);
    if (!load_bank(0, room)) { loaded = persistReady; return false; }
    uo_spriteset = banks[0].set.loaded ? &banks[0].set : NULL;
    loaded = true;
    UObj **tree; int n;
    if (instantiate(0, true, 0, 0, 0, &tree, &n)) {
        keep_tree(tree);
        for (int i = 0; i < n; i++) for (int k = 0; k < tree[i]->nfsm; k++) if (!strcmp(tree[i]->fsm[k]->name, "shop_control")) shop_setup(tree[i]);
        start_tree(tree, n);
    }
    char add[48];
    if (additive_scene(room, add, sizeof add)) {
        curBank = 2;
        if (load_bank(2, add) && instantiate(2 * TMPL_BANK, true, 0, 0, 0, &tree, &n)) { keep_tree(tree); start_tree(tree, n); }
        curBank = 0;
    }
    return true;
}

/* ---------------- dynamic terrain (gates, lifts, breakable walls...: layer 8 solid colliders of live objects) ---------------- */
#define MAX_TBOX 128
static float tbox[MAX_TBOX][4]; static UObj *tboxOwner[MAX_TBOX]; static int ntbox;
static bool tboxSoft[MAX_TBOX];        // Soft Terrain (layer 25): blocks the Knight only (bone and Deepnest battle gates,
                                       // the Ancestral Mound's Bone Gate, the Failed Champion's terrain block)
static UObj *terrainIgnore;
#define TBOX_SKIP(i) (tboxOwner[i] == terrainIgnore || (tboxSoft[i] && terrainIgnore))   /* terrainIgnore set: an enemy body is moving */
static void refresh_terrain(void) {
    ntbox = 0;
    for (int i = 0; i < nobjs && ntbox < MAX_TBOX; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || o->pseudo || (o->layer != 8 && o->layer != 25) || !uo_active(o)) continue;
        for (int k = 0; k < o->ncol && ntbox < MAX_TBOX; k++) {
            UCol *c = &o->col[k];
            if (!c->en || c->trig) continue;
            if (uo_col_aabb(o, c, &tbox[ntbox][0], &tbox[ntbox][1], &tbox[ntbox][2], &tbox[ntbox][3])) { tboxSoft[ntbox] = o->layer == 25; tboxOwner[ntbox++] = o; }
        }
    }
#ifdef __HOST__
    { static int n = 0; if (getenv("HK_TBOXDUMP") && uo_frame == atoi(getenv("HK_TBOXDUMP")) && n++ < 1) for (int i = 0; i < ntbox; i++) fprintf(stderr, "[tbox] %s (%.2f,%.2f..%.2f,%.2f)\n", tboxOwner[i]->name, tbox[i][0], tbox[i][1], tbox[i][2], tbox[i][3]); }
#endif
}
// moving platforms: how far the terrain object under the Knight's feet moved during the last physics step
static UObj *mvOwner[MAX_TBOX]; static float mvX[MAX_TBOX], mvY[MAX_TBOX], mvDX[MAX_TBOX], mvDY[MAX_TBOX]; static int nmv;
static void track_movers(void) {      // each terrain owner's motion since last step (moving platforms carry the Knight)
    static unsigned stampSeen[UO_MAX_OBJS], stampPrev[UO_MAX_OBJS]; static int prevIdx[UO_MAX_OBJS]; static unsigned gen;
    UObj *seen[MAX_TBOX]; float sx[MAX_TBOX], sy[MAX_TBOX], dx[MAX_TBOX], dy[MAX_TBOX]; int ns = 0;
    gen++;
    for (int k = 0; k < nmv; k++) { UObj *o = mvOwner[k]; if (o >= objs && o < objs + UO_MAX_OBJS) { stampPrev[o - objs] = gen; prevIdx[o - objs] = k; } }
    for (int i = 0; i < ntbox; i++) {
        UObj *o = tboxOwner[i]; bool in = o >= objs && o < objs + UO_MAX_OBJS;
        if (in) { if (stampSeen[o - objs] == gen) continue; stampSeen[o - objs] = gen; }
        else { bool dup = false; for (int k = 0; k < ns; k++) if (seen[k] == o) { dup = true; break; } if (dup) continue; }
        float x = uo_wx(o), y = uo_wy(o), ddx = 0, ddy = 0;
        if (in) { if (stampPrev[o - objs] == gen) { int k = prevIdx[o - objs]; ddx = x - mvX[k]; ddy = y - mvY[k]; } }
        else for (int k = 0; k < nmv; k++) if (mvOwner[k] == o) { ddx = x - mvX[k]; ddy = y - mvY[k]; break; }
        seen[ns] = o; sx[ns] = x; sy[ns] = y; dx[ns] = ddx; dy[ns] = ddy; ns++;
    }
    nmv = ns;
    for (int k = 0; k < ns; k++) { mvOwner[k] = seen[k]; mvX[k] = sx[k]; mvY[k] = sy[k]; mvDX[k] = dx[k]; mvDY[k] = dy[k]; }
}
static void terrain_reset(void) { ntbox = 0; nmv = 0; nhc = 0; }
bool uo_platform_delta(float x0, float x1, float feetY, float *dx, float *dy) {
    for (int i = 0; i < ntbox; i++) {
        if (x1 <= tbox[i][0] || x0 >= tbox[i][2] || fabsf(feetY - tbox[i][3]) > 0.15f) continue;
        for (int k = 0; k < nmv; k++) if (mvOwner[k] == tboxOwner[i] && (mvDX[k] != 0 || mvDY[k] != 0)) { *dx = mvDX[k]; *dy = mvDY[k]; return true; }
    }
    return false;
}
// the Knight's body against solid terrain objects: OnCollisionEnter2D / Exit2D for the objects' components and FSMs
static void hero_contacts_step(void) {
    if (!heroObj) { nhc = 0; return; }
    float x0, y0, x1, y1; uo_hero_box(&x0, &y0, &x1, &y1);
    const float e = 0.08f;
    UObj *now[32]; int side[32], nn = 0;
    for (int i = 0; i < ntbox && nn < 32; i++) {
        float *b = tbox[i];
        if (!(x0 < b[2] + e && x1 > b[0] - e && y0 < b[3] + e && y1 > b[1] - e)) continue;
        UObj *o = tboxOwner[i]; bool dup = false;
        for (int k = 0; k < nn; k++) if (now[k] == o) dup = true;
        if (dup) continue;
        float pt = fabsf(y0 - b[3]), pb = fabsf(y1 - b[1]), pl = fabsf(x1 - b[0]), pr = fabsf(x0 - b[2]);
        int sd = 2; float m = pt;
        if (pb < m) { m = pb; sd = 3; } if (pl < m) { m = pl; sd = 0; } if (pr < m) { m = pr; sd = 1; }
        now[nn] = o; side[nn++] = sd;
    }
    for (int k = 0; k < nn; k++) {
        bool was = false; for (int q = 0; q < nhc; q++) if (hcOwner[q] == now[k]) was = true;
        if (!was) { curBank = now[k]->bank; comp2_hero_touch(now[k], side[k], 0); act_collision_hero(now[k], 0); }
    }
    for (int q = 0; q < nhc; q++) {
        bool still = false; for (int k = 0; k < nn; k++) if (now[k] == hcOwner[q]) still = true;
        if (!still && hcOwner[q]->used && !hcOwner[q]->destroyed) { curBank = hcOwner[q]->bank; comp2_hero_touch(hcOwner[q], 2, 2); act_collision_hero(hcOwner[q], 2); }
    }
    nhc = nn; memcpy(hcOwner, now, sizeof(UObj *) * nn);
}
// a raycast narrows the terrain boxes to the ones its bounds touch first (it tests one point per 0.02 units, and the
// Abyss core's ~100 boxes made its line-of-sight checks cost a third of the frame on the 3DS)
static short tcand[MAX_TBOX]; static int ntcand = -1;
void uo_terrain_narrow(bool on, float x0, float y0, float x1, float y1) {
    ntcand = -1; if (!on) return;
    ntcand = 0;
    for (int i = 0; i < ntbox; i++)
        if (!TBOX_SKIP(i) && x0 < tbox[i][2] && x1 > tbox[i][0] && y0 < tbox[i][3] && y1 > tbox[i][1]) tcand[ntcand++] = (short)i;
}
bool uo_terrain_hit(float x0, float y0, float x1, float y1) {
    if (ntcand >= 0) {
        for (int k = 0; k < ntcand; k++) { int i = tcand[k];
            if (x0 < tbox[i][2] && x1 > tbox[i][0] && y0 < tbox[i][3] && y1 > tbox[i][1]) return true; }
        return false;
    }
    for (int i = 0; i < ntbox; i++)
        if (!TBOX_SKIP(i) && x0 < tbox[i][2] && x1 > tbox[i][0] && y0 < tbox[i][3] && y1 > tbox[i][1]) return true;
    return false;
}

/* ---------------- physics ---------------- */
static int probe(float x0, float y0, float x1, float y1) {
    int m = 0; const float e = 0.03f, in = 0.02f;
    if (w_solid_box(x0 + in, y0 - e, x1 - in, y0)) m |= 1;
    if (w_solid_box(x0 + in, y1, x1 - in, y1 + e)) m |= 2;
    if (w_solid_box(x0 - e, y0 + 0.06f, x0, y1 - in)) m |= 4;
    if (w_solid_box(x1, y0 + 0.06f, x1 + e, y1 - in)) m |= 8;
    return m;
}

// Unity: solid colliders on children that have no Rigidbody2D of their own belong to the parent's body (the Hollow
// Knight's root collider is off; his "Colliders/Idle" child holds him on the floor, and he fell out of the arena)
static bool layers_collide(int a, int b);
static bool child_body_aabb(UObj *o, float *x0, float *y0, float *x1, float *y1, int depth) {
    bool any = false;
    for (UObj *ch = o->child; ch && depth < 3; ch = ch->next) {
        if (ch->destroyed || ch->rb.has || !uo_active(ch)) continue;
        for (int i = 0; i < ch->ncol; i++) {
            float a, b, c, d;
            if (ch->col[i].trig || !ch->col[i].en || !layers_collide(ch->layer & 31, 8) || !uo_col_aabb(ch, &ch->col[i], &a, &b, &c, &d)) continue;
            if (!any) { *x0 = a; *y0 = b; *x1 = c; *y1 = d; any = true; }
            else { *x0 = fminf(*x0, a); *y0 = fminf(*y0, b); *x1 = fmaxf(*x1, c); *y1 = fmaxf(*y1, d); }
        }
        float a, b, c, d;
        if (child_body_aabb(ch, &a, &b, &c, &d, depth + 1)) {
            if (!any) { *x0 = a; *y0 = b; *x1 = c; *y1 = d; any = true; }
            else { *x0 = fminf(*x0, a); *y0 = fminf(*y0, b); *x1 = fmaxf(*x1, c); *y1 = fmaxf(*y1, d); }
        }
    }
    return any;
}
static void body_step(UObj *o, float dt) {
    URb *rb = &o->rb;
    terrainIgnore = o;
    rb->prevContacts = rb->contacts;
    if (!rb->sim) return;
    if (rb->kin) {   // kinematic: velocity only
        float x = uo_wx(o) + rb->vx * dt, y = uo_wy(o) + rb->vy * dt;
        uo_set_world_pos(o, x, y);
        return;
    }
    if (rb->asleep) {
        // Box2D puts a body that has rested for 0.5 s to sleep; a velocity change, a script moving it, or losing
        // the ground under it wakes it. False Knight's armour lies still after its collider is switched off for the
        // death steam (instead of dropping through the floor), and resting debris (his arena keeps 150 rubble
        // pieces waiting under the floor) costs nothing while it sleeps
        if (rb->vx != 0 || rb->vy != 0 || fabsf(uo_wx(o) - rb->sleepX) > 1e-4f || fabsf(uo_wy(o) - rb->sleepY) > 1e-4f) rb->asleep = false;
        else {
            bool anySolid = false; for (int i = 0; i < o->ncol; i++) if (!o->col[i].trig && o->col[i].en) anySolid = true;
            if (!anySolid) { rb->contacts = rb->prevContacts; return; }
            if (++rb->sleepChk < 15) { rb->contacts = rb->prevContacts; return; }     // look at the ground now and then
            rb->sleepChk = 0;
            float bx0, by0, bx1, by1;
            if (uo_body_aabb(o, &bx0, &by0, &bx1, &by1) && (probe(bx0, by0, bx1, by1) & 1)) { rb->contacts = rb->prevContacts; return; }
            rb->asleep = false; rb->restT = 0;
        }
    }
    rb->vy -= PHYS_G * rb->g * dt;
    rb->pvx = rb->vx; rb->pvy = rb->vy;
    if (rb->angVel != 0) { o->rz += rb->angVel * dt; if (rb->contacts & 1) rb->angVel *= 0.9f; if (fabsf(rb->angVel) < 1) rb->angVel = 0; }
    float x0, y0, x1, y1;
    // only solid colliders collide with terrain
    bool solid = false; for (int i = 0; i < o->ncol; i++) if (!o->col[i].trig && o->col[i].en) solid = true;
    if (solid) uo_body_aabb(o, &x0, &y0, &x1, &y1);
    else solid = child_body_aabb(o, &x0, &y0, &x1, &y1, 0);
    if (!solid) { uo_set_world_pos(o, uo_wx(o) + rb->vx * dt, uo_wy(o) + rb->vy * dt); return; }
    float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hw = (x1 - x0) / 2, hh = (y1 - y0) / 2;
    float ox = uo_wx(o) - cx, oy = uo_wy(o) - cy;
    if (!w_unstick_box(&cx, &cy, hw, hh, 0.5f)) {
        // standing on the floor with a moving terrain object shut into it (a battle gate closing on a Watcher Knight):
        // Box2D pushes the body out sideways; it used to stay wedged, unable to move or land, and the fight never ended
        float tx = cx, ty = cy;
        if ((probe(cx - hw, cy - hh, cx + hw, cy + hh) & 1) && w_unstick_box(&tx, &ty, hw, hh, hw + 0.5f) && fabsf(ty - cy) < 0.01f) { cx = tx; cy = ty; }
        else if (w_solid_box(cx - hw, cy - hh, cx + hw, cy + hh) && (probe(cx - hw, cy - hh, cx + hw, cy + hh) & 1)) {
            // sunk into the floor: Hornet's air dash sets her down at her FSM's "Floor Y" (27.55 in Kingdom's Edge),
            // a unit below the arena floor, and Unity's solver pushes her back up out of it, 0.2 a step (Box2D's
            // b2_maxLinearCorrection). Here she stayed wedged in the ground for the rest of the fight
            // In a corner (the arena's battle gate beside her) straight up isn't free either: the nearest free spot up
            // and to one side, the way Box2D pushes a body out of two shapes at once
            float bd = 1e9f, bx = 0, by = 0;
            for (float d = 0.0625f; d <= 2 * hh + 0.5f && bd > 1e8f; d += 0.0625f)
                for (float s = 0; s <= hw + 0.5f; s += 0.0625f) {
                    if (!w_solid_box(cx - hw + s, cy - hh + d, cx + hw + s, cy + hh + d)) { bd = d + s; bx = s; by = d; break; }
                    if (s > 0 && !w_solid_box(cx - hw - s, cy - hh + d, cx + hw - s, cy + hh + d)) { bd = d + s; bx = -s; by = d; break; }
                }
            if (bd < 1e8f) { cy += fminf(by, 0.2f); cx += bx > 0 ? fminf(bx, 0.2f) : fmaxf(bx, -0.2f); }
        }
    }
    if (w_solid_box(cx - hw + 0.02f, cy - hh + 0.02f, cx + hw - 0.02f, cy + hh - 0.02f) && !(probe(cx - hw, cy - hh, cx + hw, cy + hh) & 1)) {
        // still deep inside terrain with nothing under it (a boss dropping through the ceiling, like False Knight):
        // Unity's solver lets it fall out, so move it freely until it's clear
        uo_set_world_pos(o, uo_wx(o) + rb->vx * dt, uo_wy(o) + rb->vy * dt);
        rb->contacts = 0; rb->collidedThisStep = false;
        return;
    }
    bool grounded = (rb->contacts & 1) && rb->vy <= 0.01f;
    int fl = w_move_box(&cx, &cy, hw, hh, rb->vx * dt, rb->vy * dt, grounded ? 0.3f : 0);
    if (grounded && !(fl & 2) && !(fl & 4)) { float cy2 = cy; int f2 = w_move_box(&cx, &cy2, hw, hh, 0, -0.3f, 0); if (f2 & 2) { cy = cy2; fl |= 2; } }
    uo_set_world_pos(o, cx + ox, cy + oy);
    if (fl & 1) rb->vx = 0;
    if ((fl & 2) && rb->vy < 0) rb->vy = 0;
    if ((fl & 4) && rb->vy > 0) rb->vy = 0;
    rb->contacts = probe(cx - hw, cy - hh, cx + hw, cy + hh);
    rb->collidedThisStep = rb->contacts != 0;
    if ((rb->contacts & 1) && fabsf(rb->vx) < 0.05f && fabsf(rb->vy) < 0.05f) { rb->restT += dt; if (rb->restT >= 0.5f) { rb->asleep = true; rb->vx = rb->vy = 0; rb->sleepX = uo_wx(o); rb->sleepY = uo_wy(o); rb->sleepChk = 0; } }
    else rb->restT = 0;
    if (rb->fric > 0 && (rb->contacts & 1)) {   // sliding on the ground: friction stops it (corpses slid along forever)
        float d = rb->fric * PHYS_G * fmaxf(rb->g, 0.1f) * dt;
        rb->vx = fabsf(rb->vx) <= d ? 0 : rb->vx - copysignf(d, rb->vx);
    }
    if (rb->contacts) {
        bool enter = (rb->contacts & ~rb->prevContacts) != 0;
        for (int k = 0; k < o->ncomp; k++) comp_collision(o, &o->comp[k], enter);
        act_collision_notify(o, enter ? 0 : 1);
    } else if (rb->prevContacts) act_collision_notify(o, 2);
}

/* ---------------- triggers (OnTriggerEnter2D / Stay2D / Exit2D) ---------------- */
// Hollow Knight's layer collision matrix, for the layers enemies use
static bool layers_collide(int a, int b) {
    if (a > b) { int t = a; a = b; b = t; }
    switch (a) {
        case 0: return b != 13 && b != 14 && b != 15 && b != 17 && b != 20;
        case 8: return b == 9 || b == 11 || b == 12 || b == 14 || b == 16 || b == 17 || b == 18 || b == 19 || b == 26;
        case 9: return b == 10 || b == 13 || b == 16 || b == 19;
        case 11: return b == 15 || b == 17 || b == 20;
        case 12: return b == 17 || b == 20;
        case 16: return b == 17;
        case 17: return b == 19 || b == 22 || b == 23;
        case 20: return b == 22 || b == 23;
        default: return false;
    }
}
static bool wants_triggers(UObj *o) {
    for (int k = 0; k < o->nfsm; k++) if (o->fsm[k]->wantsTriggers && o->fsm[k]->started) return true;
    return o->ncomp && comp2_wants_triggers(o);
}
static bool objs_touch(UObj *a, UObj *b) {
    for (int i = 0; i < a->ncol; i++) {
        if (!a->col[i].en) continue;
        float ax0, ay0, ax1, ay1; if (!uo_col_aabb(a, &a->col[i], &ax0, &ay0, &ax1, &ay1)) continue;
        for (int j = 0; j < b->ncol; j++) {
            if (!b->col[j].en || !(a->col[i].trig || b->col[j].trig)) continue;
            if (uo_col_overlap_box(b, &b->col[j], ax0, ay0, ax1, ay1)) return true;
        }
    }
    return false;
}
static bool touches_terrain(UObj *a) {
    for (int i = 0; i < a->ncol; i++) {
        if (!a->col[i].en || !a->col[i].trig) continue;
        float x0, y0, x1, y1; if (uo_col_aabb(a, &a->col[i], &x0, &y0, &x1, &y1) && w_solid_box(x0, y0, x1, y1)) return true;
    }
    return false;
}
static float bbX0[UO_MAX_OBJS], bbY0[UO_MAX_OBJS], bbX1[UO_MAX_OBJS], bbY1[UO_MAX_OBJS]; static unsigned char bbOk[UO_MAX_OBJS];
static void triggers_step(void) {
#ifdef __HOST__
    { static int once; if (getenv("HK_WAKEDBG") && uo_frame >= 3 && once < 6) for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i];
        if (!o->used || strcmp(o->name, getenv("HK_WAKEDBG"))) continue;
        once++;
        fprintf(stderr, "[wakedbg] %s layer=%d ncol=%d ncomp=%d nfsm=%d active=%d wants=%d", o->name, o->layer, o->ncol, o->ncomp, o->nfsm, uo_active(o), wants_triggers(o));
        for (int k = 0; k < o->ncol; k++) fprintf(stderr, " col%d(en=%d trig=%d)", k, o->col[k].en, o->col[k].trig);
        for (int k = 0; k < o->ncomp; k++) fprintf(stderr, " comp%d=%d", k, o->comp[k].type);
        fprintf(stderr, " hero-layer-collide=%d\n", layers_collide(o->layer & 31, 9)); } }
#endif
    // broadphase: every live object's collider bounds, once per step
    for (int j = 0; j < nobjs; j++) {
        UObj *b = &objs[j]; bbOk[j] = 0;
        if (!b->used || b->destroyed || b->ncol == 0 || !uo_active(b)) continue;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int k = 0; k < b->ncol; k++) {
            if (!b->col[k].en) continue;
            float a0, b0, a1, b1; if (!uo_col_aabb(b, &b->col[k], &a0, &b0, &a1, &b1)) continue;
            if (a0 < x0) x0 = a0; if (b0 < y0) y0 = b0; if (a1 > x1) x1 = a1; if (b1 > y1) y1 = b1;
        }
        if (x1 < x0) continue;
        bbX0[j] = x0; bbY0[j] = y0; bbX1[j] = x1; bbY1[j] = y1; bbOk[j] = 1;
    }
    // candidates (objects with a collider box), split by the layers they can touch: an object only looks at those
    // (False Knight's arena keeps 150 rubble pieces that each listen for the Knight: testing every pair made the
    // room run at a third of the frame rate on a 3DS)
    static short lcand[32][UO_MAX_OBJS]; static int lcount[32]; static unsigned char lbuilt[32];
    memset(lbuilt, 0, sizeof lbuilt);
    for (int i = 0; i < nobjs; i++) {
        UObj *a = &objs[i];
        if (!bbOk[i] || a->destroyed || a->pseudo || !wants_triggers(a)) { if (a->used) a->nov = 0; continue; }
        int la = a->layer & 31;
        if (!lbuilt[la]) {
            lbuilt[la] = 1; lcount[la] = 0;
            for (int j = 0; j < nobjs; j++) if (bbOk[j] && layers_collide(la, objs[j].layer)) lcand[la][lcount[la]++] = (short)j;
        }
        UObj *now[UO_MAX_OV]; int nn = 0;
        for (int q = 0; q < lcount[la] && nn < UO_MAX_OV; q++) {
            int j = lcand[la][q];
            if (!bbOk[j] || j == i) continue;
            if (bbX0[j] > bbX1[i] || bbX1[j] < bbX0[i] || bbY0[j] > bbY1[i] || bbY1[j] < bbY0[i]) continue;
            UObj *b = &objs[j];
            if (b->destroyed || !b->used) continue;
            if (objs_touch(a, b)) now[nn++] = b;
        }
        if (nn < UO_MAX_OV && layers_collide(a->layer, 8) && touches_terrain(a)) now[nn++] = &terrainObj;
        curBank = a->bank;
        UObj *prev[UO_MAX_OV]; int np = a->nov; memcpy(prev, a->ov, sizeof(UObj *) * np);
        a->nov = nn; memcpy(a->ov, now, sizeof(UObj *) * nn);
        for (int k = 0; k < nn; k++) {
            bool was = false; for (int q = 0; q < np; q++) if (prev[q] == now[k]) was = true;
#ifdef __HOST__
            if (!was && getenv("HK_TRIGTRACE")) fprintf(stderr, "[trig %d] %s <- %s (%s)\n", uo_frame, a->name, now[k]->name, now[k]->tag);
#endif
            act_trigger_notify(a, now[k], was ? 1 : 0);
            if (a->ncomp && !a->destroyed && uo_active(a)) comp2_trigger(a, now[k], was ? 1 : 0);
            if (a->destroyed || !uo_active(a)) break;
        }
        for (int q = 0; q < np && !a->destroyed; q++) {
            bool still = false; for (int k = 0; k < nn; k++) if (prev[q] == now[k]) still = true;
            if (!still && prev[q]->used && !prev[q]->destroyed && uo_active(prev[q])) { act_trigger_notify(a, prev[q], 2); if (a->ncomp && !a->destroyed) comp2_trigger(a, prev[q], 2); }
        }
    }
}
// FSMs move / turn / push the Knight through its stand-in object (SetPosition, SetScale, SetVelocity2d on "Hero"):
// changes made since the last sync are written back to the HeroController before re-syncing
static void hero_writeback(void) {
    if (!heroObj || !heroSynced) return;
    if (heroObj->lx != hsx || heroObj->ly != hsy) hero_set_position(P.x + heroObj->lx - hsx, P.y + heroObj->ly - hsy);
    if (heroObj->rb.vx != hvx || heroObj->rb.vy != hvy) { P.vx = heroObj->rb.vx; P.vy = heroObj->rb.vy; }
    if (heroObj->sx != hscale && heroObj->sx != 0) hero_face(heroObj->sx < 0);   // HK: scale.x -1 = facing right
}
static void hero_sync(void) {
    if (!heroObj) return;
    hero_writeback();
    float x0, y0, x1, y1;
    uo_hero_box(&x0, &y0, &x1, &y1);
    // the Knight's transform sits above its box centre like in the game (HeroController collider offset), so scripts that
    // read or follow the hero's position (range detectors that follow the hero, aiming, spawns) see the game's numbers
    heroObj->lx = (x0 + x1) / 2; heroObj->ly = (y0 + y1) / 2 - COL_OFF_Y; heroObj->col[0].oy = COL_OFF_Y;
    // + Box2D's shape skin (polygon radius 0.01 on each shape): triggers touching the Knight's box edge count, like the
    // Crystal Peak item stand whose inspect region starts 0.02 above his head when he stands on the floor
    heroObj->col[0].w = x1 - x0 + 0.04f; heroObj->col[0].h = y1 - y0 + 0.04f;
    heroObj->rb.vx = P.vx; heroObj->rb.vy = P.vy;
    heroObj->sx = P.facing > 0 ? -1 : 1;
    hsx = heroObj->lx; hsy = heroObj->ly; hvx = heroObj->rb.vx; hvy = heroObj->rb.vy; hscale = heroObj->sx; heroSynced = true;
    uo_hero_hurtbox(&x0, &y0, &x1, &y1);
    heroBoxObj->lx = (x0 + x1) / 2; heroBoxObj->ly = (y0 + y1) / 2; heroBoxObj->col[0].w = x1 - x0; heroBoxObj->col[0].h = y1 - y0;
    bool act = uo_hero_slash_box(&x0, &y0, &x1, &y1);
    slashObj->activeSelf = act;
    if (act && slashObj->nfsm) {   // damages_enemy direction: 0 right, 90 up, 180 left, 270 down
        FVar *d = fsm_var(slashObj->fsm[0], "direction");
        if (d) d->f = P.attackDir == 1 ? 90 : P.attackDir == 2 ? 270 : (P.facing > 0 ? 0 : 180);
    }
    if (act) { slashObj->lx = (x0 + x1) / 2; slashObj->ly = (y0 + y1) / 2; slashObj->col[0].w = x1 - x0; slashObj->col[0].h = y1 - y0; }
}

void uo_fixed(float dt) {
    extern void w_edge_walls(bool on); w_edge_walls(true);
    if (!loaded) return;
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || !uo_active(o)) continue;
        curBank = o->bank;
        for (int k = 0; k < o->ncomp; k++) if (o->comp[k].en) comp_fixed(o, &o->comp[k], dt);
        for (int k = 0; k < o->nfsm; k++) if (o->fsm[k]->enabled) fsm_fixed(o->fsm[k], dt);
    }
    refresh_terrain();
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || !o->rb.has || !uo_active(o)) continue;
        body_step(o, dt);
    }
    terrainIgnore = NULL;
    refresh_terrain();
    track_movers();
    extern void comps_fixed_world(float dt);
    hero_sync();
    hero_contacts_step();
    comps_fixed_world(dt);
    triggers_step();
    fsm_flush();
    curBank = 0;
}

void uo_update(float dt) {
    extern void w_edge_walls(bool on); w_edge_walls(true);
    if (!loaded) return;
    uo_frame++; uo_time += dt;
#ifdef __HOST__
    if (uo_frame % 120 == 0) uo_polytest();
#endif
#ifdef __HOST__
    {   extern bool hm_died(UObj *);   // test: HK_KILLALL=N kills every live enemy every N frames (exercises boss-death flow without fighting)
        static int ka = -1; if (ka < 0) ka = getenv("HK_KILLALL") ? atoi(getenv("HK_KILLALL")) : 0;
        if (ka > 0 && uo_frame % ka == 0) for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i];
            if (o->used && !o->destroyed && o->bank != 1 && uo_comp(o, C_HEALTH) && !uo_comp(o, C_CORPSE) && !hm_died(o) && uo_active(o) && fabsf(uo_wx(o) - P.x) < 16 && fabsf(uo_wy(o) - P.y) < 10) { hm_set_invincible(o, false); hm_hit(o, 9999, 0, ATK_NAIL, 1); } }   // on screen only (later waves wait parked off-screen)
    }
    {   // test: HK_HITALL="dmg:N" hits every live, hittable enemy for dmg every N frames (a fight without the bot's aim)
        // optional third field "A|B": only while some FSM on the object or its parent is in a state whose name contains A or B
        static int hd = -1, hn; static char hs[128]; if (hd < 0) { hd = 0; const char *e = getenv("HK_HITALL"); if (e) sscanf(e, "%d:%d:%127s", &hd, &hn, hs); }
        if (hd > 0 && hn > 0 && uo_frame % hn == 0) for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i];
            bool ok = !hs[0];
            for (UObj *q = o; q && !ok && (q == o || q == o->parent); q = q->parent) for (int k = 0; k < q->nfsm && !ok; k++) {
                const char *sn = fsm_state_name(q->fsm[k]); char buf[128]; strcpy(buf, hs);
                for (char *t = strtok(buf, "|"); t && !ok; t = strtok(NULL, "|")) if (sn && strstr(sn, t)) ok = true; }
            bool hittable = o->ncol == 0; for (int k = 0; k < o->ncol; k++) if (o->col[k].en) hittable = true;   // a real nail needs a collider
            float hx0, hy0, hx1, hy1;                                        // (its own, or a child's: the Hollow Knight's)
            if (!hittable && child_body_aabb(o, &hx0, &hy0, &hx1, &hy1, 0)) hittable = true;
            if (ok && hittable && o->used && !o->destroyed && o->bank != 1 && uo_comp(o, C_HEALTH) && !hm_is_dead(o) && uo_active(o)) { fprintf(stderr, "[hitall %d] %s\n", uo_frame, o->name); hm_hit(o, hd, 0, ATK_NAIL, 1); } }
    }
#endif
#ifdef __HOST__
    if (getenv("HK_STATS") && uo_frame % 600 == 0) {
        int used = 0, act = 0, nf = 0; static int namec[UO_MAX_OBJS];
        for (int i = 0; i < nobjs; i++) if (objs[i].used && !objs[i].destroyed) { used++; if (uo_active(&objs[i])) { act++; nf += objs[i].nfsm; } }
        extern size_t host_heap_used(void);
        fprintf(stderr, "[stats f%d] room=%s nobjs=%d used=%d active=%d fsms=%d heapKB=%zu\n", uo_frame, uo_room_name(), nobjs, used, act, nf, host_heap_used() / 1024);
        (void)namec;
    }
    { const char *dm = getenv("HK_OBJDUMP");       // "frame": list every active visible object at that frame
      if (dm && (atoi(dm) > 0 ? uo_frame == atoi(dm) : uo_frame % -atoi(dm) == 0)) for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i];
          if (o->used && !o->destroyed && uo_active(o) && (o->sr.en || o->an.has || o->tmp.has)) fprintf(stderr, "[dump f%d] %d bank=%d %s pos=(%.2f,%.2f) life=%.2f sr=%d mr=%d a=%.2f clip=%s hud=%d text=%s ta=%.2f srf=%d anh=%d add=%d sc=(%.2f,%.2f) tint=(%.2f,%.2f,%.2f,%.2f) srcol=(%.2f,%.2f,%.2f)\n", uo_frame, i, o->bank, o->name, uo_wx(o), uo_wy(o), o->life, o->sr.en, o->mr, o->sr.a,
              o->an.clip >= 0 && SETOF(o) ? SETOF(o)->anims[o->an.clip].name : "-", o->hud, o->tmp.has && o->tmp.text ? o->tmp.text : "-", o->tmp.a * o->tintA, o->sr.frame, o->an.has, o->additive, o->wsx, o->wsy, o->tintR, o->tintG, o->tintB, o->tintA, o->sr.r, o->sr.g, o->sr.b); } }
    { const char *tn = getenv("HK_UOTRACE");
      if (tn && uo_frame % 5 == 0) for (int i = 0; i < nobjs; i++) if (objs[i].used && !objs[i].destroyed && !strcmp(objs[i].name, tn)) {
          UObj *o = &objs[i]; float x0 = 0, y0 = 0, x1 = 0, y1 = 0; uo_body_aabb(o, &x0, &y0, &x1, &y1);
          fprintf(stderr, "[uo %d] %s pos=(%.2f,%.2f) v=(%.2f,%.2f) sx=%.2f act=%d contacts=%d clip=%s fr=%d box=(%.2f,%.2f..%.2f,%.2f) fsm=%s\n", uo_frame, o->name, uo_wx(o), uo_wy(o), o->rb.vx, o->rb.vy, o->sx, uo_active(o), o->rb.contacts,
                  o->an.clip >= 0 && SETOF(o) ? SETOF(o)->anims[o->an.clip].name : "-", o->an.frame, x0, y0, x1, y1, o->nfsm ? fsm_state_name(o->fsm[0]) : "-");
          fprintf(stderr, "    rb has=%d sim=%d kin=%d g=%.2f fsms:", o->rb.has, o->rb.sim, o->rb.kin, o->rb.g);
          for (int q = 0; q < o->nfsm; q++) fprintf(stderr, " [%s:%s%s]", o->fsm[q]->name ? o->fsm[q]->name : "?", fsm_state_name(o->fsm[q]), o->fsm[q]->enabled ? "" : " off");
          fprintf(stderr, "\n");
          for (int t = 0; t < ntbox; t++) if (tboxOwner[t] != o && x0 - 0.05f < tbox[t][2] && x1 + 0.05f > tbox[t][0] && y0 - 0.05f < tbox[t][3] && y1 + 0.05f > tbox[t][1])
              fprintf(stderr, "    touching %s (%.2f,%.2f..%.2f,%.2f)\n", tboxOwner[t]->name, tbox[t][0], tbox[t][1], tbox[t][2], tbox[t][3]); } }
#endif
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || !uo_active(o)) continue;
        o->life += dt;
        if (o->destroyT > 0) { o->destroyT -= dt; if (o->destroyT <= 0) { uo_destroy(o); continue; } }
        if (o->flash > 0) o->flash -= dt;
        curBank = o->bank;
        for (int k = 0; k < o->ncomp; k++) if (o->comp[k].en) comp_update(o, &o->comp[k], dt);
        for (int k = 0; k < o->nfsm; k++) if (o->fsm[k]->enabled) fsm_update(o->fsm[k], dt);
    }
    fsm_flush();
    if (heroObj) {     // the Knight's tk2dSpriteAnimator AnimationCompleted delegate
        bool d = P.anim.done;
        if (d && !lastHeroDone) { anim_complete(heroObj); heroReqClip = -1; }
        else if (heroReqClip >= 0 && P.anim.anim != heroReqClip) { anim_complete(heroObj); heroReqClip = -1; }   // the Knight's own animation took over: the clip counts as finished
        lastHeroDone = d; lastHeroClip = P.anim.anim;
        fsm_flush();
    }
    hero_sync();
}

void uo_late_update(float dt) {
    extern void w_edge_walls(bool on); w_edge_walls(true);
    if (!loaded) return;
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || !uo_active(o)) continue;
        curBank = o->bank;
        for (int k = 0; k < o->nfsm; k++) if (o->fsm[k]->enabled) fsm_late(o->fsm[k], dt);
        if (o->an.has && !o->pseudo) anim_update_obj(o, dt);    // tk2d animators update in LateUpdate
        if (o->uan) uan_update(o, dt);
    }
    fsm_flush();
    hero_sync();
    reap();
    curBank = 0;
}

/* ---------------- drawing ---------------- */
static float sx_of(float x) { return (x - g_camx) * PPU + TOP_W / 2; }
static float sy_of(float y) { return (g_camy - y) * PPU + TOP_H / 2; }

static bool drawable(UObj *o) {
    if (!o->used || o->destroyed || o->pseudo || !uo_active(o)) return false;
    if (o->an.has) return o->mr;
    if (o->sr.has && o->sr.en && o->sr.frame >= 0) return !(o->sr.tk && o->mrSet && !o->mr);
    return o->tmp.has && o->tmp.en && o->tmp.text && o->tmp.text[0];
}
// TextMeshPro: font size 7.2 ~ the dialogue text (14 px Perpetua); alignment = TMProOld TextAlignmentOptions
static void draw_tmp(UObj *o, float x, float y, float pxPerUnit) {
    const Font *f = o->tmp.font ? &A.fontMenu : &A.fontText;
    // TextMeshPro's size is in local units: the object's world scale applies (prompt texts are size 34 at scale 0.12,
    // boss titles 36 at 0.8)
    float ws = fabsf(o->wsy); if (ws < 1e-4f) ws = 1;
    float sc = o->tmp.size * ws / 7.2f * (pxPerUnit / 14.2f) * (o->tmp.font ? 0.9f : 1.0f);
    if (sc < 0.3f) sc = 0.3f;
    float a = o->tmp.a * o->tintA;
    if (a <= 0.01f) return;
    u32 col = C2D_Color32f(o->tmp.r * o->tintR, o->tmp.g * o->tintG, o->tmp.b * o->tintB, a);
    int h = o->tmp.align % 4, v = (o->tmp.align / 4) % 4;
    if (o->tmp.rw > 0) {      // aligned inside its RectTransform box: to the box's edge (or centre), not its pivot
        float bw = o->tmp.rw * fabsf(o->wsx) * pxPerUnit;
        x += h == 0 ? -bw * o->tmp.px : h == 2 ? bw * (1 - o->tmp.px) : bw * (0.5f - o->tmp.px);
    }
    if (o->tmp.rh > 0) {      // top-aligned text hangs from the box's top edge, bottom-aligned sits on its bottom edge
        float bh = o->tmp.rh * fabsf(o->wsy) * pxPerUnit;
        y -= v == 0 ? bh * (1 - o->tmp.py) : v == 2 ? -bh * o->tmp.py : bh * (0.5f - o->tmp.py);
    }
    // line by line so each line is aligned on its own
    int nl = 1; for (const char *c = o->tmp.text; *c; c++) if (*c == '\n') nl++;
    float lh = f->lineh * sc, ty = y - (v == 0 ? 0 : v == 1 ? nl * lh / 2 : nl * lh);
    char line[256]; const char *p = o->tmp.text;
    while (*p) {
        const char *e = strchr(p, '\n'); int L = e ? (int)(e - p) : (int)strlen(p); if (L > 255) L = 255;
        memcpy(line, p, L); line[L] = 0;
        float w = font_width(f, line) * sc;
        font_draw(f, line, h == 0 ? x : h == 1 ? x - w / 2 : x - w, ty, sc, col);
        ty += lh; p = e ? e + 1 : p + L;
    }
}
static void draw_obj_(UObj *o, float x, float y, float k);
static void draw_obj(UObj *o, float x, float y, float k) {
#ifdef __HOST__
    { static const char *hide = (const char *)1; if (hide == (const char *)1) hide = getenv("HK_HIDEOBJ");   // "name|name": don't draw these
      if (hide) { const char *p = strstr(hide, o->name); size_t n = strlen(o->name);
        if (p && (p == hide || p[-1] == '|') && (p[n] == 0 || p[n] == '|')) return; } }
    { const char *dl = getenv("HK_DRAWLOG");      // "text": log the drawing of objects whose name contains it
      if (dl && strstr(o->name, dl)) { float z = 0; for (UObj *q = o; q; q = q->parent) z += q->lz;
        fprintf(stderr, "[draw f%d] %s at (%.0f,%.0f) w=(%.2f,%.2f) hero=(%.2f,%.2f) z=%.3f sc=(%.2f,%.2f) sr=%d a=%.2f add=%d\n", uo_frame, o->name, x, y, uo_wx(o), uo_wy(o), uo_hero_x(), uo_hero_y(), z, o->wsx, o->wsy, o->sr.has && o->sr.en, o->sr.a * o->tintA, o->additive); } }
#endif
    if (o->additive) { gfx_additive(true); draw_obj_(o, x, y, k); gfx_additive(false); }
    else draw_obj_(o, x, y, k);
}
static void draw_obj_(UObj *o, float x, float y, float k) {      // k: screen scale (1 world, HUD ppu / 16 for HUD)
    float scx = o->wsx * k, scy = o->wsy * k;
    if (o->an.has) {
        const Anim *a = clipp(o); if (!a || a->count == 0) return;
        int fr = o->an.frame < a->count ? o->an.frame : a->count - 1;
        SpriteSet *ss = SETOF(o);
        int gf = ss->animFrames[a->first + fr];
        float r = o->tintR, g = o->tintG, b = o->tintB, al = o->tintA;
        if (o->sr.has) { r *= o->sr.r; g *= o->sr.g; b *= o->sr.b; al *= o->sr.a; }
        if (o->sr.lit && !o->hud) { r *= uo_ambient[0]; g *= uo_ambient[1]; b *= uo_ambient[2]; }
        spr_draw_col(ss, gf, x, y, scx, scy, o->wrz, r, g, b, al, o->flash > 0 ? 0.75f : 0);
    } else if (o->sr.has && o->sr.en && o->sr.frame >= 0 && !o->sr.cutout) {
        const SpriteSet *ss = o->sr.set == 1 ? game_icons() : SETOF(o);
        if (!ss) return;
        float a = o->sr.a * o->tintA;
        float r = o->sr.r * o->tintR, g = o->sr.g * o->tintG, b = o->sr.b * o->tintB;
        if (o->sr.lit && !o->hud) { r *= uo_ambient[0]; g *= uo_ambient[1]; b *= uo_ambient[2]; }
        if (a > 0.01f) spr_draw_col(ss, o->sr.frame, x, y, o->sr.fx ? -scx : scx, o->sr.fy ? -scy : scy, o->wrz, r, g, b, a, o->flash > 0 ? 0.75f : 0);
    }
    if (o->tmp.has && o->tmp.en && o->tmp.text) draw_tmp(o, x, y, 16 * k);
}

// world z of an object (Unity: larger z is further away); parallax factor of the perspective camera at CAM_D
#define CAM_D 38.1f
static float obj_z(UObj *o) { float z = 0; for (; o; o = o->parent) z += o->lz; return z; }
static float par_f(float z) { float d = CAM_D + z; return CAM_D / (d < 1 ? 1 : d); }
// draws the room objects whose parallax factor is in [flo, fhi): the gameplay plane (0.98..1.02) between the room's
// back and front layers, background and foreground objects in between the baked layers they belong with
// one sorted list of the drawable world objects per frame (far to near); every parallax band draws a slice of it
static UObj *dOrder[UO_MAX_OBJS]; static float dZ[UO_MAX_OBJS]; static int dN, dIdx[UO_MAX_OBJS]; static bool dBegan, dValid;
void uo_log_stats(void) {
    int used = 0, act = 0; for (int i = 0; i < nobjs; i++) if (objs[i].used && !objs[i].destroyed) { used++; if (uo_active(&objs[i])) act++; }
    hk_log("objs: room %s, %d slots, %d live, %d active, %d drawn", uo_room_name(), nobjs, used, act, dN);
}
void uo_draw_begin(void) { dBegan = true; dValid = false; }
static int dcmp(const void *a, const void *b) {
    int i = *(const int *)a, j = *(const int *)b;
    if (dZ[i] != dZ[j]) return dZ[i] > dZ[j] ? -1 : 1;
    return i - j;
}
// The Knight's darkness (Vignette / Darkness Control, a dark room's vignette): Unity draws it in sorting layer 13,
// after everything (the foreground silhouettes too), except 'Darkness BG', the plate that dims the scene behind the
// Knight and not the Knight. By its depth (z -1) it landed in front of him: in a dark room he was dimmed to a grey ghost,
// and the vignette went behind the foreground layers, which stayed bright at the screen edges.
static int draw_pass(UObj *o) {
    if (o->pass) return o->pass;
    const char *pn = o->parent ? o->parent->name : "";
    if (o->bank == 1 && !strcmp(o->name, "Darkness BG")) o->pass = 2;
    // 'white_light_donut' (always on, sorting layer 13) is on a material only the Knight uses, missing from the export;
    // drawn additively it put a bright ring over everything around him, black terrain included, which the game
    // doesn't show: like the exits' cutouts it shapes the darkness, it isn't drawn (pass 4)
    else if (o->bank == 1 && !strncmp(o->name, "white_light_donut", 17)) o->pass = 4;
    else if (o->bank == 1 && (!strcmp(o->name, "Vignette") || !strcmp(o->name, "Darkness FG") ||
                               (!strncmp(o->name, "black_solid", 11) && !strcmp(pn, "Darkness Border")))) o->pass = 3;
    else o->pass = 1;
    return o->pass;
}
static UObj *lastO[16]; static float lastZ[16]; static int lastN;
static void build_draw_list(void) {
    static UObj *tmpO[UO_MAX_OBJS]; static float tmpZ[UO_MAX_OBJS]; int n = 0;
    lastN = 0;
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (o->hud || !drawable(o)) continue;
        int ps = draw_pass(o);
        if (ps == 4) continue;
        if (ps == 3) { if (lastN < 16) { lastZ[lastN] = obj_z(o); lastO[lastN++] = o; } continue; }
        tmpZ[n] = ps == 2 ? 0.0f : obj_z(o); tmpO[n] = o; n++;
    }
    for (int i = 0; i < n; i++) { dIdx[i] = i; dZ[i] = tmpZ[i]; }
    qsort(dIdx, n, sizeof(int), dcmp);
    static float sz[UO_MAX_OBJS];
    for (int i = 0; i < n; i++) { dOrder[i] = tmpO[dIdx[i]]; sz[i] = tmpZ[dIdx[i]]; }
    memcpy(dZ, sz, n * sizeof(float)); dN = n;
    dValid = dBegan;
}
void uo_draw_range(float camx, float camy, float flo, float fhi) {
    if (!loaded) return;
    g_camx = camx; g_camy = camy;
    if (!dValid) build_draw_list();
    for (int i = 0; i < dN; i++) {      // z falls along the list, so F rises: the band is one run of it
        float F = par_f(dZ[i]);
        if (F < flo) continue;
        if (F >= fhi) break;
        UObj *o = dOrder[i];
        if (o->destroyed || !o->used) continue;
        uo_world(o);
        float x = (o->wx - camx) * PPU * F + TOP_W / 2, y = (camy - o->wy) * PPU * F + TOP_H / 2;
        if (x < -300 || x > TOP_W + 300 || y < -300 || y > TOP_H + 300) continue;
        draw_obj(o, x, y, F);
    }
}
void uo_draw(float camx, float camy, float alpha) { (void)alpha; uo_draw_range(camx, camy, 0.98f, 1.02f); }
// the objects drawn after everything else (sorting layer 13: the Knight's vignette), far to near
void uo_draw_last(float camx, float camy) {
    if (!loaded) return;
    if (!dValid) build_draw_list();
    for (int i = 1; i < lastN; i++) { UObj *t = lastO[i]; float tz = lastZ[i]; int j = i - 1;
        while (j >= 0 && lastZ[j] < tz) { lastO[j + 1] = lastO[j]; lastZ[j + 1] = lastZ[j]; j--; } lastO[j + 1] = t; lastZ[j + 1] = tz; }
    for (int i = 0; i < lastN; i++) {
        UObj *o = lastO[i];
        if (o->destroyed || !o->used) continue;
        float F = par_f(lastZ[i]);
        uo_world(o);
        float x = (o->wx - camx) * PPU * F + TOP_W / 2, y = (camy - o->wy) * PPU * F + TOP_H / 2;
        draw_obj(o, x, y, F);
    }
}

#define HUD_PPU 14.2f     // HUD camera: orthographic, ~16.9 units tall
void uo_draw_hud(void) {
    if (!loaded) return;
    static UObj *order[UO_MAX_OBJS]; int n = 0;
    for (int i = 0; i < nobjs; i++) { UObj *o = &objs[i]; if (o->hud && drawable(o)) order[n++] = o; }
    for (int i = 1; i < n; i++) { UObj *t = order[i]; int j = i - 1; while (j >= 0 && order[j]->lz < t->lz) { order[j + 1] = order[j]; j--; } order[j + 1] = t; }
    for (int i = 0; i < n; i++) {
        UObj *o = order[i];
        uo_world(o);
        draw_obj(o, TOP_W / 2 + o->wx * HUD_PPU, TOP_H / 2 - o->wy * HUD_PPU, HUD_PPU / 16);
    }
}

void uo_draw_debug(void (*rect)(float, float, float, float, u32)) {
    if (!loaded) return;
    for (int i = 0; i < nobjs; i++) {
        UObj *o = &objs[i];
        if (!o->used || o->destroyed || !uo_active(o)) continue;
        for (int k = 0; k < o->ncol; k++) {
            float x0, y0, x1, y1;
            if (!o->col[k].en || !uo_col_aabb(o, &o->col[k], &x0, &y0, &x1, &y1)) continue;
            u32 c = o->col[k].trig ? (o->layer == 13 ? 0x40FFFF00u : 0xFF00A0FFu) : 0xFF00FF00u;
            if (uo_comp(o, C_DAMAGEHERO)) c = 0xFF0000FFu;
            rect(x0, y0, x1, y1, c);
        }
    }
}
