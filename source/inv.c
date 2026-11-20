// The Charms pane of the inventory (_GameCameras/HudCamera/Inventory/Charms, "UI Charms" FSM), on the bottom screen.
// Grid: the 40 backboards ("BB 1".."BB 40", CharmBackboard.charmNum) in the game's staggered 4 x 10 layout.
// Equipping only works at a bench (PlayerData atBench); equipping past the notches follows "Overcharm Check":
// with canOvercharm it overcharms at once, otherwise attempts 1-2 are rejected, 3 / 4 crack, 5 breaks and sets canOvercharm.
#include "inv.h"
#include "charms.h"
#include "input.h"
#include "game.h"
#include "gfx.h"
#include "pd.h"
#include "lang.h"
#include "audio.h"
#include "hero.h"
#include "uo.h"
#include "journal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const int SLOT_CHARM[40] = { 2, 1, 4, 20, 19, 21, 31, 37, 3, 35, 23, 24, 25, 33, 14, 15, 32, 18, 13, 6,
                                    12, 5, 11, 10, 22, 7, 34, 8, 9, 27, 29, 17, 16, 28, 26, 39, 30, 38, 40, 36 };
// backboard x offsets of each row's first slot (rows 1 and 3 start 0.91 units left of rows 2 and 4)
static const float ROW_X0[4] = { -7.92f, -7.01f, -7.92f, -7.01f };
static const float ROW_Y[4] = { -8.34f, -9.79f, -11.26f, -12.72f };

static struct {
    bool open;
    int pane;           // 0 Inventory (items), 1 Charms, 2 Journal (once the Hunter has given it)
    int isel;           // selected item row
    int sel;            // 0..39 grid slot, or 100+n for the n-th equipped charm
    int attempts;       // Overcharm Attempts (reset when the pane opens)
    float shake, flash; // crack / reject feedback
    int crack;          // 0, 1, 2 cracks shown
} V;

bool inv_open(void) { return V.open; }

/* ---------------- the Inventory pane: nail, spells, abilities and key items (InvItemDisplay) ---------------- */
typedef struct { char key[32]; int count; } Item;
static int build_items(Item *it) {
    int n = 0; char k[32];
#define ADD(K, C) do { snprintf(it[n].key, sizeof it[n].key, "%s", K); it[n].count = (C); n++; } while (0)
    snprintf(k, sizeof k, "NAIL%d", 1 + (pd_int("nailSmithUpgrades") > 4 ? 4 : pd_int("nailSmithUpgrades"))); ADD(k, 0);
    ADD("SPELL_FOCUS", 0);
    if (pd_int("fireballLevel") > 0) ADD(pd_int("fireballLevel") >= 2 ? "SPELL_FIREBALL2" : "SPELL_FIREBALL1", 0);
    if (pd_int("quakeLevel") > 0) ADD(pd_int("quakeLevel") >= 2 ? "SPELL_QUAKE2" : "SPELL_QUAKE1", 0);
    if (pd_int("screamLevel") > 0) ADD(pd_int("screamLevel") >= 2 ? "SPELL_SCREAM2" : "SPELL_SCREAM1", 0);
    if (pd_bool("hasCyclone")) ADD("ART_CYCLONE", 0);
    if (pd_bool("hasDashSlash")) ADD("ART_DASH", 0);
    if (pd_bool("hasUpwardSlash")) ADD("ART_UPPER", 0);
    if (pd_bool("hasShadowDash")) ADD("SHADOWDASH", 0); else if (pd_bool("hasDash") || P.hasDash) ADD("DASH", 0);
    if (pd_bool("hasWalljump") || P.hasWalljump) ADD("WALLJUMP", 0);
    if (pd_bool("hasSuperDash")) ADD("SUPERDASH", 0);
    if (pd_bool("hasDoubleJump") || P.hasDoubleJump) ADD("DOUBLEJUMP", 0);
    if (pd_bool("hasAcidArmour")) ADD("ACIDARMOUR", 0);
    if (pd_bool("hasDreamNail")) ADD(pd_bool("dreamNailUpgraded") ? "DREAMNAIL_B" : "DREAMNAIL_A", pd_int("dreamOrbs"));
    if (pd_bool("hasDreamGate")) ADD("DREAMGATE", 0);
    if (pd_bool("hasLantern")) ADD("LANTERN", 0);
    if (pd_bool("hasMap")) ADD(pd_bool("hasQuill") ? "MAPQUILL" : "MAP", 0); else if (pd_bool("hasQuill")) ADD("QUILL", 0);
    if (pd_bool("hasTramPass")) ADD("TRAM_PASS", 0);
    if (pd_bool("hasCityKey")) ADD("CITYKEY", 0);
    if (pd_bool("hasSlykey")) ADD("STOREKEY", 0);
    if (pd_bool("hasWhiteKey")) ADD("WHITEKEY", 0);
    if (pd_bool("hasLoveKey")) ADD("LOVEKEY", 0);
    if (pd_bool("hasKingsBrand")) ADD("KINGSBRAND", 0);
    if (pd_int("simpleKeys") > 0) ADD("SIMPLEKEY", pd_int("simpleKeys"));
    if (pd_int("rancidEggs") > 0) ADD("RANCIDEGG", pd_int("rancidEggs"));
    if (pd_int("ore") > 0) ADD("ORE", pd_int("ore"));
    for (int t = 1; t <= 4; t++) { char tk[16], pk[16]; snprintf(pk, sizeof pk, "trinket%d", t); snprintf(tk, sizeof tk, "TRINKET%d", t); if (pd_int(pk) > 0) ADD(tk, pd_int(pk)); }
    if (pd_bool("hasXunFlower")) ADD(pd_bool("xunFlowerBroken") ? "FLOWER_BROKEN" : "FLOWER", 0);
    { int hp = pd_int("heartPieces") % 4; snprintf(k, sizeof k, "HEARTPIECE_%d", hp); ADD(k, 0); }
    { int vf = pd_int("vesselFragments") % 3; snprintf(k, sizeof k, "SOULORBS_%d", vf); ADD(k, 0); }
    ADD("GEO", P.geo);
#undef ADD
    return n;
}

static bool at_bench(void) { return P.sitting || pd_bool("atBench"); }
static void sfx(const char *n) { sfx_play(n, 1.0f); }

static int equipped_list(int *out) {
    // GameManager.BuildCharmList: equippedCharms in equip order isn't kept, so list in slot order like a rebuilt list
    int n = 0;
    for (int s = 0; s < 40; s++) if (charm_on(SLOT_CHARM[s])) out[n++] = SLOT_CHARM[s];
    return n;
}

void inv_toggle(void) {
    if (getenv("HK_INVDBG")) fprintf(stderr, "inv_toggle -> %d\n", !V.open);
    V.open = !V.open;
    if (V.open) { V.attempts = 0; V.crack = 0; if (V.sel >= 100) V.sel = 0; if (V.pane == 2) { if (journal_available()) journal_open(); else V.pane = 0; } sfx("ui_option_click"); }
    else { journal_close(); sfx("ui_change_selection"); uo_broadcast("CHARM INDICATOR CHECK"); }
}

static void apply_heart(int id, bool on) {   // charm changes take effect at once (HeroController.CharmUpdate at the bench)
    (void)id; (void)on;
    game_charm_update();
    if (at_bench()) game_max_health();
}

static void try_equip(int id) {
    if (!charm_owned(id)) return;
    if (!at_bench()) { sfx("sword_hit_reject"); V.shake = 0.25f; return; }
    if (charm_on(id)) { charm_toggle(id); apply_heart(id, false); sfx("charm_click_in"); return; }
    if (charm_slots_filled() >= charm_slots()) { sfx("sword_hit_reject"); V.shake = 0.25f; return; }   // no open slot
    bool over = charm_slots_filled() + charm_cost(id) > charm_slots();
    if (over && !pd_bool("canOvercharm")) {
        V.attempts++;
        if (V.attempts <= 2) { sfx("sword_hit_reject"); V.shake = 0.25f; return; }
        if (V.attempts == 3) { sfx("sword_hit_window_1"); V.crack = 1; V.shake = 0.35f; return; }
        if (V.attempts == 4) { sfx("sword_hit_window_1"); sfx("dream_damage"); V.crack = 2; V.shake = 0.45f; return; }
        sfx("mage_lord_glass_floor_break"); V.crack = 0; V.flash = 0.5f;
        pd_set_bool("canOvercharm", true);
    }
    charm_toggle(id);
    apply_heart(id, true);
    sfx(over ? "dream_damage" : "charm_click_in");
}

void inv_update(float dt) {
    if (V.shake > 0) V.shake -= dt;
    if (V.flash > 0) V.flash -= dt;
    if (!V.open) return;
    if (gin.quickMapP || gin.dashP) {      // L / R: previous / next pane (the Journal once the Hunter has given it)
        int np = journal_available() ? 3 : 2, was = V.pane;
        V.pane = (V.pane + (gin.dashP ? 1 : np - 1)) % np;
        if (was == 2) journal_close();
        if (V.pane == 2) journal_open();
        sfx("ui_change_selection"); return;
    }
    if (V.pane == 2) {
        journal_update(dt);
        if (gin.cancelP || gin.inventoryP) inv_toggle();
        return;
    }
    if (V.pane == 0) {
        Item it[48]; int n = build_items(it);
        if (gin.upP && V.isel > 0) { V.isel--; sfx("ui_change_selection"); }
        if (gin.downP && V.isel < n - 1) { V.isel++; sfx("ui_change_selection"); }
        if (V.isel >= n) V.isel = n - 1;
        if (gin.cancelP || gin.inventoryP) inv_toggle();
        return;
    }
    int eq[40]; int ne = equipped_list(eq);
    if (V.sel >= 100) {     // the equipped row
        int i = V.sel - 100;
        if (ne == 0) { V.sel = 0; return; }
        if (i >= ne) i = ne - 1;
        if (gin.leftP && i > 0) { i--; sfx("ui_change_selection"); }
        if (gin.rightP && i < ne - 1) { i++; sfx("ui_change_selection"); }
        V.sel = 100 + i;
        if (gin.downP) { V.sel = 0; sfx("ui_change_selection"); }
        else if (gin.submitP) try_equip(eq[i]);
    } else {
        int r = V.sel / 10, c = V.sel % 10;
        if (gin.leftP && c > 0) { c--; sfx("ui_change_selection"); }
        if (gin.rightP && c < 9) { c++; sfx("ui_change_selection"); }
        if (gin.downP && r < 3) { r++; sfx("ui_change_selection"); }
        if (gin.upP) { if (r > 0) r--; else if (ne > 0) { V.sel = 100; sfx("ui_change_selection"); goto done; } sfx("ui_change_selection"); }
        V.sel = r * 10 + c;
        if (gin.submitP) try_equip(SLOT_CHARM[V.sel]);
    }
done:
    if (gin.cancelP || gin.inventoryP) inv_toggle();
}

static int selected_charm(void) {
    if (V.sel >= 100) { int eq[40]; int ne = equipped_list(eq); int i = V.sel - 100; return i < ne ? eq[i] : 0; }
    return SLOT_CHARM[V.sel];
}

static void icon(const char *name, float x, float y, float s, float a) {
    const SpriteSet *ic = game_icons(); if (!ic) return;
    int f = spr_named(ic, name); if (f >= 0) spr_draw(ic, f, x, y, s, s, a);
}
static void charm_icon(int id, float x, float y, float s, float a) {
    char n[32];
    if (id == 23 && pd_bool("fragileHealth_unbreakable")) strcpy(n, "charm_unbreakableHeart");
    else if (id == 24 && pd_bool("fragileGreed_unbreakable")) strcpy(n, "charm_unbreakableGreed");
    else if (id == 25 && pd_bool("fragileStrength_unbreakable")) strcpy(n, "charm_unbreakableStrength");
    else snprintf(n, sizeof n, "charm_%d", id);
    icon(n, x, y, s, a);
}

// word-wrapped text; returns the y after the last line
static float wrap_text(const Font *f, const char *t, float x, float y, float w, float sc, u32 col) {
    char line[256]; int ll = 0; float lw = 0, sp = font_width(f, " ") * sc;
    const char *p = t;
    while (*p) {
        if (p[0] == '\\' && p[1] == 'n') { line[ll] = 0; font_draw(f, line, x, y, sc, col); y += f->lineh * sc; ll = 0; lw = 0; p += 2; continue; }
        if (*p == '\n') { line[ll] = 0; font_draw(f, line, x, y, sc, col); y += f->lineh * sc; ll = 0; lw = 0; p++; continue; }
        const char *ws = p; while (*p && *p != ' ' && *p != '\n' && !(p[0] == '\\' && p[1] == 'n')) p++;
        char word[128]; int wl = (int)(p - ws); if (wl > 127) wl = 127; memcpy(word, ws, wl); word[wl] = 0;
        float ww = font_width(f, word) * sc;
        if (ll > 0 && lw + sp + ww > w) { line[ll] = 0; font_draw(f, line, x, y, sc, col); y += f->lineh * sc; ll = 0; lw = 0; }
        if (ll > 0) { line[ll++] = ' '; lw += sp; }
        if (ll + wl < 255) { memcpy(line + ll, word, wl); ll += wl; lw += ww; }
        while (*p == ' ') p++;
    }
    if (ll > 0) { line[ll] = 0; font_draw(f, line, x, y, sc, col); y += f->lineh * sc; }
    return y;
}

// top screen: name + description of the selected charm (the pane's right-hand text)
void inv_draw_top(void) {
    if (!V.open) return;
    C2D_DrawRectSolid(0, 0, 0.92f, TOP_W, TOP_H, C2D_Color32(0, 0, 0, 200));
    if (V.pane == 2) { journal_draw_top(); return; }
    if (V.pane == 0) {
        font_draw_centered(&A.fontMenu, lang_get_or_key("PANE_INVENTORY", "UI"), TOP_W / 2, 10, 1, C2D_Color32(230, 230, 230, 255));
        Item it[48]; int n = build_items(it); if (!n) return;
        int i = V.isel < n ? V.isel : n - 1; char k[48];
        snprintf(k, sizeof k, "INV_NAME_%s", it[i].key);
        { const char *nm = lang_get_or_key(k, "UI"); float sc = font_fit(&A.fontBig, nm, TOP_W - 16, 1);
          font_draw_centered(&A.fontBig, nm, TOP_W / 2, 44 + (1 - sc) * A.fontBig.lineh / 2, sc, C2D_Color32(255, 255, 255, 255)); }
        snprintf(k, sizeof k, "INV_DESC_%s", it[i].key);
        const char *d = lang_get(k, "UI");
        if (d) wrap_text(&A.fontText, d, 40, 92, TOP_W - 80, 1, C2D_Color32(220, 220, 220, 255));
        if (g_hints) font_draw_centered(&A.fontText, TR(journal_available() ? "L / R: Charms, Journal" : "L / R: Charms"), TOP_W / 2, 218, 1, C2D_Color32(255, 255, 255, 160));
        return;
    }
    font_draw_centered(&A.fontMenu, lang_get_or_key("PANE_CHARMS", "UI"), TOP_W / 2, 10, 1, C2D_Color32(230, 230, 230, 255));
    int id = selected_charm();
    if (!id || !charm_owned(id)) return;
    char k[40];
    const char *suf = "";
    if (id == 23 && pd_bool("fragileHealth_unbreakable")) suf = "_G";
    if (id == 24 && pd_bool("fragileGreed_unbreakable")) suf = "_G";
    if (id == 25 && pd_bool("fragileStrength_unbreakable")) suf = "_G";
    snprintf(k, sizeof k, "CHARM_NAME_%d%s", id, suf);
    { const char *nm = lang_get_or_key(k, "UI"); float sc = font_fit(&A.fontBig, nm, TOP_W - 16, 1);     // long names (Spanish) shrink to fit
      font_draw_centered(&A.fontBig, nm, TOP_W / 2, 44 + (1 - sc) * A.fontBig.lineh / 2, sc, C2D_Color32(255, 255, 255, 255)); }
    // notch cost
    const char *nc = lang_get_or_key("CHARM_TXT_NOTCHCOST", "UI");
    float cw = font_width(&A.fontText, nc) + 6 + charm_cost(id) * 14;
    float cx = TOP_W / 2 - cw / 2;
    font_draw(&A.fontText, nc, cx, 82, 1, C2D_Color32(200, 200, 200, 255));
    for (int i = 0; i < charm_cost(id); i++) icon("charm_cost_full", cx + font_width(&A.fontText, nc) + 12 + i * 14, 90, 1.4f, 1);
    snprintf(k, sizeof k, "CHARM_DESC_%d%s", id, suf);
    wrap_text(&A.fontText, lang_get_or_key(k, "UI"), 40, 108, TOP_W - 80, 1, C2D_Color32(220, 220, 220, 255));
    if (charm_on(id) && at_bench()) font_draw_centered(&A.fontText, TR("A: Unequip"), TOP_W / 2, 218, 1, C2D_Color32(255, 255, 255, 200));
    else if (at_bench()) font_draw_centered(&A.fontText, TR("A: Equip"), TOP_W / 2, 218, 1, C2D_Color32(255, 255, 255, 200));
}

void inv_draw_bottom(void) {
    if (!V.open) return;
    C2D_DrawRectSolid(0, 0, 0.5f, BOT_W, BOT_H, C2D_Color32(8, 8, 12, 255));
    if (V.pane == 2) { journal_draw_bottom(); return; }
    if (V.pane == 0) {
        Item it[48]; int n = build_items(it);
        int first = V.isel - 6; if (first > n - 12) first = n - 12; if (first < 0) first = 0;
        for (int r = 0; r < 12 && first + r < n; r++) {
            int i = first + r; char k[48]; snprintf(k, sizeof k, "INV_NAME_%s", it[i].key);
            u32 col = i == V.isel ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(170, 170, 180, 255);
            char line[96];
            if (it[i].count > 0) snprintf(line, sizeof line, "%s  x%d", lang_get_or_key(k, "UI"), it[i].count);
            else snprintf(line, sizeof line, "%s", lang_get_or_key(k, "UI"));
            for (char *q = line; *q; q++) if (q[0] == '\\' && q[1] == 'n') { q[0] = ' '; memmove(q + 1, q + 2, strlen(q + 2) + 1); }
            font_draw(&A.fontText, line, 24, 8 + r * 17, 0.95f, col);
            if (i == V.isel) C2D_DrawRectSolid(10, 13 + r * 17, 0.7f, 6, 6, C2D_Color32(255, 255, 255, 230));
        }
        if (g_hints) font_draw(&A.fontText, TR("L/R Charms   B/SELECT Close"), 10, 224, 0.85f, C2D_Color32(140, 140, 150, 255));
        return;
    }
    float jx = V.shake > 0 ? (((int)(V.shake * 60) & 1) ? 2 : -2) : 0;
    // equipped row + notches
    bool oc = charm_overcharmed();
    font_draw(&A.fontText, lang_get_or_key(oc ? "CHARM_TXT_OVERCHARMED" : "CHARM_TXT_EQUIPPED", "UI"), 10, 4, 0.9f,
              oc ? C2D_Color32(255, 120, 120, 255) : C2D_Color32(200, 200, 200, 255));
    int eq[40]; int ne = equipped_list(eq);
    for (int i = 0; i < ne; i++) {
        float x = 22 + i * 26 + jx, y = 34;
        charm_icon(eq[i], x, y, 0.62f, 1);
        if (V.sel == 100 + i) C2D_DrawRectSolid(x - 12, y + 14, 0.7f, 24, 2, C2D_Color32(255, 255, 255, 230));
    }
    int slots = charm_slots(), used = charm_slots_filled();
    font_draw(&A.fontText, lang_get_or_key("CHARM_TXT_NOTCHES", "UI"), 10, 52, 0.9f, C2D_Color32(200, 200, 200, 255));
    int shown = slots > used ? slots : used;
    for (int i = 0; i < shown; i++) {
        float x = 80 + i * 13, y = 60;
        icon(i < used ? "charm_cost_full" : "charm_cost_empty", x, y, 1.4f, 1);
        if (i >= slots && i < used) C2D_DrawRectSolid(x - 4, y - 4, 0.72f, 8, 8, C2D_Color32(200, 60, 60, 200));
    }
    // collection grid
    const float S = 20, OX = BOT_W / 2 + 0.7f * S, OY = 86;
    for (int s = 0; s < 40; s++) {
        int r = s / 10, c = s % 10, id = SLOT_CHARM[s];
        float x = OX + (ROW_X0[r] + c * 1.5f) * S + jx, y = OY + (ROW_Y[0] - ROW_Y[r]) * S;
        bool got = charm_owned(id);
        icon("charm_backboard", x, y, 1.7f, got ? 1 : 0.3f);
        if (got) charm_icon(id, x, y, 0.62f, charm_on(id) ? 0.35f : 1);
        if (V.sel == s) {
            u32 cc = C2D_Color32(255, 255, 255, 230);
            C2D_DrawRectSolid(x - 12, y - 12, 0.7f, 6, 2, cc); C2D_DrawRectSolid(x - 12, y - 12, 0.7f, 2, 6, cc);
            C2D_DrawRectSolid(x + 6, y - 12, 0.7f, 6, 2, cc);  C2D_DrawRectSolid(x + 10, y - 12, 0.7f, 2, 6, cc);
            C2D_DrawRectSolid(x - 12, y + 10, 0.7f, 6, 2, cc); C2D_DrawRectSolid(x - 12, y + 6, 0.7f, 2, 6, cc);
            C2D_DrawRectSolid(x + 6, y + 10, 0.7f, 6, 2, cc);  C2D_DrawRectSolid(x + 10, y + 6, 0.7f, 2, 6, cc);
        }
    }
    if (V.crack) font_draw_centered(&A.fontText, V.crack == 1 ? "*crack*" : "*CRACK*", BOT_W / 2, 186, 1, C2D_Color32(255, 160, 160, 255));
    if (!at_bench()) wrap_text(&A.fontText, TR("Charms can only be changed while resting at a bench."), 14, 196, BOT_W - 28, 0.85f, C2D_Color32(160, 160, 170, 255));
    if (g_hints) font_draw(&A.fontText, TR("A Equip   L/R Items   B/SELECT Close"), 10, 224, 0.85f, C2D_Color32(140, 140, 150, 255));
    if (V.flash > 0) C2D_DrawRectSolid(0, 0, 0.95f, BOT_W, BOT_H, C2D_Color32f(1, 1, 1, V.flash * 2));
}
