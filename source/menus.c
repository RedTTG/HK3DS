// Full-screen menus the FSMs open through the MenuHolder global: the Stagway map ("Open Stag" -> Stag Map.prefab's
// Control / UI List Stag). The list shows the stations whose PlayerData "opened..." bool is set; the choice goes back
// to the requester's "Stag Control" FSM as "Selection Result" followed by CONTINUE.
#include "menus.h"
#include "fsm.h"
#include "game.h"
#include "hero.h"
#include "lang.h"
#include "pd.h"
#include "audio.h"
#include "input.h"
#include <stdio.h>
#include <string.h>

static SpriteSet SM;     // stagmenu.spr: map pieces named after the stations, "backing", "glow", "Divider"

typedef struct { const char *sel, *pd, *key, *piece; int pos; } Station;
// UI List Stag order; pos = PlayerData stagPosition of the station
static const Station ST[] = {
    { "Dirtmouth", "openedTown", "DIRTMOUTH", "Dirtmouth", 0 },
    { "Crossroads", "openedCrossroads", "CROSSROADS", "Crossroads", 1 },
    { "Greenpath", "openedGreenpath", "GREENPATH", "Greenpath", 2 },
    { "Fungal Wastes", "openedFungalWastes", "FUNGAL_WASTES", "Fungal Wastes", 3 },
    { "Royal Gardens", "openedRoyalGardens", "ROYAL_GARDENS", "Royal Gardens", 4 },
    { "City Storerooms", "openedRuins1", "CITY_OF_TEARS_1", "City Storerooms", 5 },
    { "Kings Station", "openedRuins2", "KINGS_STATION", "Kings Station", 6 },
    { "Resting Grounds", "openedRestingGrounds", "RESTING_GROUNDS", "Resting Grounds", 7 },
    { "Deepnest", "openedDeepnest", "DEEPNEST", "Deepnest", 8 },
    { "Stag Nest", "openedStagNest", "STAG_NEST", "Stag Nest", 9 },
    { "Hidden Station", "openedHiddenStation", "HIDDEN_STATION", "Hidden Station", 10 },
};
#define NST (int)(sizeof ST / sizeof *ST)

static struct { bool open; float a, t; int sel, items[NST], n; UObj *req; bool closing; } M;

void menus_init(void) { if (!SM.loaded) spr_load(&SM, "romfs:/gfx/stagmenu.spr"); }
void menus_reset(void) { memset(&M, 0, sizeof M); }
bool menus_busy(void) { return M.open || M.a > 0.01f; }

static bool station_open(int i) { return ST[i].pos == pd_int("stagPosition") || pd_bool(ST[i].pd); }

void stag_menu_open(UObj *requester) {
    M.open = true; M.closing = false; M.req = requester; M.t = 0; M.n = 0; M.sel = 0;
    int cur = pd_int("stagPosition");
    for (int i = 0; i < NST; i++) if (station_open(i)) { if (ST[i].pos == cur) M.sel = M.n; M.items[M.n++] = i; }
    game_hud_out(true);
}

static void choose(int i) {
    M.open = false; M.closing = true;
    uo_broadcast("STAG MENU DOWN");
    if (M.req) {
        Fsm *f = fsm_find_on(M.req, "Stag Control");
        FVar *v = f ? fsm_var(f, "Selection Result") : NULL;
        if (v) snprintf(v->s, sizeof v->s, "%s", ST[i].sel);
        uo_send_event(M.req, "CONTINUE");
    }
}

void menus_update(float dt) {
    if (M.open) {
        M.a += dt / 0.3f; if (M.a > 1) M.a = 1;
        M.t += dt;
        if (M.n > 0 && M.t > 0.2f) {
            if (gin.upP && M.sel > 0) { M.sel--; sfx_play("ui_change_selection", 1); }
            if (gin.downP && M.sel < M.n - 1) { M.sel++; sfx_play("ui_change_selection", 1); }
            if (gin.submitP || gin.jumpP) { sfx_play("ui_button_confirm", 1); choose(M.items[M.sel]); }
            else if (gin.cancelP || gin.attackP) {     // back out: choosing the current station cancels the trip
                sfx_play("button", 1);
                int cur = pd_int("stagPosition"), ci = 0;
                for (int i = 0; i < NST; i++) if (ST[i].pos == cur) ci = i;
                choose(ci);
            }
        }
    } else if (M.a > 0) { M.a -= dt / 0.3f; if (M.a < 0) M.a = 0; }
}

#define MAP_SCALE 1.55f
static void draw_named(const char *n, float x, float y, float a) {
    int f = spr_named(&SM, n); if (f >= 0) spr_draw(&SM, f, x, y, MAP_SCALE, MAP_SCALE, a);
}
static void draw_centered(const char *n, float cx, float cy, float sx, float sy, float a) {   // ignore the map offset
    int f = spr_named(&SM, n); if (f < 0) return;
    const Frame *fr = &SM.frames[f];
    spr_draw(&SM, f, cx - (fr->ox + fr->w / 2.0f) * sx, cy - (fr->oy + fr->h / 2.0f) * sy, sx, sy, a);
}

void menus_draw(void) {
    if (M.a <= 0.01f) return;
    float a = M.a;
    C2D_DrawRectSolid(0, 0, 0.8f, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, a * 0.75f));
    float mx = 118, my = 124;          // map centre on screen
    draw_named("backing", mx, my, a);
    draw_named("Core", mx, my, a);
    for (int i = 0; i < NST; i++) if (station_open(i)) draw_named(ST[i].piece, mx, my, a);
    // station list (right side)
    float lx = 312, ly = 46, lh = 17;
    font_draw_centered(&A.fontMenu, lang_get_or_key("STATIONS", "StagMenu"), lx, 20, 1, C2D_Color32f(1, 1, 1, a));
    draw_centered("Divider", lx, 37, 3, 1, a);
    for (int k = 0; k < M.n; k++) {
        const Station *s = &ST[M.items[k]];
        bool sel = k == M.sel;
        if (sel) draw_centered("glow", lx, ly + k * lh + 9, 2.4f, 2.2f, a * 0.8f);
        bool here = s->pos == pd_int("stagPosition");
        u32 c = here ? C2D_Color32f(0.6f, 0.6f, 0.65f, a) : C2D_Color32f(1, 1, 1, a * (sel ? 1 : 0.75f));
        font_draw_centered(&A.fontText, lang_get_or_key(s->key, "StagMenu"), lx, ly + k * lh, 1, c);
        if (sel) {
            int pa = spr_anim(&A.ui, "pointer");
            if (pa >= 0) {
                float w = font_width(&A.fontText, lang_get_or_key(s->key, "StagMenu"));
                int fr = A.ui.animFrames[A.ui.anims[pa].first + A.ui.anims[pa].count - 1];
                spr_draw(&A.ui, fr, lx - w / 2 - 4, ly + k * lh + 8, 1, 1, a);
                spr_draw(&A.ui, fr, lx + w / 2 + 4, ly + k * lh + 8, -1, 1, a);
            }
        }
    }
}
