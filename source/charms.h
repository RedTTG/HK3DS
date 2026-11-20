#pragma once
#include <stdbool.h>

#define CHARM_COUNT 40

bool charm_owned(int id);
bool charm_on(int id);
int  charm_cost(int id);
int  charm_slots(void);
int  charm_slots_filled(void);
bool charm_overcharmed(void);
bool charm_toggle(int id);
bool charm_any_owned(void);              // equip / unequip (false: no free notch)

int   charm_nail_damage(void);
int   charm_soul_per_hit(int base);
float charm_focus_time(float base);
int   charm_focus_heal(void);
float charm_run_speed(float base);
float charm_dash_cooldown(float base);
float charm_attack_cooldown(float base);
float charm_invul_time(float base);
float charm_nail_scale(void);
bool  charm_steady_body(void);
int   charm_damage_soul(void);
int   charm_damage_taken(int amount);
bool  charm_act(int id);                 // equipped and not broken
void  charm_on_death(void);
int   charm_geo_drop(int n);
int   charm_blue_max(int maxHealth);
bool  charm_joni(void);
bool  charm_hiveblood(void);
bool  charm_compass(void);
