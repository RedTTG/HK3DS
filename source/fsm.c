#include "fsm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------- variables ---------------- */
static VType vtype(const char *t) {
    if (!t) return V_NONE;
    if (!strcmp(t, "float")) return V_FLOAT;
    if (!strcmp(t, "int")) return V_INT;
    if (!strcmp(t, "bool")) return V_BOOL;
    if (!strcmp(t, "string")) return V_STRING;
    if (!strcmp(t, "vector2")) return V_VEC2;
    if (!strcmp(t, "vector3")) return V_VEC3;
    if (!strcmp(t, "gameobject")) return V_GO;
    if (!strcmp(t, "object")) return V_OBJ;
    if (!strcmp(t, "color")) return V_COLOR;
    if (!strcmp(t, "enum")) return V_ENUM;
    if (!strcmp(t, "array")) return V_ARRAY;
    return V_NONE;
}

/* PlayMaker globals (PlayMakerGlobals.asset): mostly GameObjects bound to native stand-ins by uo.c; names an FSM
   uses without declaring them locally resolve here (created on first use) */
#define MAX_GLOBALS 64
static FVar globals[MAX_GLOBALS]; static char globalNames[MAX_GLOBALS][32]; static int nglobals;
static FVar *global_var(const char *name, bool create) {
    for (int i = 0; i < nglobals; i++) if (!strcmp(globalNames[i], name)) return &globals[i];
    if (!create || nglobals >= MAX_GLOBALS) return NULL;
    FVar *v = &globals[nglobals]; memset(v, 0, sizeof *v);
    snprintf(globalNames[nglobals], 32, "%s", name); v->name = globalNames[nglobals]; nglobals++;
    return v;
}
void fsm_globals_bind_go(const char *name, UObj *o) { FVar *v = global_var(name, true); if (v) { v->t = V_GO; v->go = o; } }
void fsm_globals_clear(void) {
    { FVar *h = global_var("Is HUD Out", true); if (h) h->t = V_BOOL; } for (int i = 0; i < nglobals; i++) if (globals[i].t == V_GO) globals[i].go = NULL; }

FVar *fsm_var(Fsm *f, const char *name) {
    if (!f || !name) return NULL;
    for (int i = 0; i < f->nvar; i++) if (!strcmp(f->var[i].name, name)) return &f->var[i];
    return global_var(name, false);
}
// {"var": name} node: FSMs made from the same definition share the variable order, so the index is cached in the node
FVar *fsm_var_j(Fsm *f, JVal *vn) {
    if (!f || !vn || !vn->s) return NULL;
    int c = vn->cache - 1;
    if (c >= 0 && c < f->nvar && f->var[c].name && !strcmp(f->var[c].name, vn->s)) return &f->var[c];
    for (int i = 0; i < f->nvar; i++) if (!strcmp(f->var[i].name, vn->s)) { vn->cache = i + 1; return &f->var[i]; }
    return global_var(vn->s, false);
}

Fsm *fsm_create_vars(const char *name, UObj *owner, const char **names, const VType *types, int n) {
    Fsm *f = calloc(1, sizeof(Fsm));
    f->name = name; f->owner = owner; f->pending = -1; f->cur = -1; f->prev = -1; f->enabled = true;
    f->nvar = n; f->var = calloc(n ? n : 1, sizeof(FVar));
    for (int i = 0; i < n; i++) { f->var[i].name = names[i]; f->var[i].t = types[i]; }
    f->st = calloc(1, sizeof(FState)); f->gl = calloc(1, sizeof(FTrans));
    return f;
}

static UObj *ref_obj(Fsm *f, JVal *v) {
    if (!v || v->t != J_OBJ) return NULL;
    JVal *o = jget(v, "o");
    if (o) { int i = jint(o, -1); return (f->tree && i >= 0 && i < f->ntree) ? f->tree[i] : NULL; }
    if (jget(v, "owner")) return f->owner;
    return NULL;
}

Fsm *fsm_create(JVal *def, UObj *owner, UObj **tree, int ntree) {
    Fsm *f = calloc(1, sizeof(Fsm));
    f->def = def; f->owner = owner; f->tree = tree; f->ntree = ntree;
    f->name = jstr(jget(def, "fsm"), "FSM");
    f->pending = -1; f->cur = -1; f->prev = -1; f->enabled = true;
    // variables
    JVal *vars = jget(def, "variables");
    f->nvar = vars ? vars->n : 0;
    f->var = calloc(f->nvar ? f->nvar : 1, sizeof(FVar));
    int k = 0;
    for (JVal *v = vars ? vars->child : NULL; v; v = v->next, k++) {
        FVar *fv = &f->var[k];
        fv->name = v->key; fv->t = vtype(jstr(jget(v, "type"), NULL));
        JVal *val = jget(v, "value");
        switch (fv->t) {
            case V_FLOAT: fv->f = (float)jnum(val, 0); break;
            case V_INT: case V_ENUM: fv->i = jint(val, 0); break;
            case V_BOOL: fv->b = jbool(val, false); break;
            case V_STRING: if (val && val->t == J_STR) strncpy(fv->s, val->s, FVAR_STR - 1); break;
            case V_VEC2: case V_VEC3: case V_COLOR:
                fv->v[0] = (float)jnum(jget(val, fv->t == V_COLOR ? "r" : "x"), 0); fv->v[1] = (float)jnum(jget(val, fv->t == V_COLOR ? "g" : "y"), 0);
                fv->v[2] = (float)jnum(jget(val, fv->t == V_COLOR ? "b" : "z"), 0); fv->v[3] = (float)jnum(jget(val, "a"), 1); break;
            case V_GO: fv->go = ref_obj(f, val); fv->obj = val; break;
            default: fv->obj = val; break;
        }
    }
    // states
    JVal *sts = jget(def, "states");
    f->nst = sts ? sts->n : 0;
    f->st = calloc(f->nst ? f->nst : 1, sizeof(FState));
    k = 0;
    for (JVal *s = sts ? sts->child : NULL; s; s = s->next, k++) f->st[k].name = jstr(jget(s, "name"), "");
    const char *start = jstr(jget(def, "start"), NULL);
    k = 0;
    for (JVal *s = sts ? sts->child : NULL; s; s = s->next, k++) {
        FState *st = &f->st[k];
        if (start && !strcmp(st->name, start)) f->start = k;
        JVal *tr = jget(s, "transitions");
        st->ntr = tr ? tr->n : 0;
        st->tr = calloc(st->ntr ? st->ntr : 1, sizeof(FTrans));
        int j = 0;
        for (JVal *t = tr ? tr->child : NULL; t; t = t->next, j++) {
            st->tr[j].ev = jstr(jget(t, "event"), "");
            const char *to = jstr(jget(t, "to"), "");
            st->tr[j].to = -1;
            for (int q = 0; q < f->nst; q++) if (!strcmp(f->st[q].name, to)) { st->tr[j].to = q; break; }
        }
        JVal *acts = jget(s, "actions");
        st->nact = acts ? acts->n : 0;
        st->act = calloc(st->nact ? st->nact : 1, sizeof(FsmAct));
        j = 0;
        for (JVal *a = acts ? acts->child : NULL; a; a = a->next, j++) {
            FsmAct *fa = &st->act[j];
            fa->tname = jstr(jget(a, "type"), "");
            fa->type = act_lookup(fa->tname);
            fa->enabled = jbool(jget(a, "enabled"), true);
            fa->p = jget(a, "params");
            if (act_has_fixed(fa->type)) f->handleFixed = true;
            if (act_is_trigger(fa->type)) f->wantsTriggers = true;
        }
    }
    JVal *gl = jget(def, "global");
    f->ngl = gl ? gl->n : 0;
    f->gl = calloc(f->ngl ? f->ngl : 1, sizeof(FTrans));
    k = 0;
    for (JVal *t = gl ? gl->child : NULL; t; t = t->next, k++) {
        f->gl[k].ev = jstr(jget(t, "event"), "");
        const char *to = jstr(jget(t, "to"), "");
        f->gl[k].to = -1;
        for (int q = 0; q < f->nst; q++) if (!strcmp(f->st[q].name, to)) { f->gl[k].to = q; break; }
    }
    return f;
}

void fsm_free(Fsm *f) {
    if (!f) return;
    for (int i = 0; i < f->nst; i++) { free(f->st[i].tr); free(f->st[i].act); }
    free(f->st); free(f->gl); free(f->var); free(f);
}

const char *fsm_state_name(Fsm *f) { return f && f->cur >= 0 ? f->st[f->cur].name : "-"; }

/* ---------------- state machine ---------------- */
static int busy;      // nesting depth of action callbacks for any FSM (events then queue a switch)
static Fsm *pq[128]; static int npq;   // FSMs with a transition waiting for the outermost callback to return
static void do_switch(Fsm *f);
static void queue_pending(Fsm *f) { for (int i = 0; i < npq; i++) if (pq[i] == f) return; if (npq < 128) pq[npq++] = f; }
static void enter_state(Fsm *f);
static void do_switch(Fsm *f);
static void begin_deferred(Fsm *f) { if (f->deferStart) { f->deferStart = false; enter_state(f); } }
static void flush_pending(void) {
    while (npq > 0 && !busy) { Fsm *f = pq[--npq]; if (!f->started) continue; begin_deferred(f); if (f->pending >= 0) do_switch(f); }
}

static bool all_finished(FState *st) {
    for (int i = 0; i < st->nact; i++) if (st->act[i].enabled && st->act[i].entered && !st->act[i].finished) return false;
    return true;
}

static int find_transition(Fsm *f, const char *ev) {
    if (f->cur < 0 || !ev || !ev[0]) return -1;
    FState *st = &f->st[f->cur];
    for (int i = 0; i < st->ntr; i++) if (st->tr[i].to >= 0 && !strcmp(st->tr[i].ev, ev)) return st->tr[i].to;
    for (int i = 0; i < f->ngl; i++) if (f->gl[i].to >= 0 && !strcmp(f->gl[i].ev, ev)) return f->gl[i].to;   // (skip stale ones to missing states)
    return -1;
}

static void exit_state(Fsm *f) {
    if (f->cur < 0) return;
    FState *st = &f->st[f->cur];
    busy++;
    for (int i = 0; i < st->nact; i++) {
        FsmAct *a = &st->act[i];
        if (a->entered) { act_exit(f, a); a->entered = false; }
    }
    busy--;
    f->ndelayed = 0;      // Fsm.ExitState -> KillDelayedEvents
}

static bool stFinishedSent;   // per call scratch
static void enter_state(Fsm *f) {
    FState *st = &f->st[f->cur];
    f->stateTime = 0;
    busy++; f->inEnter = true;
    for (int i = 0; i < st->nact; i++) {
        FsmAct *a = &st->act[i];
        a->finished = false; a->entered = false;
        if (!a->enabled) continue;
        a->entered = true;
        act_enter(f, a);
        if (f->pending >= 0) break;
    }
    f->inEnter = false; busy--;
    (void)stFinishedSent;
    if (f->pending < 0 && all_finished(st)) {
        int to = find_transition(f, "FINISHED");
        if (to >= 0) f->pending = to;
    }
}

static void do_switch(Fsm *f) {
    int guard = 0;
    while (f->pending >= 0 && guard++ < 1000) {      // PlayMaker's own limit (Fsm.MaxLoopCount)
        int to = f->pending; f->pending = -1;
        exit_state(f);
        f->prev = f->cur; f->cur = to; f->nswitch++;
#ifdef __HOST__
        { static int tr = -1; if (tr < 0) tr = getenv("HK_FSMTRACE") != NULL;
          if (tr) fprintf(stderr, "[fsm %d] %s/%s: %s -> %s\n", uo_frame, f->owner ? f->owner->name : "?", f->name, f->prev >= 0 ? f->st[f->prev].name : "-", f->st[to].name);
          // test: HK_TPSTATE="State=x,y;State2=x,y" puts the Knight there when any FSM enters that state (climbs the bot can't do)
          const char *tp = getenv("HK_TPSTATE");
          if (tp) { char b[256]; snprintf(b, sizeof b, "%s", tp);
              for (char *sv, *t = strtok_r(b, ";", &sv); t; t = strtok_r(NULL, ";", &sv)) { char *eq = strchr(t, '='); float x, y;
                  if (eq && (*eq = 0, !strcmp(t, f->st[to].name)) && sscanf(eq + 1, "%f,%f", &x, &y) == 2) { extern void hero_set_position(float, float); hero_set_position(x, y); extern void hero_test_stop(void); hero_test_stop(); fprintf(stderr, "[tp] %s -> %.1f,%.1f\n", t, x, y); } } } }
#endif
        enter_state(f);
    }
    if (guard >= 1000) {
        f->pending = -1;
#ifdef __HOST__
        fprintf(stderr, "WARN fsm loop %s/%s at %s\n", f->owner ? f->owner->name : "?", f->name, fsm_state_name(f));
#endif
    }
}
void fsm_flush(void) { flush_pending(); }

void fsm_start(Fsm *f) {
    if (!f || f->started || f->nst == 0) return;
    f->started = true; f->cur = f->start; f->pending = -1;
    enter_state(f);
    if (!busy) { do_switch(f); flush_pending(); } else queue_pending(f);
}

// A PlayMakerFSM on an object a script creates (CreateObject, SpawnObjectFromGlobalPool) starts in its Start(), after
// the creating state's remaining actions: the Shiny's "SetFsmInt ID" on the charm message it just made has to land before
// the message builds CHARM_NAME_<ID> (it showed CHARM_NAME_0 for every charm after the first)
void fsm_start_new(Fsm *f) {
    if (!busy) { fsm_start(f); return; }
    if (!f || f->started || f->nst == 0) return;
    f->started = true; f->cur = f->start; f->pending = -1; f->stateTime = 0; f->deferStart = true;
    FState *st = &f->st[f->cur];
    for (int i = 0; i < st->nact; i++) { st->act[i].entered = false; st->act[i].finished = false; }
    queue_pending(f);
}

void fsm_stop(Fsm *f) {
    if (!f || !f->started) return;
    if (f->deferStart) { f->deferStart = false; f->started = false; f->cur = -1; f->pending = -1; f->ndelayed = 0; return; }
    exit_state(f);
    f->started = false; f->cur = -1; f->pending = -1; f->ndelayed = 0;
}

void fsm_event(Fsm *f, const char *ev) {
    if (!f || !f->started || !ev || !ev[0]) return;
    begin_deferred(f);
    int to = find_transition(f, ev);
#ifdef __HOST__
    if (getenv("HK_EVTRACE") && !strcmp(ev, getenv("HK_EVTRACE"))) fprintf(stderr, "[ev] %s -> %s/%s state=%s to=%d busy=%d pending=%d\n", ev, f->owner ? f->owner->name : "?", f->name ? f->name : "?", f->cur >= 0 ? f->st[f->cur].name : "-", to, busy, f->pending);
#endif
    if (to < 0) return;
    if (busy && f->pending >= 0) {           // a transition is already waiting: the first event wins,
        bool local = false; FState *st = &f->st[f->cur];  // except a global transition (STUN, ZERO HP...), which interrupts
        for (int i = 0; i < st->ntr; i++) if (!strcmp(st->tr[i].ev, ev)) local = true;
        if (local) return;
    }
    f->pending = to; f->lastEvent = ev;
    if (!busy) { do_switch(f); flush_pending(); }
    else queue_pending(f);
}

void fsm_event_delayed(Fsm *f, const char *ev, float delay) {
    if (!f || !ev) return;
    if (delay <= 0) { fsm_event(f, ev); return; }
    if (f->ndelayed < 16) { f->delayed[f->ndelayed].ev = ev; f->delayed[f->ndelayed].t = delay; f->delayed[f->ndelayed].target = f; f->delayed[f->ndelayed].broadcast = false; f->ndelayed++; }
}

void fsm_event_delayed_to(Fsm *from, Fsm *to, const char *ev, float delay) {
    if (!from || !to || !ev) return;
    if (delay <= 0) { fsm_event(to, ev); return; }
    if (from->ndelayed < 16) { from->delayed[from->ndelayed].ev = ev; from->delayed[from->ndelayed].t = delay; from->delayed[from->ndelayed].target = to; from->delayed[from->ndelayed].broadcast = false; from->ndelayed++; }
}

void fsm_broadcast_delayed(Fsm *from, const char *ev, float delay) {
    if (!from || !ev) return;
    if (from->ndelayed < 16) { FDelayed *d = &from->delayed[from->ndelayed++]; d->ev = ev; d->t = delay; d->target = NULL; d->broadcast = true; }
}

void fsm_finish(Fsm *f, FsmAct *a) { (void)f; a->finished = true; }

void fsm_update(Fsm *f, float dt) {
    if (!f || !f->started || f->cur < 0) return;
    if (f->deferStart) { if (busy) return; begin_deferred(f); do_switch(f); flush_pending(); return; }
    // delayed events
    for (int i = 0; i < f->ndelayed; i++) {
        f->delayed[i].t -= dt;
        if (f->delayed[i].t <= 0) {
            const char *ev = f->delayed[i].ev; Fsm *tg = f->delayed[i].target; bool bc = f->delayed[i].broadcast;
            f->delayed[i] = f->delayed[--f->ndelayed]; i--;
            if (bc) { extern void uo_broadcast(const char *ev); uo_broadcast(ev); } else fsm_event(tg, ev);
            if (!f->started) return;
        }
    }
    f->stateTime += dt;
    FState *st = &f->st[f->cur];
    if (all_finished(st)) return;
    busy++;
    for (int i = 0; i < st->nact; i++) {
        FsmAct *a = &st->act[i];
        if (!a->entered || a->finished) continue;
        act_update(f, a, dt);
        if (f->pending >= 0) break;
    }
    busy--;
    if (f->pending < 0 && all_finished(st)) {
        int to = find_transition(f, "FINISHED");
        if (to >= 0) f->pending = to;
    }
    if (!busy) { do_switch(f); flush_pending(); }
}

void fsm_fixed(Fsm *f, float dt) {
    if (!f || !f->started || f->cur < 0 || !f->handleFixed) return;
    if (f->deferStart) return;
    FState *st = &f->st[f->cur];
    busy++;
    for (int i = 0; i < st->nact; i++) {
        FsmAct *a = &st->act[i];
        if (!a->entered || a->finished || !act_has_fixed(a->type)) continue;
        act_fixed(f, a, dt);
        if (f->pending >= 0) break;
    }
    busy--;
    if (f->pending < 0 && all_finished(st)) {
        int to = find_transition(f, "FINISHED");
        if (to >= 0) f->pending = to;
    }
    if (!busy) { do_switch(f); flush_pending(); }
}

void fsm_late(Fsm *f, float dt) {
    if (!f || !f->started || f->cur < 0) return;
    if (f->deferStart) return;
    FState *st = &f->st[f->cur];
    busy++;
    for (int i = 0; i < st->nact; i++) {
        FsmAct *a = &st->act[i];
        if (!a->entered || a->finished || !act_has_late(a->type)) continue;
        act_late(f, a, dt);
        if (f->pending >= 0) break;
    }
    busy--;
    if (!busy) { do_switch(f); flush_pending(); }
}

Fsm *fsm_find_on(UObj *o, const char *name) {
    if (!o) return NULL;
    for (int i = 0; i < o->nfsm; i++) if (!name || !name[0] || !strcmp(o->fsm[i]->name, name)) return o->fsm[i];
    return NULL;
}

/* ---------------- parameters ---------------- */
JVal *praw(FsmAct *a, const char *key) { return jget(a->p, key); }

float jv_float(Fsm *f, JVal *v, float def) {
    if (!v) return def;
    if (v->t == J_NUM) return (float)v->num;
    if (v->t == J_TRUE) return 1; if (v->t == J_FALSE) return 0;
    if (v->t == J_OBJ) {
        JVal *vn = jget(v, "var");
        if (vn) { FVar *x = fsm_var_j(f, vn); if (!x) return def; return x->t == V_INT || x->t == V_ENUM ? (float)x->i : x->t == V_BOOL ? (float)x->b : x->f; }
        if (jget(v, "none")) return def;
    }
    return def;
}
int jv_int(Fsm *f, JVal *v, int def) {
    if (!v) return def;
    if (v->t == J_NUM) return (int)v->num;
    if (v->t == J_TRUE) return 1; if (v->t == J_FALSE) return 0;
    if (v->t == J_OBJ) {
        JVal *vn = jget(v, "var");
        if (vn) { FVar *x = fsm_var_j(f, vn); if (!x) return def; return x->t == V_FLOAT ? (int)x->f : x->t == V_BOOL ? x->b : x->i; }
    }
    return def;
}
bool jv_bool(Fsm *f, JVal *v, bool def) {
    if (!v) return def;
    if (v->t == J_TRUE) return true; if (v->t == J_FALSE) return false;
    if (v->t == J_NUM) return v->num != 0;
    if (v->t == J_OBJ) {
        JVal *vn = jget(v, "var");
        if (vn) { FVar *x = fsm_var_j(f, vn); if (!x) return def; return x->t == V_BOOL ? x->b : x->t == V_INT ? x->i != 0 : x->f != 0; }
    }
    return def;
}
const char *jv_str(Fsm *f, JVal *v, const char *def) {
    if (!v) return def;
    if (v->t == J_STR) return v->s;
    if (v->t == J_OBJ) {
        JVal *vn = jget(v, "var");
        if (vn) { FVar *x = fsm_var_j(f, vn); return x && x->t == V_STRING ? x->s : def; }
    }
    return def;
}
UObj *jv_go(Fsm *f, JVal *v) {
    if (!v || v->t != J_OBJ) return NULL;
    if (jget(v, "owner")) return f->owner;
    JVal *vn = jget(v, "var");
    if (vn) { FVar *x = fsm_var_j(f, vn); return x ? x->go : NULL; }
    JVal *o = jget(v, "o");
    if (o) { int i = jint(o, -1); return (f->tree && i >= 0 && i < f->ntree) ? f->tree[i] : NULL; }
    return NULL;
}

float pf(Fsm *f, FsmAct *a, const char *key, float def) { return jv_float(f, jget(a->p, key), def); }
int pi_(Fsm *f, FsmAct *a, const char *key, int def) { return jv_int(f, jget(a->p, key), def); }
bool pb(Fsm *f, FsmAct *a, const char *key, bool def) { return jv_bool(f, jget(a->p, key), def); }
const char *ps(Fsm *f, FsmAct *a, const char *key, const char *def) { return jv_str(f, jget(a->p, key), def); }
const char *pev(FsmAct *a, const char *key) {
    JVal *v = jget(a->p, key);
    if (!v || v->t != J_OBJ) return NULL;
    return jstr(jget(v, "event"), NULL);
}
FVar *pvar(Fsm *f, FsmAct *a, const char *key) {
    JVal *v = jget(a->p, key);
    if (!v || v->t != J_OBJ) return NULL;
    JVal *vn = jget(v, "var");
    return vn ? fsm_var_j(f, vn) : NULL;
}
bool pnone(Fsm *f, FsmAct *a, const char *key) {
    JVal *v = jget(a->p, key);
    if (!v || v->t == J_NULL) return true;
    if (v->t == J_OBJ && jget(v, "none")) return true;
    if (v->t == J_OBJ && jget(v, "var") && !fsm_var_j(f, jget(v, "var"))) return true;
    return false;
}
UObj *pgo(Fsm *f, FsmAct *a, const char *key) {
    JVal *v = jget(a->p, key);
    if (!v || v->t == J_NULL) return f->owner;      // FsmOwnerDefault left empty = owner
    return jv_go(f, v);
}
int ptmpl(Fsm *f, FsmAct *a, const char *key) {
    JVal *v = jget(a->p, key);
    if (!v || v->t != J_OBJ) return -1;
    JVal *pf_ = jget(v, "prefab");
    if (pf_) return uo_template_by_key(pf_->s);
    JVal *vn = jget(v, "var");
    if (vn) { FVar *x = fsm_var_j(f, vn); if (x && x->obj && jget(x->obj, "prefab")) return uo_template_by_key(jget(x->obj, "prefab")->s); }
    return -1;
}
bool pvec(Fsm *f, FsmAct *a, const char *key, float *x, float *y, float *z) {
    JVal *v = jget(a->p, key);
    if (!v || v->t == J_NULL) return false;
    if (v->t == J_ARR) { if (x) *x = (float)jnum(jat(v, 0), 0); if (y) *y = (float)jnum(jat(v, 1), 0); if (z) *z = (float)jnum(jat(v, 2), 0); return true; }
    if (v->t == J_OBJ) {
        JVal *vn = jget(v, "var");
        if (vn) { FVar *q = fsm_var_j(f, vn); if (!q) return false; if (x) *x = q->v[0]; if (y) *y = q->v[1]; if (z) *z = q->v[2]; return true; }
        if (jget(v, "none")) return false;
        if (jget(v, "x")) { if (x) *x = (float)jnum(jget(v, "x"), 0); if (y) *y = (float)jnum(jget(v, "y"), 0); if (z) *z = (float)jnum(jget(v, "z"), 0); return true; }
    }
    return false;
}

void var_set_float(FVar *v, float x) { if (!v) return; if (v->t == V_INT) v->i = (int)x; else v->f = x; }
void var_set_bool(FVar *v, bool b) { if (v) v->b = b; }
void var_set_int(FVar *v, int i) { if (!v) return; if (v->t == V_FLOAT) v->f = (float)i; else v->i = i; }

// does any state (or a global transition) of this FSM react to the event? (the nail's TAKE DAMAGE goes to anything
// it touches that listens for it: breakable walls on the terrain layer, grass, bone gates...)
bool fsm_listens(Fsm *f, const char *ev) {
    if (!f || !ev) return false;
    for (int i = 0; i < f->ngl; i++) if (f->gl[i].ev && !strcmp(f->gl[i].ev, ev)) return true;
    for (int s = 0; s < f->nst; s++) for (int i = 0; i < f->st[s].ntr; i++) if (f->st[s].tr[i].ev && !strcmp(f->st[s].tr[i].ev, ev)) return true;
    return false;
}
