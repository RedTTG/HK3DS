// Third batch of ported MonoBehaviours: small visual helpers (BasicSpriteAnimator, SpinSelf, SetZ, RandomRotation /
// Scale, DeactivateAfterDelay...), SoulOrb, ScuttlerControl (lifeblood / orange scuttlers), EventRegister,
// VinePlatform / VinePlatformCut, SteepSlope, EnemyKillEventListener.
#include "comps.h"
#include "comps2.h"
#include "uo.h"
#include "fsm.h"
#include "pd.h"
#include "hero.h"
#include "audio.h"
#include "world.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

UObj *comps_tree_obj(UObj *o, int idx);
void game_add_blue(int n);
void game_cam_shake(int kind);

static float fr3(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }
static float ff3(UComp *c, const char *k, float d) { return (float)jnum(jget(c->f, k), d); }
static bool fb3(UComp *c, const char *k, bool d) { return jbool(jget(c->f, k), d); }
static const char *fs3(UComp *c, const char *k, const char *d) { JVal *v = jget(c->f, k); return v && v->t == J_STR ? v->s : d; }
static UObj *ref3(UObj *o, JVal *v) { JVal *oi = v && v->t == J_OBJ ? jget(v, "o") : NULL; return oi ? comps_tree_obj(o, jint(oi, -1)) : NULL; }
static UObj *fref3(UObj *o, UComp *c, const char *k) { return ref3(o, jget(c->f, k)); }
static UObj *spawn3(UObj *o, JVal *v, float x, float y) {
    if (!v || v->t != J_OBJ) return NULL;
    JVal *p = jget(v, "prefab");
    if (p && p->t == J_STR) { int ti = uo_template_by_key(p->s); return ti >= 0 ? uo_spawn(ti, x, y, 0) : NULL; }
    UObj *q = ref3(o, v); if (q) { uo_set_world_pos(q, x, y); uo_set_active(q, true); }
    return q;
}
static const char *clip3(JVal *v) {
    if (!v || v->t != J_OBJ) return NULL;
    JVal *a = jget(v, "audio"); if (a && a->t == J_STR) return a->s;
    JVal *cl = jget(v, "Clip"); return cl ? clip3(cl) : NULL;
}
static void snd3(UObj *o, UComp *c, const char *k) {
    JVal *v = jget(c->f, k); const char *cl = clip3(v); if (!cl) return;
    uo_play_at(o, cl, (float)jnum(jget(v, "Volume"), 1), fr3((float)jnum(jget(v, "PitchMin"), 1), (float)jnum(jget(v, "PitchMax"), 1)));
}
static float wz3(UObj *o) { float z = 0; for (UObj *q = o; q; q = q->parent) z += q->lz; return z; }
static void set_wz(UObj *o, float z) { o->lz += z - wz3(o); }

typedef struct { int idx; float t; bool on; } SA;        // BasicSpriteAnimator
typedef struct { int phase; float t; int tmpl, hp; } SJ;  // SpawnJarControl (the Collector's jars)
typedef struct { float t; bool done; } TM;               // timers / once flags
typedef struct { float c0[4]; int mode; float t; } SF;   // SimpleSpriteFade
typedef struct { float dx, dy, ex, ey, xx, xy, bt; bool burrow, started, left; } CP3;   // BigCentipede / Section
typedef struct { int state; float t, speed, acc, scale; } SO;
typedef struct { int state, frame; float idle, anim, scale; } SPT;   // SpatterOrange       // SoulOrb
typedef struct { int state; float t, maxSpeed, vx, actT; bool alive, landed, reverse; int dir; } SC;   // ScuttlerControl
typedef struct { bool activated; float t; float px, py; UObj *spr; } VP;   // VinePlatform
typedef struct { bool activated, completed, glowUp, doneGlow; int state; float t; } DP;       // DreamPlant
typedef struct { bool picked, active, canPick; int state; float t; float x0, y0, x1, y1, hx, hy; } DO;   // DreamPlantOrb
static UObj *dreamPlant;
typedef struct { bool setup, running; float init[4], from[4], to[4], t, time, delay; } CFD;   // ColorFader

/* ================= ColorFader: fades the sprite / text colour between its down and up colours ================= */
static float *cf_col(UObj *o) { return o->sr.has ? &o->sr.r : o->tmp.has ? &o->tmp.r : &o->tintR; }
static void cf_rgba(UComp *c, const char *k, float d[4]) { JVal *v = jget(c->f, k); d[0] = (float)jnum(jget(v, "r"), 1); d[1] = (float)jnum(jget(v, "g"), 1); d[2] = (float)jnum(jget(v, "b"), 1); d[3] = (float)jnum(jget(v, "a"), 1); }
static void cf_setup(UObj *o, UComp *c) {
    CFD *f = c->st; if (!f || f->setup) return;
    f->setup = true;
    float *col = cf_col(o), dn[4]; cf_rgba(c, "downColour", dn);
    for (int i = 0; i < 4; i++) { f->init[i] = jbool(jget(c->f, "useInitialColour"), true) ? col[i] : 1; col[i] = dn[i] * f->init[i]; }
}
void comps_color_fade(UObj *o, bool up) {
    UComp *c = o ? uo_comp(o, C_COLORFADER) : NULL; if (!c || !c->st) return;
    CFD *f = c->st; cf_setup(o, c);
    float *col = cf_col(o);
    for (int i = 0; i < 4; i++) f->from[i] = f->init[i] != 0 ? col[i] / f->init[i] : col[i];
    cf_rgba(c, up ? "upColour" : "downColour", f->to);
    f->time = ff3(c, up ? "upTime" : "downTime", up ? 0.4f : 0.5f); f->delay = up ? ff3(c, "upDelay", 0) : 0;
    f->t = 0; f->running = true;
}
void comps_color_fade_tree(UObj *o, bool up, bool children) {
    if (!o) return;
    comps_color_fade(o, up);
    if (children) for (UObj *k = o->child; k; k = k->next) comps_color_fade_tree(k, up, true);
}
static void cf_update(UObj *o, UComp *c, float dt) {
    CFD *f = c->st; if (!f || !f->running) return;
    if (f->delay > 0) { f->delay -= dt; return; }
    f->t += dt;
    float k = f->time > 0 ? f->t / f->time : 1; if (k >= 1) { k = 1; f->running = false; }
    float *col = cf_col(o);
    for (int i = 0; i < 4; i++) col[i] = (f->from[i] + (f->to[i] - f->from[i]) * k) * f->init[i];
}

void comp3_awake(UObj *o, UComp *c) {
    switch (c->type) {
        case C_SPRANIM: c->st = calloc(1, sizeof(SA)); break;
        case C_CENTIPEDE: case C_CENTISECTION: c->st = calloc(1, sizeof(CP3)); break;
        case C_BOSSSCENE: c->st = calloc(1, sizeof(TM)); break;
        case C_SPRFADE: { SF *f = calloc(1, sizeof(SF)); c->st = f; f->c0[0] = o->sr.r; f->c0[1] = o->sr.g; f->c0[2] = o->sr.b; f->c0[3] = o->sr.a; break; }
        case C_SPINSELF: case C_SPINSIMPLE: case C_SETZ: case C_DEACTDELAY: case C_DISABLETIME: case C_RANDSCALE: case C_VINECUT: c->st = calloc(1, sizeof(TM)); break;
        case C_SOULORB: c->st = calloc(1, sizeof(SO)); break;
        case C_COLORFADER: c->st = calloc(1, sizeof(CFD)); break;
        case C_WAVE: c->st = calloc(1, sizeof(float) * 2); break;
        case C_PSRECYCLE: c->st = calloc(1, sizeof(float)); break;
        case C_SENDONENABLE: c->st = calloc(1, sizeof(int)); break;
        case C_SPAWNJAR: { SJ *j = calloc(1, sizeof(SJ)); j->tmpl = -1; c->st = j; break; }
        case C_SPATTER: c->st = calloc(1, sizeof(SPT)); ((SPT *)c->st)->scale = fr3(ff3(c, "scaleModifierMin", 0.7f), ff3(c, "scaleModifierMax", 1.3f)); break;
        case C_SCUTTLER: c->st = calloc(1, sizeof(SC)); break;
        case C_VINEPLAT: c->st = calloc(1, sizeof(VP)); break;
        case C_DREAMPLANT: c->st = calloc(1, sizeof(DP)); break;
        case C_DREAMORB: c->st = calloc(1, sizeof(DO)); break;
        case C_DREAMREACT: c->st = calloc(1, sizeof(TM)); ((TM *)c->st)->done = jbool(jget(c->f, "startSuppressed"), false); break;
        default: break;
    }
}
static void orb_vis(UObj *o, bool v) { o->mr = v; o->sr.en = v; for (UObj *k = o->child; k; k = k->next) if (k->as.has || !k->ncomp) {} }
// PersistentBoolItem state of these components (OnGetSaveState / OnSetSaveState)
bool comp3_pbi_get(UObj *o, bool *val) {
    UComp *c;
    if ((c = uo_comp(o, C_DREAMPLANT))) { *val = ((DP *)c->st)->completed; return true; }
    if ((c = uo_comp(o, C_DREAMORB))) { *val = ((DO *)c->st)->picked; return true; }
    if ((c = uo_comp(o, C_VINEPLAT))) { *val = ((VP *)c->st)->activated; return true; }
    return false;
}
bool comp3_pbi_set(UObj *o, bool val) {
    UComp *c;
    if ((c = uo_comp(o, C_DREAMPLANT))) { DP *d = c->st; d->completed = val; if (val) { d->activated = true; uo_anim_play(o, "Completed", true); UObj *dd = fref3(o, c, "dreamDialogue"); if (dd) uo_set_active(dd, true); } return true; }
    if ((c = uo_comp(o, C_DREAMORB))) { ((DO *)c->st)->picked = val; return true; }
    if ((c = uo_comp(o, C_VINEPLAT))) {
        VP *v = c->st; v->activated = val;
        if (val) { UObj *ps = fref3(o, c, "platformSprite"), *as = fref3(o, c, "activatedSprite"), *ld = fref3(o, c, "landingDetector");
            if (ps) uo_set_active(ps, false); if (as) uo_set_active(as, true); if (ld) uo_set_active(ld, false);
            UObj *cl = fref3(o, c, "collider"); if (cl) for (int i = 0; i < cl->ncol; i++) if (!cl->col[i].trig) cl->col[i].en = false; }
        return true;
    }
    return false;
}

void comp3_enable(UObj *o, UComp *c) {
    switch (c->type) {
        case C_PSRECYCLE: { float *t = c->st; if (t) *t = 1.5f; break; }    // the particles aren't simulated: recycle after their usual life
        case C_SENDONENABLE: { int *n = c->st; if (n) *n = 0; break; }
        case C_SPAWNJAR: { SJ *j = c->st; if (!j) break;      // appears above the arena, waits, then drops
            j->phase = 0; j->t = 0.5f; uo_set_world_pos(o, uo_wx(o), ff3(c, "spawnY", 106.52f));
            for (int i = 0; i < o->ncol; i++) o->col[i].en = false;
            o->sr.en = false; if (o->rb.has) { o->rb.vx = o->rb.vy = 0; }
            break; }
        case C_WAVE: {      // WaveEffectControl.OnEnable: an expanding ring that fades out (shiny items, enemy deaths)
            float *w = c->st; if (!w) break; w[0] = 0; w[1] = ff3(c, "accelStart", 5);
            if (fb3(c, "blackWave", false)) { o->sr.r = o->sr.g = o->sr.b = 0; } else if (!fb3(c, "otherColour", false)) { o->sr.r = o->sr.g = o->sr.b = 1; }
            o->sr.a = 1;
            break; }
        case C_SPRFADE: { SF *f = c->st; o->sr.r = f->c0[0]; o->sr.g = f->c0[1]; o->sr.b = f->c0[2]; o->sr.a = f->c0[3]; f->mode = fb3(c, "fadeInOnStart", false) ? 1 : 0; f->t = 0; break; }
        case C_SPRANIM: { SA *s = c->st; JVal *fr = jget(c->f, "frames"); int n = fr ? fr->n : 0;
            s->on = n > 1; s->t = 0; s->idx = (fb3(c, "startRandom", true) && n > 0) ? rand() % n : 0; break; }
        case C_SPINSIMPLE: { TM *t = c->st; if (fb3(c, "randomStartRotation", false)) o->rz = fr3(0, 360); t->t = 0.01f; t->done = false; break; }
        case C_SETZ: { TM *t = c->st; t->t = ff3(c, "delayBeforeRandomizing", 0.5f); t->done = false; break; }
        case C_SETZRAND: set_wz(o, fr3(ff3(c, "zMin", 0), ff3(c, "zMax", 0))); break;
        case C_RANDROT: o->rz = fr3(0, 360); break;
        case C_RANDROTRANGE: o->rz = fr3(ff3(c, "min", 0), ff3(c, "max", 0)); break;
        case C_RANDSCALE: if (fb3(c, "scaleOnEnable", false)) { float k = fr3(ff3(c, "minScale", 1), ff3(c, "maxScale", 1)); o->sx = o->sy = k; ((TM *)c->st)->done = true; } break;
        case C_DEACTDELAY: ((TM *)c->st)->t = ff3(c, "time", 1); break;
        case C_DISABLETIME: ((TM *)c->st)->t = ff3(c, "waitTime", 5); break;
        case C_SNAPGROUND: {
            float x0, y0, x1, y1, h;
            if (uo_body_aabb(o, &x0, &y0, &x1, &y1) || (o->ncol && uo_col_aabb(o, &o->col[0], &x0, &y0, &x1, &y1))) {
                float off = uo_wy(o) - y0;
                if (w_raycast(uo_wx(o), uo_wy(o), 0, -1, 10, &h)) uo_set_world_pos(o, uo_wx(o), uo_wy(o) - h + off);
            }
            break; }
        case C_SPATTER: { SPT *p = c->st; p->state = 0; p->idle = p->anim = 0; p->frame = 1; if (o->rb.has) o->rb.kin = false; for (int k = 0; k < o->ncol; k++) o->col[k].en = true;
            JVal *e = jat(jget(c->f, "sprites"), 0); int fr = e ? uo_sprite_frame(jstr(jget(e, "sprite"), NULL)) : -1; if (fr >= 0) o->sr.frame = fr; break; }
        case C_SOULORB: { SO *s = c->st; s->state = 0; s->t = 0; s->scale = fr3(ff3(c, "scaleModifierMin", 1), ff3(c, "scaleModifierMax", 2)); o->sr.en = true; if (o->rb.has) o->rb.kin = false; break; }
        default: break;
    }
}

// Godhome pantheon door tier (romfs/text/gg_doors.txt: scene, door x, tier; nearest door in this scene)
int gg_door_tier(UObj *d) {
    int tier = 0; float best = 1e9f, dx = uo_wx(d); FILE *tf = fopen("romfs:/text/gg_doors.txt", "rb");
    if (tf) { char ln[128], rm[48]; float x; int t; while (fgets(ln, sizeof ln, tf)) if (sscanf(ln, "%47s %f %d", rm, &x, &t) == 3 && !strcmp(rm, uo_room_name()) && fabsf(x - dx) < best) { best = fabsf(x - dx); tier = t; } fclose(tf); }
    return tier;
}
void comp3_start(UObj *o, UComp *c) {
    if (c->type == C_COLORFADER) { cf_setup(o, c); return; }
    if (c->type == C_ACTIONICON) {    // the button prompt shows the 3DS button for its action instead of the PC key
        static const char *btn[] = { "B", "Y", "R", "ZL", "A", "L", "SEL", "A", "B", "X", "Up", "Down", "Left", "Right", "ZR", "L", "R" };
        int a = (int)ff3(c, "action", 0); UObj *lb = fref3(o, c, "label");
        if (lb && a >= 0 && a < (int)(sizeof btn / sizeof *btn)) uo_set_text(lb, btn[a]);
        return;
    }
    switch (c->type) {
        case C_BOSSSEQDOOR: {   // BossSequenceDoor.Start: unlocked (tier 1, or the previous pantheon beaten) -> lock set off, unlocked set on
            int t = gg_door_tier(o); char k[32];
            bool unlocked = t <= 1; if (!unlocked) { snprintf(k, sizeof k, "pantheonComplete%d", t - 1); unlocked = pd_bool(k); }
            if (t == 5) unlocked = pd_bool("pantheonComplete4");
            snprintf(k, sizeof k, "pantheonComplete%d", t); bool done = pd_bool(k);
            UObj *ls = fref3(o, c, "lockSet"), *us = fref3(o, c, "unlockedSet"), *cd = fref3(o, c, "completedDisplay");
            if (ls) uo_set_active(ls, !unlocked);
            if (us) uo_set_active(us, unlocked);
            if (cd) uo_set_active(cd, done);
            break; }
        case C_SPINSELF: o->rz = fr3(0, 360); break;
        case C_RANDROT: o->rz = fr3(0, 360); break;
        case C_RANDROTRANGE: o->rz = fr3(ff3(c, "min", 0), ff3(c, "max", 0)); break;
        case C_RANDSCALE: if (!((TM *)c->st)->done) { float k = fr3(ff3(c, "minScale", 1), ff3(c, "maxScale", 1)); o->sx = o->sy = k; ((TM *)c->st)->done = true; } break;
        case C_SOULORB: set_wz(o, fr3(-0.1f, -0.001f)); break;
        case C_CENTIPEDE: {
            CP3 *p = c->st; float a = uo_wrz(o) * (float)M_PI / 180; p->dx = cosf(a); p->dy = sinf(a);
            UObj *en = fref3(o, c, "entry"), *ex = fref3(o, c, "exit");
            if (en) { p->ex = uo_wx(en); p->ey = uo_wy(en); } else { p->ex = uo_wx(o) - p->dx * 12; p->ey = uo_wy(o) - p->dy * 12; }
            if (ex) { p->xx = uo_wx(ex); p->xy = uo_wy(ex); } else { p->xx = uo_wx(o) + p->dx * 6; p->xy = uo_wy(o) + p->dy * 6; }
            p->started = true; o->mr = true;
            break; }
        case C_SCUTTLER: {
            SC *s = c->st; float k = fr3(1.35f, 1.5f); o->sx = o->sx < 0 ? -k : k; o->sy = k;
            s->maxSpeed = fr3(6, 9); s->alive = true; s->actT = 0.25f;
            if (fb3(c, "healthScuttler", false)) s->reverse = pd_bool("equippedCharm_27");
            if (fb3(c, "startRunning", false)) s->state = 2;
            else s->state = fb3(c, "startIdle", false) ? 0 : 1;     // 0 idle until the Knight comes, 1 falling until it lands
            break; }
        case C_DREAMPLANT: {
            DP *d = c->st; dreamPlant = o;
            if (d->completed) { const char *b = fs3(c, "playerdataBool", ""); if (b[0]) pd_set_bool(b, true); }
            break; }
        case C_DREAMORB: { DO *d = c->st; d->active = false; orb_vis(o, false); d->hx = uo_wx(o); d->hy = uo_wy(o); break; }
        case C_VINEPLAT: {
            VP *v = c->st; UObj *ed = fref3(o, c, "enemyDetector"); if (ed) uo_set_active(ed, false);
            UObj *ps = fref3(o, c, "platformSprite"); v->spr = ps; if (ps) { v->px = ps->lx; v->py = ps->ly; }
            break; }
        default: break;
    }
}

static void scuttler_run_vel(UObj *o, UComp *c, SC *s, float dt) {
    (void)dt;
    float num = (uo_hero_x() - uo_wx(o) >= 0 ? 1 : -1) * (s->reverse ? -1 : 1);
    if (num != s->dir) { s->dir = (int)num; o->sx = fabsf(o->sx) * num; }
    s->vx += 0.3f * -num;     // runs away from the Knight (towards him with Lifeblood... reversed by the charm)
    if (s->vx > s->maxSpeed) s->vx = s->maxSpeed; if (s->vx < -s->maxSpeed) s->vx = -s->maxSpeed;
    if (o->rb.has) o->rb.vx = s->vx;
    (void)c;
}

bool hm_died(UObj *o);
// EndGGBossScene: the scene's BossSceneController ends the fight (after bossesDeadWaitTime) as if every boss were dead
void comps_end_boss_scene(void) {
    for (int i = 0; i < uo_count(); i++) { UObj *o = uo_obj(i); if (!o || o->destroyed) continue;
        UComp *c = uo_comp(o, C_BOSSSCENE); if (!c || !c->st) continue;
        TM *t = c->st; if (t->done) continue;
        t->done = true; t->t = fmaxf(0.01f, ff3(c, "bossesDeadWaitTime", 5)); comps_event_register_send("GG TRANSITION OUT STATUE");
#ifdef __HOST__
        fprintf(stderr, "[boss] scene ended by EndGGBossScene\n");
#endif
    }
}
// SetSpawnJarContents: what the next jar breaks into
void comps_spawnjar_set(UObj *o, int tmpl, int hp) { UComp *c = uo_comp(o, C_SPAWNJAR); if (c && c->st) { ((SJ *)c->st)->tmpl = tmpl; ((SJ *)c->st)->hp = hp; } }
void comp3_update(UObj *o, UComp *c, float dt) {
    switch (c->type) {
        case C_SPAWNJAR: { SJ *j = c->st; if (!j) break;
            if (j->phase == 0) { j->t -= dt; if (j->t <= 0) {
                    for (int i = 0; i < o->ncol; i++) o->col[i].en = true;
                    o->sr.en = true; if (o->rb.has) { o->rb.vx = 0; o->rb.vy = -25; }
                    o->rz += 0; j->phase = 1; } }
            else if (j->phase == 1) {
                o->rz += (o->rb.vx >= 0 ? -300 : 300) * dt;
                if (o->rb.has) o->rb.vy = fminf(o->rb.vy, -25);
                float by = ff3(c, "breakY", 94.55f);
                if (uo_wy(o) <= by) {
                    uo_set_world_pos(o, uo_wx(o), by);
                    extern void game_shake(float); game_shake(0.15f);
                    for (int i = 0; i < o->ncol; i++) o->col[i].en = false;
                    o->sr.en = false; if (o->rb.has) { o->rb.vx = o->rb.vy = 0; }
                    JVal *bs = jget(jget(c->f, "breakSound"), "Clips");
                    if (bs && bs->n) { const char *cl = clip3(jat(bs, rand() % bs->n)); if (cl) uo_play_at(o, cl, 1, fr3(0.85f, 1.15f)); }
                    if (j->tmpl >= 0) {
                        UObj *e = uo_spawn(j->tmpl, uo_wx(o), by, 0);
                        if (e) { if (uo_comp(e, C_HEALTH)) hm_set_hp(e, j->hp); snprintf(e->tag, sizeof e->tag, "Boss"); }
                    }
                    j->phase = 2; j->t = 2;
                }
            } else if (j->phase == 2) { j->t -= dt; if (j->t <= 0) { j->phase = 3; uo_set_active(o, false); } }
            break; }
        case C_BOSSSCENE: {     // BossSceneController: every listed boss dead -> back to the statue after bossesDeadWaitTime
            TM *t = c->st; JVal *bl = jget(c->f, "bosses"); if (t->done && t->t < -0.5f) break;
            if (!t->done) {
                if (!bl || !bl->n) break;      // no listed bosses: the fight's own FSM ends it (EndGGBossScene)
                int left = 0;
                for (JVal *e = bl->child; e; e = e->next) { UObj *b = ref3(o, e); if (b && uo_comp(b, C_HEALTH) && !hm_died(b)) left++; }
                if (left == 0) {
#ifdef __HOST__
                    fprintf(stderr, "[boss] every listed boss is dead\n");
#endif
                    t->done = true; t->t = fmaxf(0.01f, ff3(c, "bossesDeadWaitTime", 5)); comps_event_register_send("GG TRANSITION OUT STATUE"); }
            } else if (t->t > 0) { t->t -= dt; if (t->t <= 0) {
                const char *sc = pd_str("dreamReturnScene"), *g = pd_str("bossReturnEntryGate");
                if (!strncmp(uo_room_name(), "GG_", 3) && sc[0]) game_begin_transition(sc, g[0] ? g : "door_dreamReturn");
                t->t = -1; } }
            break; }
        case C_CENTIPEDE: {
            CP3 *p = c->st; if (!p->started) break;
            float sp = ff3(c, "moveSpeed", 5);
            uo_set_world_pos(o, uo_wx(o) + p->dx * sp * dt, uo_wy(o) + p->dy * sp * dt);
            float d = uo_wx(o) * p->dx + uo_wy(o) * p->dy;
            if (!p->burrow) { if (d > p->xx * p->dx + p->xy * p->dy) { p->burrow = true; p->bt = 0; o->mr = false; } }
            else { p->bt += dt; if (p->bt > ff3(c, "burrowTime", 2)) { p->burrow = false; uo_set_world_pos(o, p->ex - p->dx * 2.6f, p->ey - p->dy * 2.6f); o->mr = true; } }
            break; }
        case C_CENTISECTION: {
            UObj *par = o->parent; UComp *pc = NULL; while (par && !(pc = uo_comp(par, C_CENTIPEDE))) par = par->parent;
            if (!pc) break; CP3 *p = pc->st, *me = c->st; if (!p->started) break;
            float d = uo_wx(o) * p->dx + uo_wy(o) * p->dy, de = p->ex * p->dx + p->ey * p->dy, dx = p->xx * p->dx + p->xy * p->dy;
            if (!me->left) { if (d > dx) { o->mr = false; me->left = true; } }
            else if (d > de - 1.75f && d < dx) { o->mr = true; me->left = false; }
            break; }
        case C_COLORFADER: cf_update(o, c, dt); break;
        case C_SENDONENABLE: {  // SendPlaymakerEventOnEnable: broadcast an event when enabled (e.g. BG QUICK OPEN for the battle
            // gates once the False Knight is beaten); sent on the 2nd and 10th frame so FSMs that start alongside have reached their listening states
            int *n = c->st; if (!n) break; (*n)++;
            if (*n == 2 || *n == 10) { const char *ev = jstr(jget(c->f, "eventName"), ""); if (ev[0]) uo_broadcast(ev); }
            break; }
        case C_PSRECYCLE: {     // ParticleSystemAutoRecycle / AutoDestroy on spawned effects (item get burst, dust, steam)
            float *t = c->st; if (!t || !o->spawned) break;
            *t -= dt; if (*t <= 0) uo_destroy(o);
            break; }
        case C_WAVE: {
            float *w = c->st; if (!w) break;
            w[0] += dt * w[1];
            w[1] *= powf(0.95f, dt * 50); if (w[1] < 0.5f) w[1] = 0.5f;      // FixedUpdate: accel *= 0.95 at 50 Hz
            float k = (1 + w[0] * 4) * ff3(c, "scaleMultiplier", 1); o->sx = k; o->sy = k;
            o->sr.a = 1 - w[0] < 0 ? 0 : 1 - w[0];
            if (w[0] > 1) { if (fb3(c, "doNotRecycle", false)) uo_set_active(o, false); else uo_destroy(o); }
            break; }
        case C_SPATTER: {      // SpatterOrange: a blood droplet stretches along its flight, splats on impact (or after 3 s), recycles
            SPT *p = c->st; if (!p) break;
            JVal *spr = jget(c->f, "sprites");
            if (p->state == 0) {
                float vx = o->rb.vx, vy = o->rb.vy, sp = sqrtf(vx * vx + vy * vy);
                if (sp > 0.01f) o->rz = atan2f(vy, vx) * 180 / (float)M_PI;
                float ny = 1 - sp * 1.4f * 0.01f, nx = 1 + sp * 1.4f * 0.01f; if (nx < 0.6f) nx = 0.6f; if (ny > 1.75f) ny = 1.75f;
                o->sx = nx * p->scale; o->sy = ny * p->scale;
                p->idle += dt;
                if (p->idle > 3 || (o->rb.has && o->rb.contacts)) {      // Impact
                    if (o->rb.contacts & 1) o->rz = 0; else if (o->rb.contacts & 2) o->rz = 180; else if (o->rb.contacts & 4) o->rz = 270; else if (o->rb.contacts & 8) o->rz = 90;
                    float k = fr3(ff3(c, "splashScaleMin", 1.5f), ff3(c, "splashScaleMax", 2)); o->sx = o->sy = k;
                    for (int q = 0; q < o->ncol; q++) o->col[q].en = false;
                    if (o->rb.has) { o->rb.kin = true; o->rb.vx = o->rb.vy = 0; }
                    JVal *e = jat(spr, 1); int fr = e ? uo_sprite_frame(jstr(jget(e, "sprite"), NULL)) : -1; if (fr >= 0) o->sr.frame = fr;
                    p->state = 1;
                }
            } else {
                p->anim += dt;
                if (p->anim >= 1.0f / fmaxf(1, ff3(c, "fps", 30))) {
                    p->anim = 0; p->frame++;
                    if (p->frame > 6) { p->state = 2; uo_destroy(o); break; }
                    JVal *e = jat(spr, p->frame); int fr = e ? uo_sprite_frame(jstr(jget(e, "sprite"), NULL)) : -1; if (fr >= 0) o->sr.frame = fr;
                }
            }
            break; }
        case C_SPRFADE: {
            SF *f = c->st; if (!f->mode) break;
            float D = fmaxf(0.01f, ff3(c, "fadeDuration", 0.5f));
            if (f->mode == 1) f->t += dt; else f->t -= dt;
            bool end = false; if (f->t >= D) { f->t = D; end = true; } if (f->t <= 0) { f->t = 0; f->mode = 0; }
            JVal *fc = jget(c->f, "fadeInColor"); float c1[4] = { (float)jnum(jget(fc, "r"), 1), (float)jnum(jget(fc, "g"), 1), (float)jnum(jget(fc, "b"), 1), (float)jnum(jget(fc, "a"), 0) };
            float k = f->t / D; o->sr.r = f->c0[0] + (c1[0] - f->c0[0]) * k; o->sr.g = f->c0[1] + (c1[1] - f->c0[1]) * k; o->sr.b = f->c0[2] + (c1[2] - f->c0[2]) * k; o->sr.a = f->c0[3] + (c1[3] - f->c0[3]) * k;
            if (end && f->mode == 1) { f->mode = 0; if (fb3(c, "recycleOnFadeIn", false)) uo_destroy(o); else if (fb3(c, "deactivateOnFadeIn", false)) uo_set_active(o, false); }
            break; }
        case C_SPRANIM: {
            SA *s = c->st; if (!s->on) break;
            JVal *fr = jget(c->f, "frames"); int n = fr ? fr->n : 0; if (n < 2) break;
            s->t += dt; float step = 1.0f / fmaxf(1, ff3(c, "fps", 30));
            while (s->t >= step) { s->t -= step; s->idx++; if (s->idx >= n) { if (!fb3(c, "looping", true)) { s->on = false; s->idx = n - 1; break; } s->idx = 0; } }
            JVal *e = jat(fr, s->idx); JVal *g = e ? jget(e, "sprite") : NULL;
            if (g && g->t == J_STR && o->sr.en) { int f = uo_sprite_frame(g->s); if (f >= 0) o->sr.frame = f; }
            break; }
        case C_SPINSELF: { TM *t = c->st; if (!t->done && o->rb.has) { o->rb.angVel = fmaxf(-720, fminf(720, o->rb.vx * ff3(c, "spinFactor", -7.5f) * 20)); t->done = true; } break; }
        case C_SPINSIMPLE: { TM *t = c->st; if (!t->done && !fb3(c, "waitForCall", false)) { t->t -= dt; if (t->t <= 0) { t->done = true; if (o->rb.has) o->rb.angVel = fmaxf(-720, fminf(720, -o->rb.vx * ff3(c, "spinFactor", 0) * 20)); } } break; }
        case C_SETZ: { TM *t = c->st; if (t->done) break; t->t -= dt; if (t->t > 0) break; t->done = true;
            float z = ff3(c, "z", 0); if (!fb3(c, "dontRandomize", false)) z = fr3(z, z + 0.001f);
            if (fb3(c, "randomizeFromStartingValue", false)) { float w = wz3(o); z = fr3(w, w + 0.001f); }
            set_wz(o, z); break; }
        case C_DEACTDELAY: { TM *t = c->st; if (t->t > 0) t->t -= dt; else uo_set_active(o, false); break; }
        case C_DISABLETIME: { TM *t = c->st; t->t -= dt; if (t->t <= 0) { const char *e = fs3(c, "sendEvent", ""); if (e[0]) { uo_send_event(o, e); t->t = 1e9f; } else uo_set_active(o, false); } break; }
        case C_SOULORB: {
            SO *s = c->st;
#ifdef __HOST__
            if (getenv("HK_ORBTRACE")) fprintf(stderr, "[orb] %s st=%d rb=%d act=%d pos %.1f %.1f v %.1f %.1f\n", o->name, s->state, o->rb.has, uo_active(o), uo_wx(o), uo_wy(o), o->rb.vx, o->rb.vy);
#endif
            if (!o->rb.has) break;
            float vx = o->rb.vx, vy = o->rb.vy, sp = sqrtf(vx * vx + vy * vy);
            if (s->state == 0) {     // flung out, slowing (the prefab's linear drag), then it zooms to the Knight
                float k = expf(-4.0f * dt); o->rb.vx *= k; o->rb.vy *= k; s->t += dt;
                if (sp < 2.5f || s->t > 0.55f) { s->state = 1; s->speed = sp * 0.3f; s->acc = 0; }
            }
            if (s->state == 1) {
                s->speed += s->acc; if (s->speed > 30) s->speed = 30; s->acc += 0.07f;
                float dx = uo_hero_x() - uo_wx(o), dy = uo_hero_y() - uo_wy(o), a = atan2f(dy, dx);
                o->rb.vx = cosf(a) * s->speed; o->rb.vy = sinf(a) * s->speed; o->rb.g = 0;
                if (sqrtf(dx * dx + dy * dy) < 0.8f) {
                    o->rb.vx = o->rb.vy = 0; o->sr.en = false; o->an.has = o->an.has;
                    sfx_play("soul_orb_collect", 0.7f);
                    if (fb3(c, "awardSoul", true)) game_add_soul(2);
                    s->state = 2; s->t = 0.4f;
                }
            } else if (s->state == 2) { s->t -= dt; if (s->t <= 0) { if (fb3(c, "dontRecycle", false)) uo_set_active(o, false); else uo_destroy(o); } break; }
            if (sp > 0.01f) o->rz = atan2f(o->rb.vy, o->rb.vx) * 180 / (float)M_PI;
            float sy = 1 - sp * ff3(c, "stretchFactor", 2) * 0.01f, sx = 1 + sp * ff3(c, "stretchFactor", 2) * 0.01f;
            if (sx > ff3(c, "stretchMaxX", 2)) sx = ff3(c, "stretchMaxX", 2); if (sy < ff3(c, "stretchMinY", 1)) sy = ff3(c, "stretchMinY", 1);
            o->sx = sx * s->scale; o->sy = sy * s->scale;
            break; }
        case C_SCUTTLER: {
            SC *s = c->st; if (s->actT > 0) s->actT -= dt;
            if (!s->alive) { if (s->state == 9) { s->t -= dt; if (s->t <= 0) { game_add_blue(1); uo_set_active(o, false); } } break; }
            if (s->state == 0) {     // startIdle: the heroAlert trigger wakes it
                UObj *ha = fref3(o, c, "heroAlert"); float x0, y0, x1, y1, hx0, hy0, hx1, hy1; uo_hero_box(&hx0, &hy0, &hx1, &hy1);
                if (ha && ha->ncol && uo_col_aabb(ha, &ha->col[0], &x0, &y0, &x1, &y1) && hx0 < x1 && hx1 > x0 && hy0 < y1 && hy1 > y0) {
                    uo_anim_play(o, fs3(c, "landAnim", "Land"), true); s->state = 3; s->t = uo_anim_clip_len(o, fs3(c, "landAnim", "Land"));
                }
            } else if (s->state == 1) {      // falling: lands on the ground
                if (o->rb.has && (o->rb.contacts & 1)) { uo_anim_play(o, fs3(c, "landAnim", "Land"), true); s->state = 3; s->t = uo_anim_clip_len(o, fs3(c, "landAnim", "Land")); }
            } else if (s->state == 3) { s->t -= dt; if (s->t <= 0) { s->state = 2; uo_anim_play(o, fs3(c, "runAnim", "Run"), true); s->vx = o->rb.vx; } }
            else if (s->state == 2) {
                if (!uo_anim_is(o, fs3(c, "runAnim", "Run"))) uo_anim_play(o, fs3(c, "runAnim", "Run"), false);
                scuttler_run_vel(o, c, s, dt);
                if (o->rb.has && (o->rb.contacts & (o->rb.vx > 0 ? 8 : 4))) {      // bumped a wall: hop back
                    float a = (o->rb.vx > 0 ? fr3(110, 130) : fr3(50, 70)) * (float)M_PI / 180;
                    o->rb.vx = 5 * cosf(a); o->rb.vy = 5 * sinf(a); s->vx = o->rb.vx; s->state = 4; s->t = 0.5f;
                    snd3(o, c, "bounceSound");
                }
            } else if (s->state == 4) { s->t -= dt; if (s->t <= 0) s->state = 2; }
            break; }
        case C_VINEPLAT: {
            VP *v = c->st;
            if (v->t > 0 && v->spr) {      // the Knight landed: the platform bobs
                v->t -= dt; float e = 1 - v->t / fmaxf(0.01f, ff3(c, "playerLandAnimLength", 0.5f)), k = e < 0.5f ? e * 2 : (1 - e) * 2;
                v->spr->ly = v->py + (v->t > 0 ? k * 0.15f : 0);
            }
            if (!v->activated && o->rb.has && !o->rb.kin && (o->rb.contacts & 1)) {     // a cut vine hits the ground
                snd3(o, c, "landSound"); game_cam_shake(1);
                UObj *se = fref3(o, c, "slamEffect"); if (se) uo_set_active(se, true);
                o->rb.kin = true; o->rb.vx = o->rb.vy = 0;
                UObj *ed = fref3(o, c, "enemyDetector"); if (ed) uo_set_active(ed, false);
                v->activated = true;
            }
            break; }
        case C_DREAMREACT: { TM *t = c->st; if (t->t > 0) t->t -= dt; break; }
        case C_DREAMPLANT: {
            DP *d = c->st;
            {   // glow faders: the plant glows while the Knight (with the Dream Nail) is near it; a finished plant glows on
                extern void comps_color_fade(UObj *o, bool up);
                float dx = uo_hero_x() - uo_wx(o), dy = uo_hero_y() - uo_wy(o);
                bool want = !d->activated && pd_bool("hasDreamNail") && dx * dx + dy * dy < 64;
                if (want != d->glowUp) { d->glowUp = want; comps_color_fade(fref3(o, c, "glowFader"), want); }
                if (d->completed && !d->doneGlow) { d->doneGlow = true; comps_color_fade(fref3(o, c, "completeGlowFader"), true); }
            }
            if (!d->activated || d->completed) break;
            if (d->state == 1) {       // CheckOrbs: every orb collected
                int left = 0, n = uo_count();
                for (int i = 0; i < n; i++) { UObj *q = uo_obj(i); UComp *oc = q && !q->destroyed ? uo_comp(q, C_DREAMORB) : NULL; if (oc && ((DO *)oc->st)->active && !((DO *)oc->st)->picked) left++; }
                if (left == 0) { d->state = 2; d->t = 1; d->completed = true; const char *b = fs3(c, "playerdataBool", ""); if (b[0]) pd_set_bool(b, true); pd_set_int("dreamPlantsCompleted", pd_int("dreamPlantsCompleted") + 1); }
            } else if (d->state == 2) { d->t -= dt; if (d->t <= 0) { uo_broadcast("DREAM AREA DISABLE"); snd3(o, c, "growChargeSound"); d->state = 3; d->t = 1; } }
            else if (d->state == 3) { d->t -= dt; if (d->t <= 0) { snd3(o, c, "growSound"); uo_anim_play(o, "Complete", true); game_cam_shake(1);
                UObj *dd = fref3(o, c, "dreamDialogue"); if (dd) uo_set_active(dd, true); d->state = 4; } }
            break; }
        case C_DREAMORB: {
            DO *d = c->st; if (!d->active || d->picked) break;
            if (d->state == 1 || d->state == 3) {     // spread out from the plant, wait, fly to its own spot
                d->t += dt; float k = d->t > 1 ? 1 : d->t, e = k * (2 - k);
                uo_set_world_pos(o, d->x0 + (d->x1 - d->x0) * e, d->y0 + (d->y1 - d->y0) * e);
                if (d->t >= 1) { if (d->state == 1) { d->state = 2; d->t = fr3(1, 1.5f); } else { d->state = 4; d->canPick = true; o->sx *= 2; o->sy *= 2; } }
            } else if (d->state == 2) { d->t -= dt; if (d->t <= 0) { d->x0 = uo_wx(o); d->y0 = uo_wy(o); d->x1 = d->hx; d->y1 = d->hy; d->t = 0; d->state = 3; } }
            else if (d->state == 4) {
                float x0, y0, x1, y1, hx0, hy0, hx1, hy1; uo_hero_box(&hx0, &hy0, &hx1, &hy1);
                bool hit = false; for (int i = 0; i < o->ncol; i++) if (uo_col_aabb(o, &o->col[i], &x0, &y0, &x1, &y1) && hx0 < x1 && hx1 > x0 && hy0 < y1 && hy1 > y0) hit = true;
                if (!o->ncol) { float dx = uo_hero_x() - uo_wx(o), dy = uo_hero_y() - uo_wy(o); hit = dx * dx + dy * dy < 1; }
                if (hit) {
                    pd_set_int("dreamOrbs", pd_int("dreamOrbs") + 1);
                    comps_event_register_send("DREAM ORB COLLECT");
                    static float pitch; static float lastT; extern float uo_time;
                    if (pitch <= 0 || uo_time - lastT > ff3(c, "pitchReturnDelay", 3)) pitch = ff3(c, "basePitch", 0.85f);
                    if (pitch > ff3(c, "maxPitch", 1.25f)) pitch = ff3(c, "maxPitch", 1.25f);
                    JVal *cs = jget(c->f, "collectSound"); const char *cl = clip3(cs); uo_play_at(o, cl ? cl : "dream_orb_pickup", 1, pitch);
                    pitch += ff3(c, "increasePitch", 0.025f); lastT = uo_time;
                    UObj *pa = fref3(o, c, "pickupAnim"); if (pa) { uo_set_parent(pa, NULL, false); uo_set_active(pa, true); }
                    d->picked = true; d->active = false; orb_vis(o, false);
                }
            }
            break; }
        default: break;
    }
}

// IHitResponder: ScuttlerControl
bool comp3_hit(UObj *o, UComp *c, float dir, int atk) {
    if (c->type != C_SCUTTLER) return false;
    SC *s = c->st; if (!s || !s->alive || s->actT > 0) return false;
    s->alive = false;
    UObj *sp = fref3(o, c, "splatEffectChild"); if (sp) uo_set_active(sp, true);
    if (pd_bool("hasJournal")) {
        const char *kd = fs3(c, "killedPDBool", "killedOrangeScuttler"), *ks = fs3(c, "killsPDBool", "killsOrangeScuttler");
        pd_set_bool(kd, true); int n = pd_int(ks); if (n > 0) pd_set_int(ks, n - 1); pd_set_bool(fs3(c, "newDataPDBool", "newDataOrangeScuttler"), true);
    }
    if (!fb3(c, "healthScuttler", false)) { uo_set_active(o, false); return true; }
    if (atk == ATK_NAIL) {
        spawn3(o, jget(c->f, "strikeNailPrefab"), uo_wx(o), uo_wy(o));
        UObj *g = spawn3(o, jget(c->f, "slashImpactPrefab"), uo_wx(o), uo_wy(o));
        if (g) { g->sx = g->sy = 0.9f; if (dir < 45) g->rz = fr3(340, 380); else if (dir < 135) g->rz = fr3(70, 110); else if (dir < 225) { g->rz = fr3(340, 380); g->sx = -0.9f; } else g->rz = fr3(250, 290); }
    } else if (atk == ATK_SPELL || atk == ATK_NAILBEAM) { UObj *g = spawn3(o, jget(c->f, "fireballHitPrefab"), uo_wx(o), uo_wy(o)); if (g) g->sx = g->sy = 0.5f; }
    snd3(o, c, "deathSound1"); snd3(o, c, "deathSound2"); game_cam_shake(1);
    o->mr = false; o->sr.en = false; if (o->rb.has) { o->rb.vx = o->rb.vy = 0; }
    UObj *pool = fref3(o, c, "pool");
    if (pool) {          // FlingUtils.FlingChildren: the blue spatters fly out and home in on the Knight
        int n = 8 + rand() % 3, i = 0;
        for (UObj *k = pool->child; k && i < n; k = k->next, i++) {
            uo_set_parent(k, NULL, false); uo_set_world_pos(k, uo_wx(o), uo_wy(o)); uo_set_active(k, true);
            float a = fr3(30, 150) * (float)M_PI / 180, v = fr3(15, 20);
            if (k->rb.has) { k->rb.vx = cosf(a) * v; k->rb.vy = sinf(a) * v; }
        }
    }
    s->state = 9; s->t = 1.2f;      // Heal(): the lifeblood mask after 1.2 s
    return true;
}

// trigger callbacks: VinePlatformCut (nail), VinePlatform enemy detector
void comp3_trigger(UObj *o, UComp *c, UObj *other, int kind) {
    if (c->type == C_DREAMPLANT && kind == 0 && !strcmp(other->tag, "Dream Attack")) {
        DP *d = c->st; if (d->activated) return;
        d->activated = true; d->state = 1;
        int n = uo_count();
        for (int i = 0; i < n; i++) {      // DreamPlantOrb.Show: every orb of the room spreads out from the plant
            UObj *q = uo_obj(i); UComp *oc = q && !q->destroyed ? uo_comp(q, C_DREAMORB) : NULL; if (!oc) continue;
            DO *od = oc->st; if (od->picked) continue;
            od->active = true; orb_vis(q, true); q->sx *= 0.5f; q->sy *= 0.5f;
            od->x0 = uo_wx(o) + fr3(-1, 1); od->y0 = uo_wy(o) + fr3(-3, -2);
            float dx = od->hx - od->x0, dy = od->hy - od->y0, l = sqrtf(dx * dx + dy * dy), r = fr3(2, 10);
            od->x1 = od->x0 + (l > 0 ? dx / l * r : 0); od->y1 = od->y0 + (l > 0 ? dy / l * r : 0);
            uo_set_world_pos(q, od->x0, od->y0); od->t = 0; od->state = 1;
        }
        uo_anim_play(o, "Activate", true); snd3(o, c, "hitSound"); game_cam_shake(1);
        UObj *wf = fref3(o, c, "whiteFlash"); if (wf) uo_set_active(wf, true);
        spawn3(o, jget(c->f, "dreamImpact"), uo_wx(o), uo_wy(o));
        comps_event_register_send("DREAM PLANT HIT");
        return;
    }
    if (c->type == C_VINECUT && kind == 0) {
        TM *t = c->st; if (t->done || strcmp(other->tag, "Nail Attack")) return;
        Fsm *df = fsm_find_on(other, "damages_enemy"); FVar *dv = df ? fsm_var(df, "direction") : NULL; float d = dv ? dv->f : 0;
        if (!(d < 45 || (d >= 135 && d < 225))) return;       // horizontal slashes only
        t->done = true;
        UObj *body = fref3(o, c, "body"); if (body && body->rb.has) { body->rb.kin = false; body->rb.sim = true; }
        UObj *cp = fref3(o, c, "cutParticles"); if (cp) uo_set_active(cp, true);
        spawn3(o, jget(c->f, "cutEffectPrefab"), uo_wx(o), uo_wy(other));
        for (UObj *p = o->parent; p; p = p->parent) {
            UComp *vc = uo_comp(p, C_VINEPLAT); if (!vc) continue;
            UObj *ed = fref3(p, vc, "enemyDetector"); if (ed) uo_set_active(ed, true);
            snd3(p, vc, "cutSound"); break;
        }
        snd3(o, c, "cutSound");
        UObj *sp = fref3(o, c, "sprites"); if (sp) uo_set_active(sp, false);
    }
}
void comp3_hero_touch(UObj *o, UComp *c, int side, int kind) {
    if (c->type == C_VINEPLAT && kind == 0 && side == 2) {
        VP *v = c->st; if (v->activated && !(o->rb.has && o->rb.kin)) return;
        snd3(o, c, "playerLandSound"); v->t = ff3(c, "playerLandAnimLength", 0.5f);
    } else if (c->type == C_STEEPSLOPE && kind != 1) {
        hero_set_sliding(fb3(c, "slideLeft", false), fb3(c, "slideRight", false), kind == 0);
    }
}
bool comp3_wants_triggers(UObj *o) { for (int i = 0; i < o->ncomp; i++) if (o->comp[i].type == C_VINECUT || o->comp[i].type == C_DREAMPLANT) return true; return false; }
bool comp3_is_responder(UObj *o) { UComp *c = uo_comp(o, C_SCUTTLER); return c && c->st && ((SC *)c->st)->alive; }

// EventRegister.SendEvent: every registered object of that event gets it (its target FSM or all of its FSMs)
void comps_event_register_send(const char *ev) {
    int n = uo_count();
    for (int i = 0; i < n; i++) {
        UObj *o = uo_obj(i); if (!o || o->destroyed || !o->used) continue;
        for (int k = 0; k < o->ncomp; k++) {
            UComp *c = &o->comp[k]; if (c->type != C_EVENTREG) continue;
            if (strcmp(fs3(c, "subscribedEvent", ""), ev)) continue;
            UObj *t = fref3(o, c, "targetFsm"); uo_send_event(t ? t : o, ev);
            // EnemyKillEventListener.killEvent
            for (int q = 0; q < n; q++) {
                UObj *e = uo_obj(q); if (!e || e->destroyed) continue;
                UComp *kl = uo_comp(e, C_KILLLISTENER); if (!kl || ref3(e, jget(kl->f, "killEvent")) != o) continue;
                if (uo_comp(e, C_HEALTH) && !hm_is_dead(e)) hm_hit(e, 9999, 0, ATK_GENERIC, 1);
            }
        }
    }
}

// EnemyDreamnailReaction.RecieveDreamImpact (SendDreamImpact action of the Knight's dream nail hitbox)
void comps_dream_impact(UObj *t) {
    UObj *o = t; UComp *c = uo_comp(o, C_DREAMREACT);
    if (!c) { for (o = t ? t->parent : NULL; o; o = o->parent) if ((c = uo_comp(o, C_DREAMREACT))) break; if (c && !jbool(jget(c->f, "allowUseChildColliders"), false)) c = NULL; }
    if (!c) return;
    TM *s = c->st; if (s->done || s->t > 0) return;        // suppressed / cooling down
    if (!jbool(jget(c->f, "noSoul"), false)) game_add_soul(pd_bool("equippedCharm_30") ? 66 : 33);
    extern void dlg_dream_msg(const char *title, int amount);
    dlg_dream_msg(fs3(c, "convoTitle", "GENERIC"), (int)jnum(jget(c->f, "convoAmount"), 8));
    spawn3(o, jget(c->f, "dreamImpactPrefab"), uo_wx(o), uo_wy(o));
    if (uo_comp(o, C_RECOIL)) comps_recoil_by_direction(o, uo_hero_x() < uo_wx(o) ? 0 : 180, 2);
    o->flash = 0.2f;
    s->t = 0.2f;
}
void comps_dream_make_ready(UObj *o) { UComp *c = o ? uo_comp(o, C_DREAMREACT) : NULL; if (c) ((TM *)c->st)->done = false; }

// IHitEffectReciever.RecieveHitEffect: hit flash prefab, orange blood / ghost slashes, impact sound
static void fling_n(UObj *o, JVal *pf, int lo, int hi, float smin, float smax, float amin, float amax, float x, float y) {
    if (!pf || pf->t != J_OBJ) return;
    int n = lo + (hi > lo ? rand() % (hi - lo + 1) : 0);
    for (int i = 0; i < n; i++) {
        UObj *g = spawn3(o, pf, x, y); if (!g) return;
        float a = fr3(amin, amax) * (float)M_PI / 180, v = fr3(smin, smax);
        UObj *b = g->rb.has ? g : NULL; if (!b) for (UObj *k = g->child; k; k = k->next) if (k->rb.has) { b = k; break; }
        if (b) { b->rb.vx = cosf(a) * v; b->rb.vy = sinf(a) * v; }
    }
}
bool comps_hit_effect(UObj *o, float dir) {
    UComp *c = uo_comp(o, C_INFECTEDFX); bool inf = c != NULL;
    if (!c) c = uo_comp(o, C_UNINFFX);
    if (!c) return false;
    JVal *eo = jget(c->f, "effectOrigin"); float ex = (float)jnum(jget(eo, "x"), 0), ey = (float)jnum(jget(eo, "y"), 0);
    float x = uo_wx(o) + ex * (o->sx < 0 ? -1 : 1), y = uo_wy(o) + ey;
    int d = ((int)floorf((dir + 45) / 90)) & 3;
    if (inf) {
        uo_send_event(o, "DAMAGE FLASH");
        snd3(o, c, "impactAudio");
        spawn3(o, jget(c->f, "hitFlashOrangePrefab"), x, y);
        if (!jbool(jget(c->f, "noBlood"), false)) {
            JVal *bl = jget(c->f, "spatterOrangePrefab");
            switch (d) {
                case 0: fling_n(o, bl, 3, 4, 10, 15, 120, 150, x, y); fling_n(o, bl, 8, 15, 10, 25, 30, 60, x, y); break;
                case 2: fling_n(o, bl, 3, 4, 10, 15, 30, 60, x, y); fling_n(o, bl, 8, 10, 15, 25, 120, 150, x, y); break;
                case 1: fling_n(o, bl, 8, 10, 20, 30, 80, 100, x, y); break;
                default: fling_n(o, bl, 4, 5, 15, 25, 140, 180, x, y); fling_n(o, bl, 4, 5, 15, 25, 360, 400, x, y); break;
            }
        }
    } else {
        if (jbool(jget(c->f, "sendDamageFlashEvent"), true)) uo_send_event(o, "DAMAGE FLASH");
        snd3(o, c, "enemyDamage");
        UObj *hp = spawn3(o, jget(c->f, "uninfectedHitPt"), x, y);
        static const float rot[4] = { -45, 45, -225, 225 }, a0[4] = { -40, 50, 140, 230 }, a1[4] = { 40, 130, 220, 310 };
        if (hp) hp->rz = rot[d];
        fling_n(o, jget(c->f, "slashEffectGhost1"), 2, 3, 20, 35, a0[d], a1[d], x, y);
        fling_n(o, jget(c->f, "slashEffectGhost2"), 2, 3, 10, 35, a0[d], a1[d], x, y);
    }
    return true;
}
