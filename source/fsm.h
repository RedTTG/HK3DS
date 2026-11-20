// PlayMaker FSM runtime: states, transitions, global transitions, variables, events, and the action lifecycle
// (OnEnter / OnUpdate / OnFixedUpdate / OnLateUpdate / OnExit / Finish -> FINISHED), following PlayMaker 1.9 as the
// game uses it. Actions are implemented in fsm_actions.c from the decompiled sources.
#pragma once
#include <stdbool.h>
#include "json.h"
#include "uo.h"

#define FVAR_STR 160
typedef enum { V_NONE, V_FLOAT, V_INT, V_BOOL, V_STRING, V_VEC2, V_VEC3, V_GO, V_OBJ, V_COLOR, V_ENUM, V_ARRAY } VType;

typedef struct {
    const char *name; VType t;
    float f; int i; bool b; char s[FVAR_STR];
    float v[4];
    UObj *go; JVal *obj;
} FVar;

typedef struct FsmAct {
    int type; const char *tname;
    bool enabled, entered, finished;
    JVal *p;                         // params object
    // runtime scratch
    float t0, t1, t2, t3; int i0, i1, i2; bool b0, b1, b2; UObj *o0, *o1; float f[6];
} FsmAct;

typedef struct { const char *ev; int to; } FTrans;
typedef struct { const char *name; int ntr; FTrans *tr; int nact; FsmAct *act; } FState;

typedef struct { const char *ev; float t; Fsm *target; bool broadcast; } FDelayed;   // broadcast: every FSM (BroadcastAll)

struct Fsm {
    const char *name;
    UObj *owner;
    UObj **tree; int ntree;          // objects of the instantiated tree ({"o": i} references)
    JVal *def;
    int nvar; FVar *var;
    int nst; FState *st;
    int ngl; FTrans *gl;
    int start, cur, prev;
    bool started, enabled, inEnter;
    bool deferStart;                 // spawned by a script: the start state is entered once that script's actions are done
    int pending;                     // state to switch to (-1 none)
    float stateTime;
    unsigned nswitch;                // state changes so far (callbacks stop when it moves)
    FDelayed delayed[16]; int ndelayed;
    // last event data (Fsm.EventData)
    int evInt; float evFloat; const char *evStr;
    const char *lastEvent;           // Fsm.LastTransition event (GetLastEvent)
    bool handleFixed, wantsTriggers;
};

Fsm *fsm_create(JVal *def, UObj *owner, UObj **tree, int ntree);
bool fsm_listens(Fsm *f, const char *ev);
void fsm_free(Fsm *f);
void fsm_start(Fsm *f);
void fsm_start_new(Fsm *f);         // a newly created object's FSM (PlayMakerFSM.Start: after the creating action list)
void fsm_stop(Fsm *f);
void fsm_event(Fsm *f, const char *ev);              // Fsm.Event (immediate transition)
void fsm_event_delayed(Fsm *f, const char *ev, float delay);
void fsm_event_delayed_to(Fsm *from, Fsm *to, const char *ev, float delay);
void fsm_broadcast_delayed(Fsm *from, const char *ev, float delay);      // a delayed BroadcastAll (one entry, not one per FSM)   // DelayedEvent with an event target
void fsm_update(Fsm *f, float dt);
void fsm_fixed(Fsm *f, float dt);
void fsm_late(Fsm *f, float dt);
FVar *fsm_var(Fsm *f, const char *name);
FVar *fsm_var_j(Fsm *f, JVal *varNameNode);       // cached lookup of a {"var": name} name node           // local variable, else a PlayMaker global
Fsm *fsm_create_vars(const char *name, UObj *owner, const char **names, const VType *types, int n);   // variables only (native stand-ins)
void fsm_globals_bind_go(const char *name, UObj *o);
void fsm_globals_clear(void);
Fsm *fsm_find_on(UObj *o, const char *fsmName);     // FSMUtility.LocateFSM
void fsm_finish(Fsm *f, FsmAct *a);
const char *fsm_state_name(Fsm *f);

// parameter helpers (resolve variables)
float  pf(Fsm *f, FsmAct *a, const char *key, float def);
int    pi_(Fsm *f, FsmAct *a, const char *key, int def);
bool   pb(Fsm *f, FsmAct *a, const char *key, bool def);
const char *ps(Fsm *f, FsmAct *a, const char *key, const char *def);
const char *pev(FsmAct *a, const char *key);        // event name or NULL
FVar  *pvar(Fsm *f, FsmAct *a, const char *key);      // variable written by the action (NULL when not a variable)
bool   pnone(Fsm *f, FsmAct *a, const char *key);     // FsmFloat.IsNone (unset variable)
UObj  *pgo(Fsm *f, FsmAct *a, const char *key);        // FsmOwnerDefault / FsmGameObject
int    ptmpl(Fsm *f, FsmAct *a, const char *key);      // prefab template index (-1)
bool   pvec(Fsm *f, FsmAct *a, const char *key, float *x, float *y, float *z);
JVal  *praw(FsmAct *a, const char *key);
float  jv_float(Fsm *f, JVal *v, float def);
bool   jv_bool(Fsm *f, JVal *v, bool def);
int    jv_int(Fsm *f, JVal *v, int def);
UObj  *jv_go(Fsm *f, JVal *v);
const char *jv_str(Fsm *f, JVal *v, const char *def);
void   var_set_float(FVar *v, float x);
void   var_set_bool(FVar *v, bool b);
void   var_set_int(FVar *v, int i);

// actions (fsm_actions.c)
int  act_lookup(const char *name);
void act_enter(Fsm *f, FsmAct *a);
void act_update(Fsm *f, FsmAct *a, float dt);
void act_fixed(Fsm *f, FsmAct *a, float dt);
void act_late(Fsm *f, FsmAct *a, float dt);
void act_exit(Fsm *f, FsmAct *a);
bool act_has_fixed(int type);
bool act_has_late(int type);
bool act_is_trigger(int type);
void act_collision_notify(UObj *o, int kind);
void act_collision_hero(UObj *o, int kind);            // the Knight's body against the owner's solid collider          // terrain collision: 0 enter, 1 stay, 2 exit
void act_trigger_notify(UObj *o, UObj *other, int kind); // trigger overlap: 0 enter, 1 stay, 2 exit
void fsm_flush(void);
