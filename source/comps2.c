// More of Hollow Knight's MonoBehaviours, ported from the decompiled sources: breakable props, geo rocks, persistent
// bools / ints, dropping / lifting / flipping platforms, conveyors, bounce shrooms, shadow gates, music regions,
// lifeblood cocoons, stalactites, bridge levers, jelly eggs, enemy wake / message regions, DamageEnemies...
// Coroutines become small state machines; Unity callbacks arrive through comps2.h.
#include "comps.h"
#include "comps2.h"
#include "uo.h"
#include "fsm.h"
#include "pd.h"
#include "hero.h"
#include "audio.h"
#include "world.h"
#include "masks.h"
#include "particles.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

UObj *comps_tree_obj(UObj *o, int idx);
void comps_pbi_key(UObj *o, UComp *c, char *buf, int n);
void game_add_blue(int n);
int game_darkness_level(void);
void game_cam_shake(int kind);

#ifdef __HOST__
#define TR(...) do { static int tr_ = -1; if (tr_ < 0) tr_ = getenv("HK_FSMTRACE") != NULL; if (tr_) fprintf(stderr, __VA_ARGS__); } while (0)
#else
#define TR(...) do { } while (0)
#endif
static float frand2(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }
static float ffl(UComp *c, const char *k, float d) { return (float)jnum(jget(c->f, k), d); }
static int fin(UComp *c, const char *k, int d) { return jint(jget(c->f, k), d); }
static bool fbo(UComp *c, const char *k, bool d) { return jbool(jget(c->f, k), d); }
static const char *fst(UComp *c, const char *k, const char *d) { JVal *v = jget(c->f, k); return v && v->t == J_STR ? v->s : d; }

static UObj *ref_at(UObj *o, JVal *v) {
    if (!v || v->t != J_OBJ) return NULL;
    JVal *oi = jget(v, "o"); if (!oi) return NULL;
    return comps_tree_obj(o, jint(oi, -1));
}
static UObj *fref(UObj *o, UComp *c, const char *k) { return ref_at(o, jget(c->f, k)); }
// HIT to a GameObject field: an object of the tree, or a scene object left out of it ({"ext": fileID}: a secret mask)
static void send_hit_field(UObj *o, UComp *c, const char *k) {
    JVal *v = jget(c->f, k); UObj *rx = ref_at(o, v);
    if (rx) { uo_send_event(rx, "HIT"); return; }
    JVal *e = v && v->t == J_OBJ ? jget(v, "ext") : NULL;
    if (e) masks_hit((long)jnum(e, 0));
}
static int ref_comp(JVal *v) { JVal *ci = v && v->t == J_OBJ ? jget(v, "c") : NULL; return ci ? jint(ci, -1) : -1; }
// a GameObject field: a local object, or a prefab to spawn
static UObj *spawn_ref(UObj *o, JVal *v, float x, float y) {
    if (!v || v->t != J_OBJ) return NULL;
    JVal *p = jget(v, "prefab");
    if (p && p->t == J_STR) { int ti = uo_template_by_key(p->s); return ti >= 0 ? uo_spawn(ti, x, y, 0) : NULL; }
    UObj *q = ref_at(o, v);
    if (q) { uo_set_world_pos(q, x, y); uo_set_active(q, true); }
    return q;
}
static UObj *spawn_field(UObj *o, UComp *c, const char *k, float x, float y) { return spawn_ref(o, jget(c->f, k), x, y); }
static const char *clip_of(JVal *v) {         // {"audio": name} or an AudioEvent {"Clip": {"audio": name}}
    if (!v || v->t != J_OBJ) return NULL;
    JVal *a = jget(v, "audio"); if (a && a->t == J_STR) return a->s;
    JVal *cl = jget(v, "Clip"); return cl ? clip_of(cl) : NULL;
}
static void snd(UObj *o, UComp *c, const char *k) {
    JVal *v = jget(c->f, k); const char *cl = clip_of(v); if (!cl) return;
    float pmin = (float)jnum(jget(v, "PitchMin"), 1), pmax = (float)jnum(jget(v, "PitchMax"), 1), vol = (float)jnum(jget(v, "Volume"), 1);
    uo_play_at(o, cl, vol, frand2(pmin, pmax));
}
static void snd_name(UObj *o, const char *clip, float vol) { uo_play_at(o, clip, vol, frand2(0.9f, 1.1f)); }
// RandomAudioClipTable assets ({"asset": guid}). The export has the tables (MonoBehaviour/*.asset) but not their GUIDs,
// so each is matched by the objects that use it: weights and pitch ranges as in the asset
typedef struct { const char *guid; const char *clip[4]; float w[4]; float pmin, pmax; } ClipTable;
static const ClipTable clipTables[] = {
    { "5e746eae", { "breakable_wall_hit_1" }, { 1 }, 0.9f, 1.1f },              // breakable_hit: rock walls, eggs, fossils, poles
    { "493dabe7", { "breakable_wall_hit_1" }, { 1 }, 0.9f, 1.1f },              // embedded bones (same family)
    { "51a3a345", { "breakable_pole_death" }, { 1 }, 0.85f, 1.15f },            // wrought-iron poles
    { "488e460c", { "fungus_mushroom_hit" }, { 10 }, 0.85f, 1.15f },            // fungus_mushroom_break
    { "96cdcc78", { "mushroom_walk_through_1", "mushroom_walk_through_2", "mushroom_walk_through_3" }, { 1, 1, 1 }, 0.85f, 1.15f },   // fungus_mushroom_shake
    { "f9432f73", { "barrel_death_1" }, { 1 }, 0.85f, 1.15f },                  // barrel_death
    { "fbcdea92", { "wagon_death" }, { 1 }, 0.9f, 1.1f },                       // mine carts
    { "49d56833", { "grass_cut_2", "grass_cut_3", "grass_cut_4", "grass_cut_5" }, { 1, 1, 1, 1 }, 0.9f, 1.1f },   // webs: grass_cut
    { "fd6dfec1", { "splat_01", "Splat_02", "Splat_03" }, { 1, 1, 1 }, 1, 1 },  // corpse splat
    { "313cdd5a", { "hive_zombie_death" }, { 1 }, 1, 1 },                       // splat_med (chunkers)
    { "15a48a37", { "explosion_4_wet_original" }, { 1 }, 0.85f, 1 },            // splat_big (Shrumal Ogre)
    { "21d7866a", { "water_drip_1", "water_drip_2", "water_drip_3", "water_drip_4" }, { 1, 1, 1, 1 }, 1, 1 },
    { "671b1751", { "enemy_damage_over_time" }, { 1 }, 0.85f, 1.15f },          // spore / dung damage ticks
};
const char *clip_table_pick(JVal *v, float *pitch) {
    JVal *a = v && v->t == J_OBJ ? jget(v, "asset") : NULL;
    if (!a || a->t != J_STR) return NULL;
    for (size_t i = 0; i < sizeof clipTables / sizeof clipTables[0]; i++) {
        const ClipTable *t = &clipTables[i];
        if (strncmp(a->s, t->guid, 8)) continue;
        int n = 0; float tot = 0; while (n < 4 && t->clip[n]) tot += t->w[n++];
        float r = frand2(0, tot), acc = 0; int k = n - 1;
        for (int j = 0; j < n - 1; j++) { acc += t->w[j]; if (r < acc) { k = j; break; } }
        if (pitch) *pitch = t->pmin == t->pmax ? t->pmax : frand2(t->pmin, t->pmax);
        return t->clip[k];
    }
    return NULL;
}
static void snd_table(UObj *o, JVal *v, float vol) {
    float p = 1; const char *cl = clip_table_pick(v, &p);
    if (cl) uo_play_at(o, cl, vol, p);
}
static void set_colliders(UObj *o, bool en, bool trigToo) { if (!o) return; for (int i = 0; i < o->ncol; i++) if (trigToo || !o->col[i].trig) o->col[i].en = en; }
static void set_collider_ref(UObj *o, UComp *c, const char *k, bool en) {   // a Collider2D field: {"o": i, "c": comp}
    JVal *v = jget(c->f, k); UObj *q = ref_at(o, v); if (!q) return;
    // colliders are the object's first components in export order; the comp index can't be mapped exactly, so all solid ones
    (void)ref_comp(v);
    set_colliders(q, en, false);
}
static void play(UObj *o, const char *clip) { if (o && clip && clip[0]) uo_anim_play(o, clip, true); }
static float clip_len(UObj *o, const char *clip) { float l = o ? uo_anim_clip_len(o, clip) : 0; return l > 0 ? l : 0.3f; }
static UObj *first_anim(UObj *o) {          // GetComponentInChildren<tk2dSpriteAnimator>
    if (!o) return NULL;
    if (o->an.has) return o;
    for (UObj *c = o->child; c; c = c->next) { UObj *r = first_anim(c); if (r) return r; }
    return NULL;
}
static float wz(UObj *o) { float z = 0; for (UObj *q = o; q; q = q->parent) z += q->lz; return z; }
static void fling(UObj *g, float speed, float angDeg) {
    if (!g) return;
    float a = angDeg * (float)M_PI / 180;
    UObj *b = g; if (!b->rb.has) for (UObj *k = g->child; k; k = k->next) if (k->rb.has) { b = k; break; }
    if (b->rb.has) { b->rb.vx = cosf(a) * speed; b->rb.vy = sinf(a) * speed; }
}
static float slash_dir(UObj *other) {         // the nail's damages_enemy "direction"
    Fsm *f = other ? fsm_find_on(other, "damages_enemy") : NULL;
    FVar *v = f ? fsm_var(f, "direction") : NULL;
    return v ? v->f : 0;
}
static bool tag_is(UObj *o, const char *t) { return o && !strcmp(o->tag, t); }
static UObj *comp_owner_of(UObj *o, CompType t) { for (UObj *q = o; q; q = q->parent) if (uo_comp(q, t)) return q; return NULL; }

/* ================= persistence (PersistentBoolItem / PersistentIntItem / GeoRock via SceneData) ================= */
static Fsm *fsm_with_var(UObj *o, const char *name, VType t) {
    for (int i = 0; i < o->nfsm; i++) { FVar *v = fsm_var(o->fsm[i], name); if (v && v->t == t) return o->fsm[i]; }
    return NULL;
}
static void pii_key(UObj *o, UComp *c, char *buf, int n) {   // keyed on the name at start, as pbi_key
    if (!c->st) { char *k = malloc(96); snprintf(k, 96, "%s:%s:%s", fbo(c, "semiPersistent", false) ? "pis" : "pii", uo_room_name(), o->name); c->st = k; }
    snprintf(buf, n, "%s", (const char *)c->st);
}
static void geo_key(UObj *o, char *buf, int n) { snprintf(buf, n, "geo:%s:%s", uo_room_name(), o->name); }
#define PINT_BIAS 1000000      // stored ints are value + bias so 0 can mean "no saved state"

/* ================= TouchShake ================= */
// a mushroom / plant the Knight walks through: plays its BasicSpriteAnimator (or tk2d clip) once, with a sound from the table
typedef struct { float t; int frame0; bool on; } TS;
static void ts_start(UObj *o, UComp *c) {
    UObj *a = fref(o, c, "anim"); UComp *ac = a ? uo_comp(a, C_SPRANIM) : NULL;
    if (ac) ac->en = false;      // TouchShake.Start: the animator only runs while shaking
}
static void ts_touch(UObj *o, UComp *c) {
    TS *s = c->st; if (s->on) return;
    UObj *a = fref(o, c, "anim"), *tk = fref(o, c, "tk2dAnim");
    UComp *ac = a ? uo_comp(a, C_SPRANIM) : NULL;
    if (!(ac && uo_active(a)) && !(tk && uo_active(tk) && tk->an.has)) return;
    snd_table(o, jget(c->f, "audioTable"), 1);
    if (ac) {
        JVal *fr = jget(ac->f, "frames"); int n = fr ? fr->n : 0;
        s->t = n / fmaxf(1, (float)jnum(jget(ac->f, "fps"), 30)) + 1.0f / 60; s->frame0 = a->sr.frame;
        ac->en = true; comp3_enable(a, ac);    // from its first frame
        if (ac->st) { int *idx = (int *)ac->st; *idx = 0; }
    } else { uo_anim_play_frame(tk, 0); s->t = 0.5f; }
    s->on = true;
}
static void ts_update(UObj *o, UComp *c, float dt) {
    TS *s = c->st; if (!s->on) return;
    s->t -= dt; if (s->t > 0) return;
    s->on = false;
    UObj *a = fref(o, c, "anim"); UComp *ac = a ? uo_comp(a, C_SPRANIM) : NULL;
    if (ac) { ac->en = false; a->sr.frame = s->frame0; }
}

/* ================= DamageEffectTicker / ExtraDamageable (Spore Shroom, Defender's Crest clouds) ================= */
typedef struct { float t; } DT;
typedef struct { int frame; } XD;
static void extra_damage(UObj *o, int type) {      // ExtraDamageable.RecieveExtraDamage
    UComp *c = uo_comp(o, C_EXTRADMG); if (!c || !c->en || !c->st) return;
    XD *x = c->st; if (x->frame == uo_frame) return;  // damagedThisFrame
    x->frame = uo_frame;
    if (strcmp(o->tag, "Spell Vulnerable") && hm_invincible(o)) return;
    snd_table(o, jget(c->f, "impactClipTable"), 1);
    o->flash = 0.12f;                                // flashSporeQuick / flashDungQuick
    if (!uo_comp(o, C_HEALTH) || hm_is_dead(o)) return;
    uo_send_event(o, "EXTRA DAMAGED");                // HealthManager.ApplyExtraDamage
    int hp = hm_get_hp(o) - (type == 2 ? 2 : 1);
    hm_set_hp(o, hp);
    TR("[comp] extra damage (type %d) to %s: hp %d\n", type, o->name, hp);
    if (hp <= 0) hm_die(o, 0, ATK_GENERIC);
}
static void ticker_update(UObj *o, UComp *c, float dt) {   // DamageEffectTicker.Update: every interval, everything in the cloud
    DT *d = c->st; float iv = ffl(c, "damageInterval", 0.2f); if (iv < 0.02f) iv = 0.02f;
    d->t += dt; if (d->t < iv) return;
    d->t -= iv;
    const char *ev = fst(c, "damageEvent", "SPORE"); int type = (int)ffl(c, "extraDamageType", 0);
    UObj *hit[UO_MAX_OV]; int n = o->nov; memcpy(hit, o->ov, sizeof(UObj *) * n);
    for (int k = 0; k < n; k++) {
        UObj *q = hit[k]; if (!q || !q->used || q->destroyed || q->pseudo) continue;
        Fsm *f = fsm_find_on(q, "Extra Damage"); if (f) fsm_event(f, ev);
        extra_damage(q, type);
    }
}

/* ================= Breakable ================= */
typedef struct { bool broken; float angleOffset; bool inert; } BK;
static void bk_static_parts(UObj *o, UComp *c, bool broken) {
    UObj *wr = fref(o, c, "wholeRenderer");
    if (wr) { wr->sr.en = !broken; wr->mr = !broken; }
    JVal *w = jget(c->f, "wholeParts"), *r = jget(c->f, "remnantParts");
    for (JVal *e = w ? w->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q) uo_set_active(q, !broken); }
    for (JVal *e = r ? r->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q) uo_set_active(q, broken); }
    send_hit_field(o, c, "hitEventReciever");
    set_colliders(o, !broken, true);
}
static void bk_break(UObj *o, UComp *c, float amin, float amax, float mult) {
    BK *b = c->st; if (!b || b->broken) return;
    bk_static_parts(o, c, true);
    JVal *dp = jget(c->f, "debrisParts");
    for (JVal *e = dp ? dp->child : NULL; e; e = e->next) {
        UObj *q = ref_at(o, e); if (!q) continue;
        uo_set_active(q, true);
        q->rz += b->angleOffset;
        fling(q, frand2(ffl(c, "flingSpeedMin", 10), ffl(c, "flingSpeedMax", 17)) * mult, frand2(amin, amax));
    }
    JVal *cp = jget(c->f, "containingParticles");
    if (cp && cp->n > 0) {
        float tot = 0; for (JVal *e = cp->child; e; e = e->next) tot += (float)jnum(jget(e, "probability"), 1);
        float r = frand2(0, tot), acc = 0; JVal *pick = NULL;
        for (JVal *e = cp->child; e; e = e->next) { acc += (float)jnum(jget(e, "probability"), 1); if (r <= acc) { pick = e; break; } }
        JVal *pg = pick ? jget(pick, "prefab") : NULL;
        if (pg && pg->t == J_OBJ) {
            UObj *local = ref_at(o, pg);
            if (local && local->parent == o) uo_set_active(local, true);
            else {
                JVal *fo = NULL, *fr = jget(c->f, "flingObjectRegister"); JVal *pk = jget(pg, "prefab");
                for (JVal *e = fr ? fr->child : NULL; e; e = e->next) {
                    JVal *ro = jget(e, "referenceObject"), *rk = ro ? jget(ro, "prefab") : NULL;
                    if ((rk && pk && !strcmp(rk->s, pk->s)) || (local && ref_at(o, ro) == local)) { fo = e; break; }
                }
                if (fo) {
                    int lo = jint(jget(fo, "spawnMin"), 1), hi = jint(jget(fo, "spawnMax"), lo), n = lo + (hi > lo ? rand() % (hi - lo + 1) : 0);
                    float ovx = (float)jnum(jget(jget(fo, "originVariation"), "x"), 0.5), ovy = (float)jnum(jget(jget(fo, "originVariation"), "y"), 0.5);
                    for (int i = 0; i < n; i++) {
                        UObj *g = spawn_ref(o, pg, uo_wx(o) + frand2(-ovx, ovx), uo_wy(o) + frand2(-ovy, ovy)); if (!g) break;
                        fling(g, frand2((float)jnum(jget(fo, "speedMin"), 9), (float)jnum(jget(fo, "speedMax"), 20)),
                              frand2((float)jnum(jget(fo, "angleMin"), 20), (float)jnum(jget(fo, "angleMax"), 160)));
                    }
                } else spawn_ref(o, pg, uo_wx(o), uo_wy(o));
            }
        }
    }
    snd(o, c, "breakAudioEvent");
    snd_table(o, jget(c->f, "breakAudioClipTable"), 1);
    send_hit_field(o, c, "hitEventReciever");
    if (fbo(c, "forwardBreakEvent", false)) uo_send_event(o, "BREAK");
    game_cam_shake(1);
    b->broken = true;
    TR("[comp] Breakable %s broken\n", o->name);
}
static bool bk_hit(UObj *o, UComp *c, float dir, int atk, UObj *src) {
    BK *b = c->st; if (!b || b->broken || b->inert) return false;
    float mult = 1, impact = dir;
    if (atk == ATK_SPELL) spawn_field(o, c, "spellHitEffectPrefab", uo_wx(o), uo_wy(o));
    else {
        if (atk != ATK_NAIL && atk != ATK_GENERIC) { impact = 90; mult = 1; }
        spawn_field(o, c, "strikeEffectPrefab", uo_wx(o), uo_wy(o));
        float sx = src ? (uo_wx(src) + uo_wx(o)) / 2 : uo_wx(o), sy = src ? (uo_wy(src) + uo_wy(o)) / 2 : uo_wy(o);
        UObj *e = spawn_field(o, c, "nailHitEffectPrefab", sx, sy);
        if (e) { int cd = ((int)floorf((impact + 45) / 90)) & 3; e->rz = cd == 3 ? frand2(250, 290) : cd == 1 ? frand2(70, 110) : frand2(340, 380); e->sx = cd == 2 ? -1.5f : 1.5f; e->sy = 1.5f; }
    }
    float amin, amax; int d = ((int)floorf((dir + 45) / 90)) & 3;
    JVal *eo = jget(c->f, "effectOffset"); float ex = (float)jnum(jget(eo, "x"), 0), ey = (float)jnum(jget(eo, "y"), 0.5);
    switch (d) {
        case 2: b->angleOffset = -b->angleOffset; amin = 120; amax = 160; spawn_field(o, c, "dustHitRegularPrefab", uo_wx(o) + ex, uo_wy(o) + ey); break;
        case 0: amin = 30; amax = 70; spawn_field(o, c, "dustHitRegularPrefab", uo_wx(o) + ex, uo_wy(o) + ey); break;
        case 1: b->angleOffset = 0; amin = 70; amax = 110; mult *= 1.5f; spawn_field(o, c, "dustHitRegularPrefab", uo_wx(o) + ex, uo_wy(o) + ey); break;
        default: b->angleOffset = 0; amin = 160; amax = 380; spawn_field(o, c, "dustHitDownPrefab", uo_wx(o) + ex, uo_wy(o) + ey); break;
    }
    bk_break(o, c, amin, amax, mult);
    return true;
}

/* ================= platforms ================= */
typedef struct { int state; float t; float y1, y2, jx; bool cancel, idle; UObj *jit; } PS;
static UObj *plat_anim(UObj *o, UComp *c) { UObj *a = fref(o, c, "spriteAnimator"); return a ? a : first_anim(o); }
static void drop_idle(UObj *o, UComp *c) {
    o->lz = 0.003f; play(plat_anim(o, c), fst(c, "idleAnim", "Idle Small"));
    UObj *col = fref(o, c, "collider"); set_colliders(col ? col : o, true, false);
}
static void drop_update(UObj *o, UComp *c, PS *s, float dt) {
    UObj *an = plat_anim(o, c), *col = fref(o, c, "collider"); if (!col) col = o;
    switch (s->state) {
        case 1: s->t -= dt; if (s->t <= 0) { set_colliders(col, false, false); snd(o, c, "dropSound"); play(an, fst(c, "dropAnim", "Drop Small")); s->t = clip_len(an, fst(c, "dropAnim", "Drop Small")); s->state = 2; } break;
        case 2: s->t -= dt; if (s->t <= 0) { o->lz = 0.007f; s->t = 1.5f; s->state = 3; } break;
        case 3: s->t -= dt; if (s->t <= 0) { snd(o, c, "flipUpSound"); play(an, fst(c, "raiseAnim", "Raise Small")); s->t = clip_len(an, fst(c, "raiseAnim", "Raise Small")); s->state = 4; } break;
        case 4: s->t -= dt; if (s->t <= 0) { s->state = 0; drop_idle(o, c); } break;
    }
}
static void lift_update(UObj *o, UComp *c, PS *s, float dt) {
    UObj *p1 = fref(o, c, "part1"), *p2 = fref(o, c, "part2");
    float off;
    if (s->state == 1) { if (s->t < 0.12f) { off = s->t * 0.75f; s->t += dt; } else { off = 0.09f; s->state = 2; s->t = 0.12f; } }
    else if (s->state == 2) { if (s->t > 0) { off = s->t * 0.75f; s->t -= dt; } else { off = 0; s->state = 0; } }
    else return;
    if (p1) uo_set_world_pos(p1, uo_wx(p1), s->y1 - off);
    if (p2) uo_set_world_pos(p2, uo_wx(p2), s->y2 - off);
}
static void flip_update(UObj *o, UComp *c, PS *s, float dt) {
    UObj *an = plat_anim(o, c), *top = fref(o, c, "topSpikes"), *bot = fref(o, c, "bottomSpikes");
    if (s->jit && s->state == 1) { if (s->t > 0) s->jit->lx = s->jx + frand2(-0.1f, 0.1f); else s->jit->lx = s->jx; }
    s->t -= dt; if (s->t > 0) return;
    switch (s->state) {
        case 0: if (s->idle) { play(an, "Glimmer Up"); s->t = clip_len(an, "Glimmer Up"); s->idle = false; } else { play(an, "Idle Up"); s->t = frand2(2, 5); s->idle = true; } break;
        case 1: if (s->jit) s->jit->lx = s->jx; play(an, "Flip Down 1"); s->t = clip_len(an, "Flip Down 1"); s->state = 2; break;
        case 2: play(an, "Flip Down 2"); s->t = clip_len(an, "Flip Down 2"); s->state = 3; break;
        case 3: if (top) uo_set_active(top, true); if (bot) uo_set_active(bot, false); play(an, "Idle Down"); s->cancel = false; s->t = 4; s->state = 4; break;
        case 4: { snd(o, c, "flipBackSound"); const char *cl = "Flip Up 1"; play(an, cl); s->t = clip_len(an, cl); s->state = 5; break; }
        case 5: play(an, "Flip Up 2"); s->t = clip_len(an, "Flip Up 2"); s->state = 6; break;
        case 6: if (top) uo_set_active(top, false); if (bot) uo_set_active(bot, true); s->state = 0; s->idle = false; s->t = 0; break;
    }
}
static void flip_cancel(UObj *o, UComp *c, PS *s) {    // a nail hit on the top spikes cuts the flip short
    if (!s || s->state != 4) return;
    spawn_field(o, c, "nailStrikePrefab", uo_wx(o), uo_wy(o));
    snd(o, c, "hitSound"); game_cam_shake(1);
    snd(o, c, "flipBackSound");
    UObj *an = plat_anim(o, c); play(an, "Flip Up 1N"); s->t = clip_len(an, "Flip Up 1N"); s->state = 5;
}

/* ================= BounceShroom ================= */
typedef struct { int state; float t; } BS;
static void shroom_bounce(UObj *o, UComp *c, BS *s) {
    if (!fbo(c, "active", false) || !s) return;
    spawn_field(o, c, "bounceSmallPrefab", uo_wx(o), uo_wy(o));
    if (s->state == 2) return;
    UObj *an = first_anim(o); const char *cl = fst(c, "bounceAnim", "Bounce 1");
    play(an, cl); s->t = clip_len(an, cl); s->state = 2;
    snd_name(o, "bounce_shroom_small", 0.8f);
}

// NailSlash.Bounce: the shroom's "Bounce Shroom" FSM gets BOUNCE UPWARD, or BounceShroom.BounceLarge runs
static float shroomNextFx, shroomNextShake;   // statics shared by every shroom, like the game's
void comp2_shroom_nail(UObj *o, bool effects) {
    for (int k = 0; k < o->nfsm; k++) if (o->fsm[k]->name && !strcmp(o->fsm[k]->name, "Bounce Shroom")) { fsm_event(o->fsm[k], "BOUNCE UPWARD"); return; }
    UComp *c = uo_comp(o, C_BOUNCESHROOM); BS *s = c ? c->st : NULL;
    if (!c || !s || !fbo(c, "active", false)) return;
    if (effects) { if (uo_time >= shroomNextFx) shroomNextFx = uo_time + 0.25f; else effects = false; }
    if (effects) spawn_field(o, c, "bounceLargePrefab", uo_wx(o), uo_wy(o));
    if (s->state != 2) {
        UObj *an = first_anim(o); const char *cl = fst(c, "bounceAnim", "Bounce 1");
        play(an, cl); s->t = clip_len(an, cl); s->state = 2;
        snd_name(o, "bounce_shroom_small", 0.8f);
    }
    if (uo_time > shroomNextShake) { game_shake(0.25f); shroomNextShake = uo_time + 0.25f; }    // EnemyKillShake
    if (!effects) return;
    UObj *he = fref(o, c, "hitEffect"); if (he) uo_set_active(he, true);
    spawn_field(o, c, "heroParticlePrefab", uo_hero_x(), uo_hero_y() - 1.5f);
}

/* ================= HealthCocoon / Stalactite / Bridge / misc state ================= */
typedef struct { bool activated; float t; bool sweating; } HC;
typedef struct { int state; float t; } ST;
typedef struct { int state; float t; } BL;
typedef struct { int state; float t, t2; } BSec;
typedef struct { bool activated; } FL;           // poles, vines, eggs, ActivateChildrenOnContact
typedef struct { float timer; bool inside; int lastHit; } DE;

static void cocoon_broken(UObj *o, UComp *c) {
    o->mr = false; o->sr.en = false;
    JVal *d = jget(c->f, "disableChildren"); for (JVal *e = d ? d->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q) uo_set_active(q, false); }
    JVal *dc = jget(c->f, "disableColliders"); for (JVal *e = dc ? dc->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q) set_colliders(q, false, true); }
    if (!dc || !dc->n) set_colliders(o, false, true);
}
static void cocoon_hit(UObj *o, UComp *c, UObj *other, bool spell) {
    HC *h = c->st; if (!h || h->activated) return;
    if (!spell) {
        float v = slash_dir(other), z; float sx = 1.5f;
        if (v < 135) z = frand2(340, 380); else if (v < 225) { sx = -1.5f; z = frand2(70, 110); } else z = frand2(250, 290);
        JVal *se = jget(c->f, "slashEffects"); for (JVal *e = se ? se->child : NULL; e; e = e->next) { UObj *g = spawn_ref(o, e, uo_wx(o), uo_wy(o) + 0.8f); if (g) { g->rz = z; g->sx = sx; g->sy = 1.5f; } }
    } else { JVal *se = jget(c->f, "spellEffects"); for (JVal *e = se ? se->child : NULL; e; e = e->next) spawn_ref(o, e, uo_wx(o), uo_wy(o) + 0.8f); }
    h->activated = true;
    JVal *en = jget(c->f, "enableChildren"); for (JVal *e = en ? en->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q) uo_set_active(q, true); }
    UObj *cap = fref(o, c, "cap");
    if (cap) { uo_set_active(cap, true); float dx = uo_wx(o) - (other ? uo_wx(other) : uo_hero_x()), dy = uo_wy(o) - (other ? uo_wy(other) : uo_hero_y()), l = sqrtf(dx * dx + dy * dy); if (l > 0 && cap->rb.has) { float f = ffl(c, "capHitForce", 10); cap->rb.vx = dx / l * f; cap->rb.vy = dy / l * f; } }
    JVal *fp = jget(c->f, "flingPrefabs");
    int fl_n = 0;
    for (JVal *e = fp ? fp->child : NULL; e; e = e->next) {
        JVal *pf = jget(e, "prefab"); if (!pf) continue;
        int lo = jint(jget(e, "minAmount"), 1), hi = jint(jget(e, "maxAmount"), lo), n = lo + (hi > lo ? rand() % (hi - lo + 1) : 0);
        float ovx = (float)jnum(jget(jget(e, "originVariation"), "x"), 0.5), ovy = (float)jnum(jget(jget(e, "originVariation"), "y"), 0.5);
        for (int i = 0; i < n; i++) {
            UObj *g = spawn_ref(o, pf, uo_wx(o) + ovx * frand2(-1, 1), uo_wy(o) + ovy * frand2(-1, 1));
            if (!g) continue; fl_n++;
            fling(g, frand2((float)jnum(jget(e, "minSpeed"), 0), (float)jnum(jget(e, "maxSpeed"), 0)), frand2((float)jnum(jget(e, "minAngle"), 0), (float)jnum(jget(e, "maxAngle"), 0)));
        }
    }
    snd(o, c, "deathSound");
    cocoon_broken(o, c);
    pd_set_int("cocoonsBroken", pd_int("cocoonsBroken") + 1);
    game_cam_shake(1);
    if (fl_n == 0) game_add_blue(2);       // no scuttler prefab in this room: give the lifeblood directly
}

static void stal_fall(UObj *o, UComp *c, ST *s, float delay) {
    if (s->state != 0) return;
    UObj *top = fref(o, c, "top"); if (top) uo_set_parent(top, o->parent, false);
    o->ly -= ffl(c, "startFallOffset", 0.1f);
    UObj *sfe = fref(o, c, "startFallEffect"); if (sfe) { uo_set_active(sfe, true); uo_set_parent(sfe, o->parent, false); }
    snd(o, c, "startFallSound");
    s->state = 1; s->t = delay;
}
static void stal_fallen(UObj *o, UComp *c, ST *s) {
    UObj *fe = fref(o, c, "fallEffect"); if (fe) { uo_set_active(fe, true); uo_set_parent(fe, o->parent, false); }
    UObj *te = fref(o, c, "trailEffect"); if (te) uo_set_active(te, true);
    if (s->state != 3) damagehero_set(o, 1);
    UComp *de = uo_comp(o, C_DAMAGEENEMIES); if (de) de->en = true;
    o->rb.kin = false; o->rb.sim = true;
    s->state = 2;
}

/* ================= dispatch ================= */
void comp2_awake(UObj *o, UComp *c) {
    if (c->type >= C_SPRANIM) { comp3_awake(o, c); return; }
    switch (c->type) {
        case C_BREAKABLE: c->st = calloc(1, sizeof(BK)); ((BK *)c->st)->angleOffset = ffl(c, "angleOffset", -60); break;
        case C_DROPPLAT: case C_LIFTPLAT: case C_FLIPPLAT: case C_SLOPEPLAT: case C_RUINSLIFT: case C_LIFTCHAIN: case C_DREAMPLAT: c->st = calloc(1, sizeof(PS)); break;
        case C_BOUNCESHROOM: c->st = calloc(1, sizeof(BS)); break;
        case C_HEALTHCOCOON: c->st = calloc(1, sizeof(HC)); break;
        case C_STALACTITE: c->st = calloc(1, sizeof(ST)); break;
        case C_BRIDGELEVER: c->st = calloc(1, sizeof(BL)); break;
        case C_BRIDGESECTION: c->st = calloc(1, sizeof(BSec)); o->lz = 0.036f; break;
        case C_POLE: case C_VINE: case C_JELLYEGG: case C_ACTCHILDREN: case C_POLETOP: c->st = calloc(1, sizeof(FL)); break;
        case C_TOUCHSHAKE: c->st = calloc(1, sizeof(TS)); break;
        case C_DMGTICKER: c->st = calloc(1, sizeof(DT)); break;
        case C_EXTRADMG: c->st = calloc(1, sizeof(XD)); ((XD *)c->st)->frame = -1; break;
        case C_GRUBBG: c->st = calloc(1, sizeof(int)); break;
        case C_DAMAGEENEMIES: c->st = calloc(1, sizeof(DE)); ((DE *)c->st)->lastHit = -1; break;
        default: break;
    }
    if (c->type == C_JELLYEGG) { UObj *sh = fref(o, c, "shinyItem"); if (sh) uo_set_active(sh, true); }
}

static bool has_comp_handler(UObj *o) {   // components that keep their own PersistentBoolItem state (OnGetSaveState)
    return uo_comp(o, C_BREAKABLE) || uo_comp(o, C_HEALTHCOCOON) || uo_comp(o, C_HEALTH);
}

void comp2_start(UObj *o, UComp *c) {
    if (c->type >= C_SPRANIM) { comp3_start(o, c); return; }
    char k[96];
    switch (c->type) {
        case C_TOUCHSHAKE: ts_start(o, c); break;
        case C_PBI: {
            if (uo_comp(o, C_HEALTH)) break;          // comps.c: HealthManager
            comps_pbi_key(o, c, k, sizeof k);
            bool saved = pd_bool(k);
            if (!saved && !strncmp(o->name, "Grub Bottle", 11)) {
                // saves from v1.0.8 and older stored a freed grub as "<room>:Collected" (the bottle's name after it
                // breaks); in a room with one grub that is this bottle
                char old[96]; snprintf(old, sizeof old, "pbi:%s:Collected", uo_room_name());
                int n = 0; for (int i = 0; i < uo_count(); i++) { UObj *q = uo_obj(i); if (q && !q->destroyed && (!strncmp(q->name, "Grub Bottle", 11) || !strcmp(q->name, "Collected"))) n++; }
                if (n == 1 && pd_bool(old)) { saved = true; pd_set_bool(k, true); }
            }
            if (!saved) break;
            if (uo_comp(o, C_BREAKABLE)) { UComp *bc = uo_comp(o, C_BREAKABLE); ((BK *)bc->st)->broken = true; bk_static_parts(o, bc, true); }
            else if (uo_comp(o, C_HEALTHCOCOON)) { UComp *hc = uo_comp(o, C_HEALTHCOCOON); ((HC *)hc->st)->activated = true; cocoon_broken(o, hc); }
            else if (comp3_pbi_set(o, true)) {}
            else { Fsm *f = fsm_with_var(o, "Activated", V_BOOL); if (f) fsm_var(f, "Activated")->b = true; }
            break; }
        case C_PII: {
            pii_key(o, c, k, sizeof k);
            int v = pd_int(k); Fsm *f = fsm_with_var(o, "Value", V_INT);
            if (v != 0 && f) fsm_var(f, "Value")->i = v - PINT_BIAS;
            break; }
        case C_GEOROCK: {
            geo_key(o, k, sizeof k);
            int v = pd_int(k); Fsm *f = fsm_with_var(o, "Hits", V_INT);
            if (v != 0 && f) fsm_var(f, "Hits")->i = v - PINT_BIAS;
            break; }
        case C_BREAKABLE: {
            BK *b = c->st; float z = wz(o);
            if (z > ffl(c, "inertBackgroundThreshold", 1) || z < ffl(c, "inertForegroundThreshold", -1)) { b->inert = true; set_colliders(o, false, true); c->en = false; break; }
            if (!b->broken) { JVal *r = jget(c->f, "remnantParts"); for (JVal *e = r ? r->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q && q->activeSelf) uo_set_active(q, false); } }
            b->angleOffset *= o->sx < 0 ? -1 : 1;
            break; }
        case C_POLE: case C_VINE:
            if (fabsf(wz(o) - 0.004f) > 1) { set_colliders(o, false, true); c->en = false; }
            break;
        case C_DROPPLAT: drop_idle(o, c); break;
        case C_LIFTPLAT: { PS *s = c->st; UObj *p1 = fref(o, c, "part1"), *p2 = fref(o, c, "part2"); s->y1 = p1 ? uo_wy(p1) : 0; s->y2 = p2 ? uo_wy(p2) : 0; break; }
        case C_FLIPPLAT: { PS *s = c->st; UObj *an = plat_anim(o, c); play(an, "Idle Up"); s->idle = true; s->t = frand2(2, 5); s->jit = an; s->jx = an ? an->lx : 0; break; }
        case C_BOUNCESHROOM: {
            BS *s = c->st; if (!fbo(c, "active", false)) break;
            spawn_field(o, c, "idleParticlePrefab", uo_wx(o), uo_wy(o));
            play(first_anim(o), fst(c, "idleAnim", "Idle 1")); s->t = frand2(2, 8); s->state = 0;
            break; }
        case C_SETPOS_PD:
            if (pd_bool(fst(c, "playerDataBool", ""))) {
                if (fbo(c, "setX", false)) o->lx = ffl(c, "XPos", o->lx);
                if (fbo(c, "setY", false)) o->ly = ffl(c, "YPos", o->ly);
            }
            break;
        case C_DARK_NOLANTERN: if (game_darkness_level() == 2 && !pd_bool("hasLantern")) uo_set_active(o, false); break;
        case C_HEALTHCOCOON: { HC *h = c->st; h->t = frand2(ffl(c, "waitMin", 2), ffl(c, "waitMax", 6)); break; }
        case C_STALACTITE: damagehero_set(o, 0); o->rb.kin = true; { UComp *de = uo_comp(o, C_DAMAGEENEMIES); if (de) de->en = false; } break;
        case C_BRIDGELEVER: {
            BL *b = c->st;
            bool act = pd_bool(fst(c, "playerDataBool", "cityBridge1"));
            UObj *bc = fref(o, c, "bridgeCollider"); if (bc) set_colliders(bc, act, false);
            if (act) {
                b->state = 3;
                JVal *secs = jget(c->f, "sections");
                for (JVal *e = secs ? secs->child : NULL; e; e = e->next) {
                    UObj *s = ref_at(o, e); if (!s) continue;
                    UComp *sc = uo_comp(s, C_BRIDGESECTION); if (!sc) continue;
                    play(fref(s, sc, "sectionAnim"), "Bridge Activated"); play(fref(s, sc, "fenceAnim"), "Fence Activated");
                    UObj *fr = fref(s, sc, "fenceRenderer"); if (fr) fr->mr = true;
                    s->lz = 0.001f; ((BSec *)sc->st)->state = 3;
                }
                play(o, "Lever Activated");
            }
            break; }
        case C_ENEMYMSG: { Fsm *f = fsm_find_on(o, "enemy_message"); if (f) { f->enabled = false; } break; }
        case C_JELLYEGG: { UObj *sh = fref(o, c, "shinyItem"); if (sh) uo_set_active(sh, false); break; }
        case C_RUINSLIFT: break;
        case C_GRUBBG:
            if (fin(c, "grubNumber", 0) > pd_int("grubsCollected")) { uo_set_active(o, false); break; }
            play(o, "Home Bounce");
            break;
        default: break;
    }
}

void comp2_update(UObj *o, UComp *c, float dt) {
    if (c->type >= C_SPRANIM) { comp3_update(o, c, dt); return; }
    switch (c->type) {
        case C_TOUCHSHAKE: ts_update(o, c, dt); break;
        case C_DMGTICKER: ticker_update(o, c, dt); break;
        case C_DROPPLAT: drop_update(o, c, c->st, dt); break;
        case C_LIFTPLAT: lift_update(o, c, c->st, dt); break;
        case C_FLIPPLAT: flip_update(o, c, c->st, dt); break;
        case C_BOUNCESHROOM: {
            BS *s = c->st; if (!fbo(c, "active", false)) break;
            s->t -= dt; if (s->t > 0) break;
            UObj *an = first_anim(o);
            if (s->state == 0) { const char *cl = fst(c, "bobAnim", "Bob 1"); play(an, cl); s->t = clip_len(an, cl); s->state = 1; }
            else { play(an, fst(c, "idleAnim", "Idle 1")); s->t = frand2(2, 8); s->state = 0; }
            break; }
        case C_HEALTHCOCOON: {
            HC *h = c->st; if (h->activated) break;
            h->t -= dt; if (h->t > 0) break;
            UObj *an = o->an.has ? o : NULL;
            if (!h->sweating) { snd(o, c, "moveSound"); const char *cl = fst(c, "sweatAnimation", "Cocoon Sweat"); play(an, cl); h->t = clip_len(an, cl); h->sweating = true; }
            else { play(an, fst(c, "idleAnimation", "Cocoon Idle")); h->t = frand2(ffl(c, "waitMin", 2), ffl(c, "waitMax", 6)); h->sweating = false; }
            break; }
        case C_STALACTITE: { ST *s = c->st; if (s->state == 1) { s->t -= dt; if (s->t <= 0) stal_fallen(o, c, s); } break; }
        case C_BRIDGELEVER: {
            BL *b = c->st; if (b->state != 1 && b->state != 2) break;
            b->t -= dt; if (b->t > 0) break;
            if (b->state == 1) {       // after the hit: the sections rise and the Knight roars
                uo_send_event(uo_hero(), "ROAR ENTER");
                JVal *secs = jget(c->f, "sections");
                for (JVal *e = secs ? secs->child : NULL; e; e = e->next) {
                    UObj *s = ref_at(o, e); UComp *sc = s ? uo_comp(s, C_BRIDGESECTION) : NULL; if (!sc) continue;
                    BSec *bs = sc->st; float dx = uo_wx(s) - uo_wx(o), dy = uo_wy(s) - uo_wy(o);
                    bs->state = 1; bs->t = sqrtf(dx * dx + dy * dy) * 0.1f + 0.25f;
                }
                snd_name(o, "bridge_rise_loop", 0.7f);
                b->state = 2; b->t = 3.25f;
            } else { uo_send_event(uo_hero(), "ROAR EXIT"); b->state = 3; }
            break; }
        case C_BRIDGESECTION: {
            BSec *s = c->st;
            if (s->state == 1) { s->t -= dt; if (s->t <= 0) { play(fref(o, c, "sectionAnim"), "Bridge Rise"); snd_name(o, "bridge_section_rise", 0.6f); o->lz = 0.001f; s->state = 2; s->t2 = 2.5f; } }
            else if (s->state == 2) { s->t2 -= dt; if (s->t2 <= 0) { UObj *fr = fref(o, c, "fenceRenderer"); if (fr) fr->mr = true; play(fref(o, c, "fenceAnim"), "Fence Rise"); s->state = 3; } }
            break; }
        case C_GRUBBG:     // Wave -> back to Idle when the clip ends
            if (c->st && *(int *)c->st && o->an.done) { *(int *)c->st = 0; play(o, "Home Bounce"); }
            break;
        case C_SLOPEPLAT: {
            float x = uo_hero_x(), xl = ffl(c, "heroXLeft", 0), xr = ffl(c, "heroXRight", 1), yl = ffl(c, "platYLeft", 0), yr = ffl(c, "platYRight", 0);
            if (x <= xl) o->ly = yl; else if (x >= xr) o->ly = yr; else o->ly = yl + (yr - yl) * ((x - xl) / (xr - xl));
            break; }
        case C_RUINSLIFT: {       // RuinsLift.HideChains: chains above the lift car disappear as it rises
            PS *s = c->st; UObj *cp = fref(o, c, "chainsParent"); JVal *sp = jget(c->f, "stopPositions");
            if (!cp || !sp || sp->n < 2) break;
            float y = uo_wy(o); if (y == s->y1) break; s->y1 = y;
            float mn = 1e9f, mx = -1e9f; for (JVal *e = sp->child; e; e = e->next) { float v = (float)jnum(e, 0); if (v < mn) mn = v; if (v > mx) mx = v; }
            UObj *ch[64]; int n = 0;
            for (UObj *k = cp->child; k && n < 64; k = k->next) if (k->activeSelf || k->started) ch[n++] = k;
            for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (uo_wy(ch[j]) > uo_wy(ch[i])) { UObj *t = ch[i]; ch[i] = ch[j]; ch[j] = t; }
            int num = (int)floorf((y - mn) / (mx - mn) * n);
            for (int i = 0; i < n; i++) uo_set_active(ch[i], i >= num);
            break; }
        default: break;
    }
}

void comp2_fixed(UObj *o, UComp *c, float dt) {
    (void)dt;
    switch (c->type) {
        case C_SHADOWGATE: {
            if (!pd_bool("hasShadowDash")) break;
            UObj *dc = fref(o, c, "disableCollider"); if (!dc) dc = o;
            bool dashing = hero_get_state("dashing");
            set_colliders(dc, !dashing, false);
            break; }
        case C_DAMAGEENEMIES: {    // FixedUpdate: everything still inside gets hit (HealthManager's own i-frames limit it)
            if (!c->en) break;
            int dmg = fin(c, "damageDealt", 0); if (dmg <= 0) break;
            for (int i = 0; i < o->nov; i++) {
                UObj *t = o->ov[i]; if (!t || t->destroyed || !uo_active(t)) continue;
                if (t->layer == 20 || t->layer == 9 || t->layer == 26 || t->layer == 31 || !strcmp(t->tag, "Geo") || t->pseudo) continue;
                uo_set_event_sender(o);
                uo_send_event(t, "TAKE DAMAGE");
                comps_hit_taker(t, dmg, ffl(c, "direction", 0), fin(c, "attackType", 1), ffl(c, "magnitudeMult", 1), o);
            }
            break; }
        default: break;
    }
}

// HitTaker.Hit for the IHitResponders handled here (Breakable); true if one responded
bool comp2_hit(UObj *o, float dir, int atk, UObj *src) {
    bool any = false;
    for (int i = 0; i < o->ncomp; i++) { UComp *c = &o->comp[i]; if (c->en && c->type == C_BREAKABLE) any |= bk_hit(o, c, dir, atk, src); else if (c->en && c->type == C_SCUTTLER) any |= comp3_hit(o, c, dir, atk); }
    return any;
}
bool comp2_is_responder(UObj *o) { if (comp3_is_responder(o)) return true; UComp *c = uo_comp(o, C_BREAKABLE); return c && c->en && c->st && !((BK *)c->st)->broken && !((BK *)c->st)->inert; }

static void pole_hit(UObj *o, UComp *c, UObj *other) {
    FL *s = c->st; if (s->activated) return;
    bool go = false; float sgn = 1;
    if (tag_is(other, "Nail Attack")) { float v = slash_dir(other); if (v < 45) go = true; else if (v >= 135 && v < 225) { go = true; sgn = -1; } }
    else if (tag_is(other, "Hero Spell")) go = true;
    if (!go) return;
    o->sr.en = false;
    UObj *b = fref(o, c, "bottom"), *t = fref(o, c, "top");
    if (b) uo_set_active(b, true);
    if (t) { uo_set_active(t, true); float a = frand2(ffl(c, "angleMin", 40), ffl(c, "angleMax", 60)) * (float)M_PI / 180, sp = ffl(c, "speed", 17);
             UObj *bd = t->rb.has ? t : NULL; if (bd) { bd->rb.vx = sp * cosf(a) * sgn; bd->rb.vy = sp * sinf(a); } }
    UObj *e = spawn_field(o, c, "slashEffectPrefab", uo_wx(o), uo_wy(o)); if (e) { e->sx = sgn; e->rz = frand2(ffl(c, "slashAngleMin", 340), ffl(c, "slashAngleMax", 380)); }
    snd_name(o, "breakable_pole_hit", 0.8f);
    s->activated = true;
    TR("[comp] pole %s cut\n", o->name);
}

// OnTriggerEnter2D / Stay / Exit (kind 0 / 1 / 2) of o's colliders with other
void comp2_trigger(UObj *o, UObj *other, int kind) {
    bool heroLayer = other->layer == 9;
    for (int i = 0; i < o->ncomp; i++) {
        UComp *c = &o->comp[i]; if (!c->en) continue;
        switch (c->type) {
            case C_MUSICREGION:
                if (!heroLayer || kind == 1) break;
                if (kind == 0) { JVal *mc = jget(c->f, "enterMusicCue"); const char *g = mc ? jstr(jget(mc, "asset"), NULL) : NULL; if (g) game_music_cue(g, fbo(c, "minesDelay", false) ? 2.35f : 0, ffl(c, "enterTransitionTime", 1)); }
                else { JVal *mc = jget(c->f, "exitMusicCue"); const char *g = mc ? jstr(jget(mc, "asset"), NULL) : NULL; if (g) game_music_cue(g, 0, ffl(c, "exitTransitionTime", 1)); }
                break;
            case C_CONVEYORZONE: if (heroLayer && kind != 1) hero_conveyor(1, kind == 0, ffl(c, "speed", 0), fbo(c, "vertical", false)); break;
            case C_CONVEYORSPEED: if (heroLayer && kind == 0) hero_conveyor(2, true, ffl(c, "speed", 0), false); break;
            case C_WAKEREGION: if (kind != 1) { UObj *t = fref(o, c, "fsm"); if (t) uo_send_event(t, kind == 0 ? fst(c, "enterEvent", "WAKE") : fst(c, "exitEvent", "SLEEP")); } break;
            case C_ENEMYMSG: {
                if (kind == 2) break;
                const char *ev = fst(c, "eventName", ""); Fsm *f = fsm_find_on(o, "enemy_message"); FVar *v = f ? fsm_var(f, "Event") : NULL;
                if (v && v->t == V_STRING) ev = v->s;
                if (!ev || !ev[0]) break;
                UObj *t = other; while (t->parent && !t->rb.has) t = t->parent;
                uo_send_event(t, ev);
                if (!strcmp(ev, "GO LEFT")) walker_go(t, -1); else if (!strcmp(ev, "GO RIGHT")) walker_go(t, 1);
                break; }
            case C_POLE: if (kind == 0) pole_hit(o, c, other); break;
            case C_VINE: {
                FL *s = c->st; if (kind != 0 || s->activated) break;
                if (!(tag_is(other, "Nail Attack") || tag_is(other, "Hero Spell") || (tag_is(other, "HeroBox") && hero_get_state("superDashing")))) break;
                JVal *bl = jget(c->f, "blobs"); for (JVal *e = bl ? bl->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q) uo_set_active(q, false); }
                JVal *ef = jget(c->f, "effects"); for (JVal *e = ef ? ef->child : NULL; e; e = e->next) { UObj *q = ref_at(o, e); if (q) uo_set_active(q, true); }
                snd_name(o, "infected_vine_break", 0.8f);
                s->activated = true;
                break; }
            case C_JELLYEGG: {
                FL *s = c->st; if (kind != 0 || s->activated) break;
                if (!(tag_is(other, "Nail Attack") || tag_is(other, "Hero Spell") || tag_is(other, "HeroBox"))) break;
                s->activated = true;
                UObj *mr = fref(o, c, "meshRenderer"); if (mr) mr->mr = false; else o->mr = false;
                set_colliders(o, false, true);
                snd_name(o, "jelly_egg_pop", 0.9f);
                if (fbo(c, "bomb", false)) { spawn_field(o, c, "explosionObject", uo_wx(o), uo_wy(o)); break; }
                UObj *se = fref(o, c, "strikeEffect"); if (se) { float k = frand2(1, 1.5f); se->sx = se->sy = k; se->rz = frand2(0, 360); uo_set_active(se, true); }
                UObj *fs = fref(o, c, "falseShiny"); if (fs) uo_set_active(fs, false);
                UObj *sh = fref(o, c, "shinyItem"); if (sh) uo_set_active(sh, true);
                break; }
            case C_ACTCHILDREN: {
                FL *s = c->st; if (kind != 0 || s->activated) break;
                for (UObj *k = o->child; k; k = k->next) uo_set_active(k, true);
                set_colliders(o, false, true); s->activated = true;
                break; }
            case C_HEALTHCOCOON:
                if (kind == 0 && (tag_is(other, "Nail Attack") || tag_is(other, "Hero Spell"))) cocoon_hit(o, c, other, tag_is(other, "Hero Spell"));
                break;
            case C_BRIDGELEVER: {
                BL *b = c->st; if (kind != 0 || b->state != 0 || !tag_is(other, "Nail Attack")) break;
                pd_set_bool(fst(c, "playerDataBool", "cityBridge1"), true);
                snd(o, c, "switchSound"); snd_name(o, "lever_hit", 1);
                game_freeze(0.05f); game_cam_shake(1);
                UObj *ho = fref(o, c, "hitOrigin");
                spawn_field(o, c, "strikeNailPrefab", ho ? uo_wx(ho) : uo_wx(o), ho ? uo_wy(ho) : uo_wy(o));
                play(o, "Lever Hit");
                UObj *bc = fref(o, c, "bridgeCollider"); if (bc) set_colliders(bc, true, false);
                b->state = 1; b->t = 0.1f;
                break; }
            case C_STALACTITE: {
                ST *s = c->st; if (kind != 0 || s->state == 4) break;
                if (s->state == 2 && other->layer == 8) {      // landed
                    o->rb.kin = true; o->rb.vx = o->rb.vy = 0;
                    // the trail stays where the spike landed and stops emitting (its smoke fades, then it removes itself):
                    // without the Stop it smoked there for the rest of the visit
                    UObj *te = fref(o, c, "trailEffect"); if (te) { uo_set_parent(te, NULL, false); particles_obj_play(te, false); }
                    UObj *emb = fref(o, c, "embeddedVersion"); if (emb) { uo_set_active(emb, true); uo_set_parent(emb, o->parent, false); }
                    float h = 0; bool hit = w_raycast(uo_wx(o), uo_wy(o), 0, -1, 10, &h);
                    JVal *le = jget(c->f, "landEffectPrefabs"); for (JVal *e = le ? le->child : NULL; e; e = e->next) spawn_ref(o, e, uo_wx(o), hit ? uo_wy(o) - h : uo_wy(o));
                    s->state = 4; uo_set_active(o, false);
                } else if (tag_is(other, "Nail Attack")) {
                    if (s->state == 0) { stal_fall(o, c, s, 0); stal_fallen(o, c, s); }
                    damagehero_set(o, 0); s->state = 3;
                    float v = slash_dir(other), ang = 0;
                    if (v < 45) ang = 45;
                    else if (v < 135) {     // hit upward: shatters
                        JVal *hu = jget(c->f, "hitUpEffectPrefabs"); for (JVal *e = hu ? hu->child : NULL; e; e = e->next) spawn_ref(o, e, uo_wx(o), uo_wy(o));
                        int n = fin(c, "spawnMin", 10) + rand() % (fin(c, "spawnMax", 12) - fin(c, "spawnMin", 10) + 1);
                        for (int q = 0; q < n; q++) fling(spawn_field(o, c, "hitUpRockPrefabs", uo_wx(o), uo_wy(o)), frand2(fin(c, "speedMin", 15), fin(c, "speedMax", 20)), frand2(0, 360));
                        snd(o, c, "breakSound");
                        s->state = 4; uo_set_active(o, false); break;
                    } else if (v < 225) ang = -45; else ang = 0;
                    float hv = ffl(c, "hitVelocity", 40), a = (ang - 90) * (float)M_PI / 180;
                    o->rz = ang; o->rb.g = 0; o->rb.kin = false; o->rb.sim = true; o->rb.vx = cosf(a) * hv; o->rb.vy = sinf(a) * hv;
                    spawn_field(o, c, "nailStrikePrefab", uo_wx(o), uo_wy(o));
                    snd(o, c, "hitSound");
                }
                break; }
            case C_TRIGGERENTER: {     // delegates of owners up the tree
                if (kind == 0 && heroLayer && o->treeArr) {     // GrubBGControl.waveRegion (a separate object)
                    for (int q = 0; q < o->treeN; q++) {
                        UObj *g = o->treeArr[q]; UComp *gc = g ? uo_comp(g, C_GRUBBG) : NULL;
                        if (!gc || !uo_active(g) || fref(g, gc, "waveRegion") != o) continue;
                        play(g, "Home Wave"); if (gc->st) *(int *)gc->st = 1;
                    }
                }
                for (UObj *p = o->parent; p; p = p->parent) {
                    UComp *sc = uo_comp(p, C_STALACTITE);
                    if (sc && kind == 0 && tag_is(other, "Player")) {
                        ST *s = sc->st;
                        if (s->state == 0) {
                            float h; bool blocked = w_raycast(uo_wx(p), uo_wy(p), 0, -1, fmaxf(0, uo_wy(p) - uo_hero_y()), &h) && uo_wx(p) > -1e8f;
                            (void)blocked;
                            stal_fall(p, sc, s, ffl(sc, "fallDelay", 0.25f)); uo_set_active(o, false);
                        }
                        break;
                    }
                    UComp *fc = uo_comp(p, C_FLIPPLAT);
                    if (fc && kind == 0 && tag_is(other, "Nail Attack") && fref(p, fc, "topSpikes") == o) { flip_cancel(p, fc, fc->st); break; }
                    UComp *dc = uo_comp(p, C_DREAMPLAT);
                    if (dc && heroLayer) {
                        PS *s = dc->st;
                        if (fref(p, dc, "innerCollider") == o && kind == 0 && !s->idle) { play(first_anim(p), "Show"); snd(p, dc, "activateSound"); s->idle = true; }
                        if (fref(p, dc, "outerCollider") == o && kind == 2 && s->idle) { play(first_anim(p), "Hide"); snd(p, dc, "deactivateSound"); s->idle = false; }
                        break;
                    }
                }
                break; }
            case C_VINECUT: case C_DREAMPLANT: comp3_trigger(o, c, other, kind); break;
            case C_PUSHRUBBLE: if (kind == 0 && o->rb.has && !o->rb.kin) { o->rb.vx += frand2(-2, 2); o->rb.vy += frand2(0, 0.8f); } break;
            case C_TOUCHSHAKE: if (kind == 0 && (tag_is(other, "HeroBox") || tag_is(other, "Player"))) ts_touch(o, c); break;
            default: break;
        }
    }
}
bool comp2_wants_triggers(UObj *o) {
    for (int i = 0; i < o->ncomp; i++) switch (o->comp[i].type) {
        case C_MUSICREGION: case C_CONVEYORZONE: case C_CONVEYORSPEED: case C_WAKEREGION: case C_ENEMYMSG: case C_POLE: case C_VINE:
        case C_JELLYEGG: case C_ACTCHILDREN: case C_HEALTHCOCOON: case C_BRIDGELEVER: case C_STALACTITE: case C_TRIGGERENTER:
        case C_DAMAGEENEMIES: case C_PUSHRUBBLE: case C_VINECUT: case C_DREAMPLANT: case C_TOUCHSHAKE: case C_DMGTICKER: return true;
        default: break;
    }
    return false;
}

// the Knight's body touched (kind 0) / left (kind 2) a solid collider of o; side = where the Knight is: 0 left, 1 right, 2 above, 3 below
void comp2_hero_touch(UObj *o, int side, int kind) {
    for (UObj *p = o; p; p = p->parent) {
        for (int i = 0; i < p->ncomp; i++) {
            UComp *c = &p->comp[i]; if (!c->en) continue;
            switch (c->type) {
                case C_DROPPLAT: { PS *s = c->st; if (kind == 0 && side == 2 && s->state == 0) { s->state = 1; s->t = 0.7f; snd(p, c, "landSound"); UObj *se = fref(p, c, "strikeEffect"); if (se) uo_set_active(se, true); } break; }
                case C_LIFTPLAT: { PS *s = c->st; if (kind == 0 && side == 2 && s->state == 0) { s->state = 1; s->t = 0; snd_name(p, "lift_platform_land", 0.6f); } break; }
                case C_FLIPPLAT: { PS *s = c->st; if (kind == 0 && s->state == 0) { snd(p, c, "flipSound"); play(plat_anim(p, c), "Shake Up"); UObj *se = fref(p, c, "strikeEffect"); if (se) uo_set_active(se, true); s->state = 1; s->t = 0.5f; } break; }
                case C_VINEPLAT: case C_STEEPSLOPE: comp3_hero_touch(p, c, side, kind); break;
                case C_CONVEYOR: if (p == o) hero_conveyor(0, kind == 0, ffl(c, "speed", 0), fbo(c, "vertical", false)); break;
                case C_COLLENTER: {     // BounceShroom children report the side the Knight hit
                    if (kind != 0 || p != o) break;
                    for (UObj *q = o->parent; q; q = q->parent) {
                        UComp *bs = uo_comp(q, C_BOUNCESHROOM);
                        if (!bs || !fbo(bs, "active", false)) continue;
                        if (side == 0) hero_recoil_left(); else if (side == 1) hero_recoil_right();
                        else if (side == 2) hero_shroom_recoil();
                        shroom_bounce(q, bs, bs->st);
                        break;
                    }
                    break; }
                default: break;
            }
        }
        if (p != o) break;    // OnCollisionEnter2D reaches the collider's object and its rigidbody's parent only
    }
}

// own rigidbody vs terrain (BreakablePoleTopLand)
void comp2_collision(UObj *o, UComp *c, bool enter) {
    if (c->type != C_POLETOP || !enter || !(o->rb.contacts & 1)) return;
    float z = fmodf(uo_wrz(o), 360); if (z < 0) z += 360;
    if (z >= ffl(c, "angleMin", 165) && z <= ffl(c, "angleMax", 195)) {
        JVal *ef = jget(c->f, "effects"); for (JVal *e = ef ? ef->child : NULL; e; e = e->next) spawn_ref(o, e, uo_wx(o), uo_wy(o));
        o->rb.kin = true; o->rb.sim = false; o->rb.vx = o->rb.vy = 0; o->rb.angVel = 0;
    }
}

// SavePersistentObjects: before the room unloads (and when the game saves)
void comps_save_persistent(void) {
    int n = uo_count(); char k[96];
    for (int i = 0; i < n; i++) {
        UObj *o = uo_obj(i); if (!o || o->destroyed || o->pseudo) continue;
        for (int j = 0; j < o->ncomp; j++) {
            UComp *c = &o->comp[j];
            if (c->type == C_PBI) {
                if (uo_comp(o, C_HEALTH) || jbool(jget(c->f, "dontSave"), false)) continue;
                bool act = false;
                UComp *bc = uo_comp(o, C_BREAKABLE), *hc = uo_comp(o, C_HEALTHCOCOON);
                if (bc && bc->st) act = ((BK *)bc->st)->broken;
                else if (hc && hc->st) act = ((HC *)hc->st)->activated;
                else if (comp3_pbi_get(o, &act)) {}
                else { Fsm *f = fsm_with_var(o, "Activated", V_BOOL); if (!f) continue; act = fsm_var(f, "Activated")->b; }
                comps_pbi_key(o, c, k, sizeof k); pd_set_bool(k, act);
            } else if (c->type == C_PII) {
                Fsm *f = fsm_with_var(o, "Value", V_INT); if (!f) continue;
                pii_key(o, c, k, sizeof k); pd_set_int(k, fsm_var(f, "Value")->i + PINT_BIAS);
            } else if (c->type == C_GEOROCK) {
                Fsm *f = fsm_with_var(o, "Hits", V_INT); if (!f) continue;
                geo_key(o, k, sizeof k); pd_set_int(k, fsm_var(f, "Hits")->i + PINT_BIAS);
            }
        }
    }
}

// RuinsLift / LiftChain methods called from the lift FSMs (CallMethodProper)
bool comp2_call(UObj *o, const char *behaviour, const char *method, float a0, float *res) {
    if (!o) return false;
    if (!strcmp(behaviour, "RuinsLift")) {
        UComp *c = uo_comp(o, C_RUINSLIFT); if (!c) return false;
        JVal *sp = jget(c->f, "stopPositions"); int n = sp ? sp->n : 0, pos = (int)a0 - 1;
        if (pos < 0 || pos + 1 > n) pos = 0;
        if (!strcmp(method, "GetPositionY")) { *res = n ? (float)jnum(jat(sp, pos), 0) : 0; return true; }
        if (!strcmp(method, "IsCurrentPositionTerminus")) { *res = ((int)a0 == 1 || (int)a0 == n) ? 1 : 0; return true; }
        if (!strcmp(method, "KeepInBounds")) { *res = (float)(pos + 1); return true; }
        return false;
    }
    if (!strcmp(behaviour, "LiftChain")) {
        UComp *c = uo_comp(o, C_LIFTCHAIN); if (!c) return false;
        PS *s = c->st; int dir = !strcmp(method, "GoDown") ? -1 : !strcmp(method, "GoUp") ? 1 : 0;
        // every animator below: play / pause
        UObj *stack[64]; int sn = 0; stack[sn++] = o;
        while (sn) {
            UObj *q = stack[--sn];
            if (q->an.has) { if (dir == 0) q->an.playing = false; else { q->an.playing = true; if (s->state != dir) uo_anim_play(q, dir < 0 ? "Chain Down" : "Chain Up", true); } }
            for (UObj *k = q->child; k && sn < 64; k = k->next) stack[sn++] = k;
        }
        if (dir) s->state = dir;
        *res = 0; return true;
    }
    if (!strcmp(behaviour, "HeroPlatformStick") || !strcmp(behaviour, "PushableRubble")) { *res = 0; return true; }
    if (!strcmp(behaviour, "Breakable") && !strcmp(method, "BreakSelf")) { UComp *c = uo_comp(o, C_BREAKABLE); if (c) bk_break(o, c, 70, 110, 1); *res = 0; return true; }
    if (!strcmp(behaviour, "DropPlatform") && !strcmp(method, "Idle")) { UComp *c = uo_comp(o, C_DROPPLAT); if (c) drop_idle(o, c); *res = 0; return true; }
    if (!strcmp(behaviour, "HealthCocoon") && !strcmp(method, "SetScuttlerAmount")) { *res = 0; return true; }
    return false;
}
