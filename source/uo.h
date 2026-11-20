// "Unity objects": a small GameObject runtime for the converted scene objects (enemies and what they spawn).
// Transforms, colliders, Rigidbody2D on the collision grid, tk2d sprite animators, the game's MonoBehaviours
// (ported in comps.c) and PlayMaker FSMs (fsm.c). Loaded from <room>.obj (tools/build_objs.py).
#pragma once
#include <stdbool.h>
#include "gfx.h"
#include "json.h"

#define UO_MAX_OBJS   4096
#define UO_MAX_COLS   4
#define UO_MAX_OV     12

typedef struct UObj UObj;
typedef struct Fsm Fsm;

typedef enum { COL_BOX, COL_CIRCLE, COL_POLY, COL_EDGE } ColType;
typedef struct {
    ColType t; bool trig, en;
    bool sprBox;                 // hurtbox taken from the current sprite frame (tk2d sprite colliders aren't exported)
    float ox, oy, w, h, r;       // local (box: offset/size, circle: offset/radius, poly/edge: bounds of the points)
    float *pts; int npts;        // local points (poly: first path only, edge: chain)
} UCol;

// tk2dSpriteAnimator
typedef struct {
    bool has, en;
    int lib;                     // index into the room's library table
    int clip;                    // anim index in R.enemyset (-1 none)
    float clipTime, fps;         // clipTime in frames
    int frame, prevFrame;
    bool playing;
    bool done;                   // a once-clip reached its end this frame (complete callback)
    int trig;                    // frame trigger fired this frame (frame index) or -1
    float speed;                 // ClipFps override multiplier (1)
    int tkClip;                  // clip+1 whose per-frame collider row is cached (0: none looked up)
    void *tkRow;                 // that row (JVal array of [cx, cy, ex, ey] / 1 (no collider) / 0 per frame), NULL if the clip has none
    int tkpClip; void *tkpRow;   // the same for the per-frame mesh outline bounds (tkpoly)
} UAnim;

// Rigidbody2D
typedef struct {
    bool has, kin, sim;
    float g, vx, vy;
    int contacts, prevContacts;  // terrain contact sides (bits: 1 bottom, 2 top, 4 left, 8 right)
    bool collidedThisStep;
    float angVel;
    float pvx, pvy;              // velocity before the last step's collisions
    float fric;                  // ground friction (corpses: Unity's default physics material, 0.4)
    float restT; bool asleep;    // Box2D sleep: resting on the ground for 0.5 s
    float sleepX, sleepY; int sleepChk;
} URb;

// SpriteRenderer, or a tk2dSprite with no animator (one fixed frame)
typedef struct { bool has, en; int frame, set; float r, g, b, a; bool fx, fy; int order; bool tk; char coll[9]; bool lit, cutout; } USr;   // set: 0 room sprites, 1 icons; lit: on the game's lit sprite material (tinted by the scene's ambient light); cutout: on Sprites-Darkness-Cutout, never drawn (it only cuts the darkness vignette open at exits)
// AudioSource (clip name in romfs:/sfx)
typedef struct { bool has, en, loop, awake, on; const char *clip; float vol, pitch, min, max, sb; int h, roll, slot; } UAs;   // on/slot: a looping source playing
// TextMeshPro
typedef struct { bool has, en; char *text; float size; int align, font; float r, g, b, a, rw, px, rh, py; } UTmp;

typedef enum {
    C_UNKNOWN, C_HEALTH, C_DAMAGEHERO, C_WALKER, C_RECOIL, C_DEATHFX, C_CORPSE, C_ALERTRANGE, C_LOS, C_DEACT_ANIM,
    C_NONBOUNCER, C_PROXY2D, C_CLIMBER, C_CRAWLER, C_GEO, C_RECYCLE_ANIM, C_AUTORECYCLE, C_PLAYAUDIO, C_BOUNCE, C_PBI, C_FSMACTIVATOR, C_BULLET,
    C_DEACT_PD_TRUE, C_DEACT_PD_FALSE, C_FADEGROUP,
    // comps2.c
    C_BREAKABLE, C_POLE, C_POLETOP, C_VINE, C_GEOROCK, C_DROPPLAT, C_LIFTPLAT, C_FLIPPLAT, C_BOUNCESHROOM, C_HEALTHCOCOON,
    C_STALACTITE, C_SHADOWGATE, C_CONVEYOR, C_CONVEYORZONE, C_CONVEYORSPEED, C_MUSICREGION, C_SETPOS_PD, C_DARK_NOLANTERN,
    C_BRIDGELEVER, C_BRIDGESECTION, C_JELLYEGG, C_ACTCHILDREN, C_ENEMYMSG, C_WAKEREGION, C_DAMAGEENEMIES, C_PII, C_SLOPEPLAT,
    C_RUINSLIFT, C_LIFTCHAIN, C_DREAMPLAT, C_TRIGGERENTER, C_COLLENTER, C_PUSHRUBBLE, C_GRUBBG, C_TOUCHSHAKE, C_DMGTICKER, C_EXTRADMG,
    C_SPRANIM, C_SPINSELF, C_SPINSIMPLE, C_SETZ, C_SETZRAND, C_RANDROT, C_RANDROTRANGE, C_RANDSCALE, C_DEACTDELAY, C_DISABLETIME,
    C_SNAPGROUND, C_SOULORB, C_SCUTTLER, C_EVENTREG, C_VINEPLAT, C_VINECUT, C_STEEPSLOPE, C_KILLLISTENER, C_DREAMPLANT, C_DREAMORB, C_DREAMREACT, C_INFECTEDFX, C_UNINFFX, C_SPRFADE, C_CENTIPEDE, C_CENTISECTION, C_BOSSSCENE, C_BOSSSEQDOOR, C_SPATTER, C_COLORFADER, C_ACTIONICON, C_WAVE, C_PSRECYCLE, C_SENDONENABLE, C_SPAWNJAR,
    C_PS,                        // a particle system (particles.c)
    C_COUNT
} CompType;

typedef struct UComp {
    CompType type; bool en, awoken;
    const char *cls;
    JVal *f;                     // serialized fields
    void *st;                    // component runtime state (comps.c)
} UComp;

struct UObj {
    bool used, activeSelf, destroyed, started;
    char name[40];
    UObj *parent, *child, *next;   // hierarchy (child = first child)
    float lx, ly, lz, sx, sy, rz;  // local transform (rz degrees)
    int layer; char tag[24];
    // colliders: up to UO_MAX_COLS kept in the object (all but ten objects in the game), more on the heap (uo.c add_col):
    // Fungus2_21's and Queen's Station's thorn vines are 11 and 7 polygons on one object; past the 4th they had no hitbox
    UCol *col; int ncol, capcol; UCol colIn[UO_MAX_COLS];
    URb rb;
    UAnim an;
    bool mr, mrSet;                // MeshRenderer enabled (mrSet: the object has one)
    float tintR, tintG, tintB, tintA, flash;   // tk2dSprite color / hit flash
    UComp *comp; int ncomp;
    Fsm **fsm; int nfsm;
    signed char tdk;             // 0 unknown, 1/2: no FSM / an FSM listens for TAKE DAMAGE (nail hits)
    signed char dhk;             // 0 unknown, 1 none, 2: has a damages_hero FSM (dhf), which hurts the Knight like DamageHero
    struct Fsm *dhf;
    int tmpl;                      // template (prefab) index this came from, -1 = scene object
    float life;                    // time since spawn
    struct JVal *uan; float uanT;  // Mecanim Animator clip ('uan', tools/uanim.py) and its time
    int lastSwing;                 // nail swing that last hit it
    // cached world transform (valid for the current frame)
    float wx, wy, wsx, wsy, wrz; int wframe;
    float destroyT;                // Object.Destroy(obj, delay)
    bool pseudo;                   // stand-in for the Knight / terrain (no update, no drawing)
    UObj *ov[UO_MAX_OV]; int nov;  // trigger overlaps last physics step
    void (*native)(UObj *o, const char *ev);   // pseudo objects backed by native code (PlayMaker globals): events
    USr sr; UTmp tmp;
    bool hud;                      // a HUD-camera object (UI layer prefab): drawn in screen space over the HUD
    unsigned char bank;            // asset bank: 0 the room, 1 the Knight (persistent across rooms)
    bool persist;                  // survives room unloads (the Knight and the global stand-ins)
    bool spawned;                  // instantiated from a prefab at runtime (not part of the scene)
    UObj **treeArr; int treeN;
    UAs as;
    bool additive;                 // glow / light sprite: drawn with additive blending
    signed char pass;              // draw pass (worked out on first draw): 1 by depth, 2 just behind the Knight, 3 after everything, 4 never     // the instantiated tree this object belongs to ({"o": i} references)
};

// world
void uo_reset(void);
UObj *uo_global(const char *name);              // PlayMaker global GameObject stand-ins (Hero, DialogueManager...)
UObj *uo_alloc_pseudo(const char *name);        // a native-backed object (prompt markers...)
UObj *uo_find_tag(const char *tag);             // GameObject.FindWithTag
void uo_preload_sprites(const char *room);       // the next room's object sprite pages, before its art (see uo.c)
void uo_native_event(UObj *o, const char *ev);  // deliver to o's native handler (if any)
bool uo_load_room(const char *room);           // romfs:/rooms/<room>.obj; false if missing
void uo_init_persistent(void);                  // the Knight's objects + global stand-ins (once per game)
void uo_shutdown(void);
bool uo_has_knight(void);
UObj *uo_door_fsm(const char *name);
bool uo_terrain_hit(float x0, float y0, float x1, float y1);
void uo_terrain_narrow(bool on, float x0, float y0, float x1, float y1);   // limit uo_terrain_hit to boxes touching these bounds
bool uo_platform_delta(float x0, float x1, float feetY, float *dx, float *dy);   // the moving platform under these feet   // solid colliders of live objects on the Terrain layer                       // the Knight's FSMs (spells, focus, superdash...) are running                         // drop everything (back to the menu)
const SpriteSet *uo_set_of(const UObj *o);
void uo_unload(void);
bool uo_room_active(void);
void uo_fixed(float dt);                        // FixedUpdate + physics (50 Hz)
void uo_update(float dt);                       // Update + animators
void uo_late_update(float dt);
void uo_draw(float camx, float camy, float alpha);   // sprites on the gameplay plane
void uo_draw_range(float camx, float camy, float flo, float fhi);   // objects with parallax factor in [flo, fhi)
void uo_draw_begin(void);   // start of a frame's drawing: the sorted object list is rebuilt once
void uo_draw_debug(void (*rect)(float, float, float, float, u32));
void uo_draw_hud(void);
void uo_draw_last(float camx, float camy);       // the Knight's vignette: after everything else                          // HUD-camera objects (item / charm messages)
int uo_sprite_frame(const char *guid);           // room sprite frame of a Sprite asset (-1)
JVal *uo_template_objects(int ti);
int uo_template_count(void);
const char *uo_template_key(int ti);
JVal *uo_comp_fields(UObj *o, const char *cls);   // serialized fields of a MonoBehaviour on o (or a child)
void uo_set_text(UObj *o, const char *text);

// objects
void uo_world(UObj *o);                         // refresh world transform
float uo_wx(UObj *o); float uo_wy(UObj *o); float uo_wsx(UObj *o); float uo_wsy(UObj *o); float uo_wrz(UObj *o);
void uo_set_world_pos(UObj *o, float x, float y);
bool uo_active(const UObj *o);                  // active in hierarchy
void uo_set_active(UObj *o, bool a);
UObj *uo_find_child(UObj *o, const char *name);
UObj *uo_find(const char *name);
int uo_sprite_id(UObj *o);   // Tk2dSpriteGetId
UObj *uo_spawn(int tmpl, float x, float y, float rz);   // instantiate a template at world position
int uo_template_by_key(const char *key);        // prefab guid -> template index
void uo_destroy(UObj *o);
UComp *uo_comp(UObj *o, CompType t);
UObj *uo_root(UObj *o);
UObj *uo_obj(int i);
int uo_count(void);
// colliders in world space
bool uo_col_aabb(UObj *o, const UCol *c, float *x0, float *y0, float *x1, float *y1);
bool uo_col_overlap_box(UObj *o, const UCol *c, float x0, float y0, float x1, float y1);
bool uo_body_aabb(UObj *o, float *x0, float *y0, float *x1, float *y1);   // first non-trigger collider
// sprite animator
void uo_anim_play(UObj *o, const char *clip, bool restartIfSame);
void uo_anim_play_from(UObj *o, const char *clip, int frame);
bool uo_anim_is(UObj *o, const char *clip);
int  uo_anim_clip(UObj *o, const char *clip);
float uo_anim_clip_len(UObj *o, const char *clip);
void uo_anim_play_frame(UObj *o, int frame);    // PlayFromFrame on the current clip
void uo_anim_set_callbacks(UObj *o, Fsm *f, const char *complete, const char *trigger, bool setComplete, bool setTrigger);
void uo_set_parent(UObj *o, UObj *parent, bool resetLocal);
void uo_destroy_after(UObj *o, float t);
const char *uo_room_name(void);
UObj *uo_hero(void);                            // the Knight stand-in (tag Player, layer 9)
UObj *uo_hero_slash(void);                      // active nail slash (layer 17, tag Nail Attack)
// audio: an object's AudioSource / a clip at the object's position (rolloff with distance from the camera / Knight)
int  uo_play_at(UObj *o, const char *clip, float vol, float pitch);
int  uo_source_play(UObj *o);                   // AudioSource.Play
void uo_source_stop(UObj *o);
void uo_update_loops(void);      // looping AudioSources: the loudest few play, by distance from the camera
// events
void uo_send_event(UObj *o, const char *ev);    // FSMUtility.SendEventToGameObject: every FSM on the object
void uo_broadcast(const char *ev);
UObj *uo_event_sender(void);              // GetEventSender
void uo_set_event_sender(UObj *o);
UObj *uo_slash_obj(void);
bool uo_has_component_named(UObj *o, const char *cls);
UComp *uo_comp_by_cls(UObj *o, const char *cls);     // a MonoBehaviour by class name (ported or not)

// rooms: per-room sprite libraries
extern SpriteSet *uo_spriteset;
const char *uo_lib_key(int lib);

// hero hooks used by components (game.c / hero.c)
void uo_hero_box(float *x0, float *y0, float *x1, float *y1);   // Knight collider (layer 9)
void uo_hero_hurtbox(float *x0, float *y0, float *x1, float *y1);
float uo_hero_x(void); float uo_hero_y(void);
bool uo_hero_slash_box(float *x0, float *y0, float *x1, float *y1);   // false when no slash is active
extern int uo_frame;
extern float uo_time;
extern float uo_ambient[3];          // the scene's ambient light (RenderSettings.ambientLight): tints 'lit' sprites
