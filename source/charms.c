// Charms: equipped state lives in PlayerData (equippedCharm_N, charmCost_N, charmSlots, overcharmed) like the game.
// The effects are the HeroController / Spell Control numbers the charms switch (see charms.h).
#include "charms.h"
#include "pd.h"
#include "hero.h"
#include <stdio.h>

static char nb[32];
static const char *key(const char *fmt, int id) { snprintf(nb, sizeof nb, fmt, id); return nb; }

bool charm_owned(int id) { return pd_bool(key("gotCharm_%d", id)); }
bool charm_on(int id) { return pd_bool(key("equippedCharm_%d", id)); }
// equipped and working: the fragile charms do nothing while broken (brokenCharm_23/24/25)
bool charm_act(int id) {
    if (!charm_on(id)) return false;
    if (id >= 23 && id <= 25 && pd_bool(key("brokenCharm_%d", id))) return false;
    return true;
}
int charm_cost(int id) { return pd_int(key("charmCost_%d", id)); }
int charm_slots(void) { return pd_int("charmSlots"); }
int charm_slots_filled(void) {
    int n = 0;
    for (int i = 1; i <= CHARM_COUNT; i++) if (charm_on(i)) n += charm_cost(i);
    return n;
}
bool charm_overcharmed(void) { return pd_bool("overcharmed"); }

// PlayerData.CalculateNotchesUsed + the inventory's equip rule: a charm fits if at least one notch is free
bool charm_toggle(int id) {
    if (!charm_owned(id)) return false;
    if (charm_on(id)) {
        pd_set_bool(key("equippedCharm_%d", id), false);
    } else {
        if (charm_slots_filled() >= charm_slots()) return false;
        pd_set_bool(key("equippedCharm_%d", id), true);
    }
    int used = charm_slots_filled();
    pd_set_int("charmSlotsFilled", used);
    pd_set_bool("overcharmed", used > charm_slots());
    return true;
}

/* ---------------- effects ---------------- */
int charm_nail_damage(void) {
    int d = pd_int("nailDamage");
    float m = 1;
    if (charm_act(25)) m *= 1.5f;                          // Fragile / Unbreakable Strength
    if (charm_act(6) && P.health == 1) m *= 1.75f;         // Fury of the Fallen
    return (int)(d * m + 0.5f);
}
int charm_soul_per_hit(int base) {                         // HeroController.SoulGain
    int s = base;
    if (charm_act(20)) s += 3;                              // Soul Catcher
    if (charm_act(21)) s += 8;                              // Soul Eater
    return s;
}
float charm_focus_time(float base) {                       // Spell Control: Quick Focus / Deep Focus
    float t = base;
    if (charm_act(7)) t *= 0.66f;
    if (charm_act(34)) t *= 1.65f;
    return t;
}
int charm_focus_heal(void) { return charm_act(34) ? 2 : 1; }
float charm_run_speed(float base) {
    if (charm_act(37)) return charm_act(31) ? 11.5f : 10.0f;  // RUN_SPEED_CH(_COMBO)
    return base;
}
float charm_dash_cooldown(float base) { return charm_act(31) ? 0.4f : base; }          // DASH_COOLDOWN_CH
float charm_attack_cooldown(float base) { return charm_act(32) ? 0.25f : base; }       // ATTACK_COOLDOWN_TIME_CH
float charm_invul_time(float base) { return charm_act(4) ? 1.75f : base; }             // INVUL_TIME_STAL
float charm_nail_scale(void) {                              // NailSlash: Longnail / Mark of Pride
    if (charm_act(18) && charm_act(13)) return 1.4f;
    if (charm_act(13)) return 1.25f;
    if (charm_act(18)) return 1.15f;
    return 1;
}
bool charm_steady_body(void) { return charm_act(14); }
int charm_damage_soul(void) { return charm_act(3) ? (charm_act(35) ? 25 : 15) : 0; }    // Grubsong (+ Grubberfly's Elegy)
int charm_damage_taken(int amount) { return charm_overcharmed() ? amount * 2 : amount; }
bool charm_any_owned(void) {
    if (pd_bool("hasCharm")) return true;
    for (int i = 1; i <= CHARM_COUNT; i++) if (charm_owned(i)) return true;
    return false;
}

// HeroController.Die: equipped fragile charms break (unless made unbreakable)
void charm_on_death(void) {
    static const char *unb[3] = { "fragileHealth_unbreakable", "fragileGreed_unbreakable", "fragileStrength_unbreakable" };
    for (int id = 23; id <= 25; id++)
        if (charm_on(id) && !pd_bool(unb[id - 23])) pd_set_bool(key("brokenCharm_%d", id), true);
}
int charm_geo_drop(int n) { return charm_act(24) ? (int)(n * 1.2f + 0.99f) : n; }       // Fragile Greed: +20%
// blue masks: Lifeblood Heart +2, Lifeblood Core +4, Joni's Blessing: 1.4 x max health as lifeblood
int charm_blue_max(int maxHealth) {
    int b = 0;
    if (charm_act(8)) b += 2;
    if (charm_act(9)) b += 4;
    if (charm_act(27)) b += (int)(maxHealth * 1.4f);
    return b;
}
bool charm_joni(void) { return charm_act(27); }
bool charm_hiveblood(void) { return charm_act(29); }
bool charm_compass(void) { return charm_act(2); }
