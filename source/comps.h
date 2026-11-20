// The game's MonoBehaviours, ported (HealthManager, Walker, Recoil, DamageHero, EnemyDeathEffects, Corpse,
// AlertRange, LineOfSightDetector, DeactivateAfter2dtkAnimation, ...).
#pragma once
#include "uo.h"

CompType comp_type_of(const char *cls);
void comp_awake(UObj *o, UComp *c);
void comp_start(UObj *o, UComp *c);
void comp_enable(UObj *o, UComp *c);
void comp_disable(UObj *o, UComp *c);
void comp_update(UObj *o, UComp *c, float dt);
void comp_fixed(UObj *o, UComp *c, float dt);
void comp_collision(UObj *o, UComp *c, bool enter);     // OnCollisionEnter2D / Stay2D with terrain
void comp_free(UComp *c);

// HealthManager / hit handling (nail, spells)
typedef enum { ATK_NAIL = 0, ATK_GENERIC = 1, ATK_SPELL = 2, ATK_ACID = 3, ATK_SPLATTER = 4, ATK_RUINSWATER = 5, ATK_SHARPSHADOW = 6, ATK_NAILBEAM = 7 } AttackType;
bool hm_hit(UObj *o, int damage, float direction, AttackType type, float magnitude);   // true if it took the hit
bool hm_is_dead(UObj *o);
bool hm_invincible(UObj *o);
void hm_set_special(UObj *o, bool sp);
void hm_set_damage_override(UObj *o, bool d);
bool hm_died(UObj *o);
void hm_set_battle_scene(UObj *o, UObj *bs);
int hm_get_hp(UObj *o);         // dead, or a special death (the boss FSM takes over)
void hm_set_invincible(UObj *o, bool inv);
void hm_set_invincible_dir(UObj *o, int dir);
void hm_set_geo(UObj *o, int small, int medium, int large);
int  hm_hp(UObj *o);
void hm_set_hp(UObj *o, int hp);
void hm_set_dead(UObj *o, bool d);
void hm_set_death_reset(UObj *o, bool r);
void hm_die(UObj *o, float direction, AttackType type);
bool alert_in_range(UObj *rangeObj);
bool los_can_see(UObj *o);
void walker_start(UObj *o); void walker_stop(UObj *o, int reason); void walker_go(UObj *o, int facing);
void walker_change_facing(UObj *o, int facing); void walker_clear_turn_cooldown(UObj *o);
void recoil_set_speed(UObj *o, float s);
void recoil_set_freeze(UObj *o, bool freeze);
int  damagehero_amount(UObj *o);
void damagehero_set(UObj *o, int dmg);
int uo_nail_hits(float x0, float y0, float x1, float y1, int swing, float angle, int damage);   // 1 touched enemy, 2 NonBouncer, 4 damaged

void fadegroup_set(UObj *o, bool up, bool fast);   // FadeGroupUp / FadeGroupDown

void comps_recoil_by_direction(UObj *o, float angle, float mag);   // Recoil.RecoilByDirection (angle in degrees)
