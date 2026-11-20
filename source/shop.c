// Shop windows (the scenes' "Shop Menu": shop_control + ShopMenuStock + ShopItemStats + Confirm Control), native.
// SHOP UP opens the list of the stock the PlayerData allows (bought items, required / removal bools); the cost comes
// from the item's priceConvo in the Prices sheet. Buying takes the geo, sets the item's PlayerData bool and applies
// its special type (mask shard, charm, vessel fragment, notch, keys, eggs, charm repairs, map pins...).
// Closing broadcasts SHOP CLOSED. With nothing left to sell the shopkeeper's "no stock" line is spoken instead.
#include "shop.h"
#include "fsm.h"
#include "game.h"
#include "hero.h"
#include "lang.h"
#include "pd.h"
#include "audio.h"
#include "input.h"
#include "comps.h"
#include "dialogue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int tmpl; JVal *f; int sprite; } Item;
#define MAX_ITEMS 24
static struct {
    bool open, confirm, talking; float a, t, thanks; int sel, top, csel, n;
    UObj *menu; Item items[MAX_ITEMS];
    char msg[64];
} S;

static const char *fs(JVal *f, const char *k) { JVal *v = jget(f, k); return v && v->t == J_STR ? v->s : ""; }
static int fi(JVal *f, const char *k) { return jint(jget(f, k), 0); }
static bool empty(const char *s) { return !s || !s[0] || !strcmp(s, "Null"); }

// the template's ShopItemStats fields and its "Item Sprite"
static JVal *item_stats(int ti, int *sprite) {
    JVal *objs = uo_template_objects(ti); JVal *stats = NULL; *sprite = -1;
    for (JVal *o = objs ? objs->child : NULL; o; o = o->next) {
        const char *n = jstr(jget(o, "n"), "");
        for (JVal *c = jget(o, "c") ? jget(o, "c")->child : NULL; c; c = c->next) {
            const char *k = jstr(jget(c, "k"), "");
            if (!strcmp(k, "mb") && !strcmp(jstr(jget(c, "cls"), ""), "ShopItemStats")) stats = jget(c, "f");
            if (!strcmp(k, "sr") && !strcmp(n, "Item Sprite")) *sprite = uo_sprite_frame(jstr(jget(c, "sp"), NULL));
        }
    }
    return stats;
}
static int cost_of(JVal *f) {
    const char *pc = fs(f, "priceConvo");
    const char *v = !empty(pc) ? lang_get(pc, "Prices") : NULL;
    return v ? atoi(v) : fi(f, "cost");
}
static bool available(JVal *f) {      // ShopMenuStock.BuildItemList
    const char *b = fs(f, "playerDataBoolName"), *req = fs(f, "requiredPlayerDataBool"), *rem = fs(f, "removalPlayerDataBool");
    if (!empty(b) && pd_bool(b)) return false;
    if (!empty(req) && !pd_bool(req)) return false;
    if (!empty(rem) && pd_bool(rem)) return false;
    return true;
}
static JVal *stock_list(UObj *menu) {
    JVal *f = uo_comp_fields(menu, "ShopMenuStock"); if (!f) return NULL;
    const char *alt = fs(f, "altPlayerDataBool"), *alt2 = fs(f, "altPlayerDataBoolAlt");
    if (!empty(alt) && (pd_bool(alt) || (!empty(alt2) && pd_bool(alt2)))) { JVal *s = jget(f, "stockAlt"); if (s && s->n) return s; }
    return jget(f, "stock");
}
static void build(void) {
    S.n = 0;
    JVal *st = stock_list(S.menu);
    for (JVal *e = st ? st->child : NULL; e && S.n < MAX_ITEMS; e = e->next) {
        JVal *pk = jget(e, "prefab"); if (!pk) continue;
        int ti = uo_template_by_key(pk->s); if (ti < 0) continue;
        int sp; JVal *f = item_stats(ti, &sp); if (!f || !available(f)) continue;
        S.items[S.n].tmpl = ti; S.items[S.n].f = f; S.items[S.n].sprite = sp; S.n++;
    }
    if (S.sel >= S.n) S.sel = S.n ? S.n - 1 : 0;
}
static FVar *shopvar(const char *n) { Fsm *f = S.menu ? fsm_find_on(S.menu, "shop_control") : NULL; return f ? fsm_var(f, n) : NULL; }

static void nostock_talk(void) {      // shop_control "Box Up" -> "Choose Convo"
    FVar *ev = shopvar("No Stock Event");
    const char *e = ev ? ev->s : "SLY";
    const char *key = "SLY_NOSTOCK_1", *sheet = "Sly";
    if (!strcmp(e, "SLY")) { key = pd_bool("gaveSlykey") ? "SLY_NOSTOCK_2" : "SLY_NOSTOCK_1"; sheet = "Sly"; }
    else if (!strcmp(e, "ISELDA")) { key = "ISELDA_NOSTOCK"; sheet = "Iselda"; }
    else if (!strcmp(e, "SLUG")) { key = "CHARMSLUG_NOSTOCK"; sheet = "Charm Slug"; }
    else if (!strcmp(e, "LEGEATER")) { key = "LEGEATER_NOSTOCK"; sheet = "Minor NPC"; }
    dlg_box_event("BOX UP");
    dlg_call(0, "StartConversation", key, sheet);
    S.talking = true;
}

static void shop_native(UObj *o, const char *ev) {
    if (!strcmp(ev, "SHOP UP")) {
        memset(&S, 0, sizeof S); S.menu = o;
        build();
        if (S.n == 0) { nostock_talk(); return; }
        S.open = true;
        game_hud_out(true);
#ifdef __HOST__
        fprintf(stderr, "[shop] open: %d items\n", S.n);
#endif
        sfx_play("ui_button_confirm", 0.8f);
    } else if (!strcmp(ev, "CONVO_FINISH") && S.talking) {
        S.talking = false; dlg_box_event("BOX DOWN");
        uo_broadcast("SHOP TALK END"); uo_broadcast("NPC TITLE DOWN"); uo_broadcast("SHOP CLOSED");
    } else if (!strncmp(ev, "CLOSE SHOP WINDOW", 17) && S.open) {
        S.open = false; game_hud_out(false); uo_broadcast("SHOP CLOSED QUICK");
    }
}

void shop_setup(UObj *menu) {         // uo: a "Shop Menu" with shop_control
    menu->native = shop_native;
    for (UObj *c = menu->child; c; c = c->next) c->activeSelf = false;     // its window art isn't used (drawn natively)
    for (int k = 0; k < menu->nfsm; k++) if (!strcmp(menu->fsm[k]->name, "shop_control")) menu->fsm[k]->enabled = false;
}
void shop_reset(void) { memset(&S, 0, sizeof S); }
bool shop_busy(void) { return S.open || S.a > 0.01f; }

static bool spawn_hud(const char *guid8, const char *fsmName, const char *var, int value) {
    int ti = -1;
    for (int i = 0; i < uo_template_count(); i++) { const char *k = uo_template_key(i); if (k && !strncmp(k, guid8, 8)) ti = i; }
    if (ti < 0) return false;
    UObj *o = uo_spawn(ti, 0, 0, 0);
    if (o && fsmName) { Fsm *f = fsm_find_on(o, fsmName); FVar *v = f ? fsm_var(f, var) : NULL; if (v) v->i = value; }
    return o != NULL;
}

static void buy(Item *it) {
    JVal *f = it->f;
    int cost = cost_of(f);
    const char *b = fs(f, "playerDataBoolName");
    FVar *selling = shopvar("Selling Shop");
    if (selling && selling->b) {          // Relic Seeker Lemm: sell relics
        game_add_geo(cost);
        const char *ri = fs(f, "relicPDInt"); if (!empty(ri)) pd_set_int(ri, pd_int(ri) - 1);
    } else {
        if (!empty(b)) pd_set_bool(b, true);
        game_take_geo(cost);
    }
    switch (fi(f, "specialType")) {
    case 1:     // the Heart Piece prefab animates the mask shard and raises maxHealth itself
            if (!spawn_hud("2b511f8c", NULL, NULL, 0)) { int hp = pd_int("heartPieces") + 1; if (hp >= 4) { hp = 0; P.maxHealth++; P.health = P.maxHealth; } pd_set_int("heartPieces", hp); }
            break;
    case 2: { pd_set_int("charmsOwned", pd_int("charmsOwned") + 1); pd_set_bool("hasCharm", true);
              int id = 0; if (!strncmp(b, "gotCharm_", 9)) id = atoi(b + 9);
              spawn_hud("17764cf4", "Charm Msg", "ID", id); break; }
    case 3: if (spawn_hud("ac4e22e0", NULL, NULL, 0)) break;
            { int v = pd_int("vesselFragments") + 1; if (v >= 3) { v = 0; pd_set_int("MPReserveMax", pd_int("MPReserveMax") + 33); } pd_set_int("vesselFragments", v); break; }
    case 8: pd_set_int("charmSlots", pd_int("charmSlots") + 1); break;
    case 9: pd_set_bool("hasMap", true); break;
    case 10: pd_set_int("simpleKeys", pd_int("simpleKeys") + 1); break;
    case 11: pd_set_int("rancidEggs", pd_int("rancidEggs") + 1); break;
    case 12: pd_set_bool("brokenCharm_23", false); break;
    case 13: pd_set_bool("brokenCharm_24", false); break;
    case 14: pd_set_bool("brokenCharm_25", false); break;
    case 15: uo_broadcast("SALUBRA BLESSING"); break;
    case 16: pd_set_bool("hasPin", true); break;
    case 17: pd_set_bool("hasMarker", true); break;
    case 4: case 5: case 6: case 7: { char e[24]; snprintf(e, sizeof e, "SOLD TRINKET %d", fi(f, "specialType") - 3); uo_broadcast(e); break; }
    }
    S.thanks = 1.5f;
    sfx_play("geo_deplete_count_down", 0.8f);
    build();
}

static bool can_buy(Item *it) {
    FVar *selling = shopvar("Selling Shop");
    if (selling && selling->b) return true;
    int need = fi(it->f, "charmsRequired");
    if (need > 0 && pd_int("charmsOwned") < need) return false;
    return P.geo >= cost_of(it->f);
}

void shop_update(float dt) {
    if (S.open) { S.a += dt / 0.25f; if (S.a > 1) S.a = 1; }
    else if (S.a > 0) { S.a -= dt / 0.25f; if (S.a < 0) S.a = 0; }
    if (S.thanks > 0) S.thanks -= dt;
    if (!S.open) return;
    S.t += dt;
    if (S.t < 0.25f) return;
    if (S.confirm) {
        if (gin.leftP || gin.rightP || gin.upP || gin.downP) { S.csel ^= 1; sfx_play("ui_change_selection", 1); }
        if (gin.submitP || gin.jumpP) {
            Item *it = &S.items[S.sel];
            S.confirm = false;
            if (S.csel == 0) { if (can_buy(it)) buy(it); else sfx_play("ui_option_click", 1); }
            else sfx_play("button", 1);
        } else if (gin.cancelP) { S.confirm = false; sfx_play("button", 1); }
        return;
    }
    if (S.n == 0) { S.open = false; game_hud_out(false); uo_broadcast("SHOP CLOSED"); return; }
    if (gin.upP && S.sel > 0) { S.sel--; sfx_play("ui_change_selection", 1); }
    if (gin.downP && S.sel < S.n - 1) { S.sel++; sfx_play("ui_change_selection", 1); }
    if (gin.submitP) {
        if (can_buy(&S.items[S.sel])) { S.confirm = true; S.csel = 0; sfx_play("ui_button_confirm", 1); }
        else sfx_play("ui_option_click", 1);
    } else if (gin.cancelP) {
        S.open = false; game_hud_out(false); sfx_play("button", 1);
        uo_broadcast("SHOP CLOSED");
    }
}

// word-wrapped text at scale sc; draw=false only measures. Returns the y below the last line.
static float wrap_text_sc(const char *t, float x, float y, float w, u32 col, float sc, bool draw) {
    char line[160] = ""; int ll = 0; const char *p = t;      // (was left uninitialised: stack garbage before the first word)
    float lh = A.fontText.lineh * sc;
    while (*p) {
        const char *ws = p; while (*p && *p != ' ' && *p != '\n') p++;
        char word[96]; int wl = (int)(p - ws); if (wl > 95) wl = 95; memcpy(word, ws, wl); word[wl] = 0;
        char cand[160]; snprintf(cand, sizeof cand, "%s%s%s", line, ll ? " " : "", word);
        if (ll && font_width(&A.fontText, cand) * sc > w) { if (draw) font_draw(&A.fontText, line, x, y, sc, col); y += lh; snprintf(line, sizeof line, "%s", word); ll = wl; }
        else { snprintf(line, sizeof line, "%s", cand); ll = (int)strlen(line); }
        if (*p == '\n') { if (draw) font_draw(&A.fontText, line, x, y, sc, col); y += lh; line[0] = 0; ll = 0; }
        if (*p) p++;
    }
    if (ll) { if (draw) font_draw(&A.fontText, line, x, y, sc, col); y += lh; }
    return y;
}
// the description fits above the geo counter: smaller text when it's long (Spanish runs a good deal longer)
static void wrap_draw(const char *t, float x, float y, float w, u32 col) {
    float sc = 1;
    while (sc > 0.7f && wrap_text_sc(t, x, y, w, col, sc, false) > 214) sc -= 0.05f;
    wrap_text_sc(t, x, y, w, col, sc, true);
}

void shop_draw(void) {
    if (S.a <= 0.01f) return;
    float a = S.a;
    C2D_DrawRectSolid(0, 0, 0.8f, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, 0.7f * a));
    // item list
    int vis = 7; if (S.sel < S.top) S.top = S.sel; if (S.sel >= S.top + vis) S.top = S.sel - vis + 1;
    float lx = 14, ly = 30, lh = 26;
    int ci = spr_anim(&A.hud, "Coin Idle");
    for (int k = S.top; k < S.n && k < S.top + vis; k++) {
        Item *it = &S.items[k]; bool sel = k == S.sel;
        float y = ly + (k - S.top) * lh;
        if (sel) C2D_DrawRectSolid(lx - 4, y - 3, 0.8f, 200, lh - 2, C2D_Color32f(1, 1, 1, 0.12f * a));
        if (it->sprite >= 0 && uo_spriteset) spr_draw(uo_spriteset, it->sprite, lx + 10, y + 10, 0.55f, 0.55f, a);
        char cs[16]; snprintf(cs, sizeof cs, "%d", cost_of(it->f));
        bool ok = can_buy(it);
        u32 col = ok ? C2D_Color32f(1, 1, 1, a) : C2D_Color32f(0.55f, 0.55f, 0.6f, a);
        if (ci >= 0) spr_draw(&A.hud, A.hud.animFrames[A.hud.anims[ci].first], lx + 30, y + 10, 0.75f, 0.75f, a);
        font_draw(&A.fontText, cs, lx + 40, y + 2, 1, col);
        font_draw(&A.fontText, lang_get_or_key(fs(it->f, "nameConvo"), "UI"), lx + 82, y + 2, 1, col);
    }
    if (S.top > 0) font_draw_centered(&A.fontText, "^", lx + 96, 14, 1, C2D_Color32f(1, 1, 1, a));
    if (S.top + vis < S.n) font_draw_centered(&A.fontText, "v", lx + 96, ly + vis * lh, 1, C2D_Color32f(1, 1, 1, a));
    // details
    if (S.n > 0) {
        Item *it = &S.items[S.sel];
        float dx = 226;
        if (it->sprite >= 0 && uo_spriteset) spr_draw(uo_spriteset, it->sprite, dx + 80, 52, 1.1f, 1.1f, a);
        {   // the name over the description: on two lines when it's wider than the panel (Spanish names run long)
            const char *nm = lang_get_or_key(fs(it->f, "nameConvo"), "UI"); u32 col = C2D_Color32f(1, 1, 1, a);
            if (font_width(&A.fontMenu, nm) <= 166) font_draw_centered(&A.fontMenu, nm, dx + 80, 92, 1, col);
            else {
                char l1[64]; snprintf(l1, sizeof l1, "%s", nm); int n = (int)strlen(l1), cut = -1;
                for (int i = 0; i < n; i++) if (l1[i] == ' ' && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) cut = i;
                if (cut < 0) font_draw_centered(&A.fontMenu, nm, dx + 80, 92, font_fit(&A.fontMenu, nm, 166, 1), col);
                else {
                    l1[cut] = 0; const char *l2 = l1 + cut + 1;
                    font_draw_centered(&A.fontMenu, l1, dx + 80, 80, font_fit(&A.fontMenu, l1, 166, 1), col);
                    font_draw_centered(&A.fontMenu, l2, dx + 80, 96, font_fit(&A.fontMenu, l2, 166, 1), col);
                }
            }
        }
        if (!S.confirm) wrap_draw(lang_get_or_key(fs(it->f, "descConvo"), "UI"), dx, 114, 166, C2D_Color32f(0.9f, 0.9f, 0.9f, a));
        else {  // the purchase prompt takes the description's place under the name (it was a box laid over both panes)
            const char *q = lang_get_or_key("SHOP_PURCHASE_CONFIRM", "UI");
            font_draw_centered(&A.fontText, q, dx + 80, 128, font_fit(&A.fontText, q, 166, 1), C2D_Color32f(1, 1, 1, a));
            font_draw_centered(&A.fontMenu, lang_get_or_key("YES", "Prompts"), dx + 45, 156, 1, S.csel == 0 ? C2D_Color32f(1, 1, 1, a) : C2D_Color32f(0.55f, 0.55f, 0.55f, a));
            font_draw_centered(&A.fontMenu, lang_get_or_key("NO", "Prompts"), dx + 115, 156, 1, S.csel == 1 ? C2D_Color32f(1, 1, 1, a) : C2D_Color32f(0.55f, 0.55f, 0.55f, a));
        }
    }
    // geo
    char g[24]; snprintf(g, sizeof g, "%d", P.geo);
    if (ci >= 0) spr_draw(&A.hud, A.hud.animFrames[A.hud.anims[ci].first], 290, 226, 0.8f, 0.8f, a);
    font_draw(&A.fontText, g, 302, 218, 1, C2D_Color32f(1, 1, 1, a));
    font_draw(&A.fontText, TR("A Buy   B Leave"), 14, 218, 0.9f, C2D_Color32f(0.7f, 0.7f, 0.75f, a));
    if (S.thanks > 0) font_draw_centered(&A.fontMenu, lang_get_or_key("SHOP_THANKS", "UI"), 300, 196, 1, C2D_Color32f(1, 1, 1, S.thanks > 1 ? 1 : S.thanks));
}
