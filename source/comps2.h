// comps2.c: more ported MonoBehaviours (breakables, platforms, conveyors, persistence...)
#pragma once
#include "uo.h"
void comp2_awake(UObj *o, UComp *c);
void comp2_start(UObj *o, UComp *c);
void comp2_update(UObj *o, UComp *c, float dt);
void comp2_fixed(UObj *o, UComp *c, float dt);
void comp2_collision(UObj *o, UComp *c, bool enter);
bool comp2_hit(UObj *o, float dir, int atk, UObj *src);     // IHitResponder.Hit (Breakable)
bool comp2_is_responder(UObj *o);
void comp2_shroom_nail(UObj *o, bool effects);                // NailSlash.Bounce on a bounce shroom
void comp2_trigger(UObj *o, UObj *other, int kind);          // OnTriggerEnter2D / Stay2D / Exit2D: 0 / 1 / 2
bool comp2_wants_triggers(UObj *o);
void comp2_hero_touch(UObj *o, int side, int kind);          // the Knight's body against o's solid collider
void comps_save_persistent(void);
bool comp2_call(UObj *o, const char *behaviour, const char *method, float a0, float *res);
// HitTaker.Hit: HealthManager and the IHitResponders on the target and up to two parents
void comps_hit_taker(UObj *t, int damage, float dir, int atk, float mag, UObj *src);
void comp3_awake(UObj *o, UComp *c);
void comp3_enable(UObj *o, UComp *c);
void comp3_start(UObj *o, UComp *c);
void comp3_update(UObj *o, UComp *c, float dt);
bool comp3_hit(UObj *o, UComp *c, float dir, int atk);
void comp3_trigger(UObj *o, UComp *c, UObj *other, int kind);
void comp3_hero_touch(UObj *o, UComp *c, int side, int kind);
bool comp3_wants_triggers(UObj *o);
bool comp3_is_responder(UObj *o);
void comps_event_register_send(const char *ev);
bool comp3_pbi_get(UObj *o, bool *val);
bool comp3_pbi_set(UObj *o, bool val);
void comps_dream_impact(UObj *target);
void comps_dream_make_ready(UObj *o);
bool comps_hit_effect(UObj *o, float dir);
