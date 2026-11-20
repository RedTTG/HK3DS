// The big "item get" message (UI Msg Get Item.prefab, Msg Control FSM) for abilities and spells: prefix + item name and
// icon fade up (2.5s), then the button line and two description lines (3s), then a stop marker until Jump / Attack /
// Cast / Pause is pressed; then everything fades (1s) and GET ITEM MSG END is broadcast.
#include "msgs.h"
#include "fsm.h"
#include "game.h"
#include "hero.h"
#include "lang.h"
#include "audio.h"
#include "input.h"
#include "itemmsgs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { M_NONE, M_WAIT_ITEM, M_TOP, M_BOT, M_STOP, M_DOWN };
static struct {
    int state; float t, topA, botA; UObj *o;
    const ItemMsg *m; int iconFrame, iconSet;
} G;

static void getitem_native(UObj *o, const char *ev) { (void)o; (void)ev; }

UObj *msg_create_getitem(FVar *sprite) {
    UObj *o = uo_alloc_pseudo("UI Msg Get Item"); if (!o) return NULL;
    o->native = getitem_native;
    static const char *n[] = { "Item" }; static const VType t[] = { V_STRING };
    Fsm *f = fsm_create_vars("Msg Control", o, n, t, 1);
    o->fsm = realloc(o->fsm, (o->nfsm + 1) * sizeof(Fsm *)); o->fsm[o->nfsm++] = f;
    memset(&G, 0, sizeof G);
    G.o = o; G.state = M_WAIT_ITEM; G.iconFrame = -1;
    if (sprite) {
        if (sprite->b) { G.iconFrame = sprite->i; G.iconSet = (int)sprite->f; }
        else if (sprite->obj && sprite->obj->t == J_OBJ && jget(sprite->obj, "sprite")) { G.iconFrame = uo_sprite_frame(jget(sprite->obj, "sprite")->s); G.iconSet = 0; }
    }
    game_hud_out(true);
    return o;
}

void msgs_reset(void) { memset(&G, 0, sizeof G); }
// "Hunter's Journal updated" corner notice (JournalUpdateMsg)
static float journalT; static bool journalFull;
void msgs_journal_update(bool full) { journalT = 3.0f; journalFull = full; sfx_play("journal_update", 0.8f); }
bool msgs_busy(void) { return G.state != M_NONE; }

static const char *txt(const char *sheet, const char *key) { return sheet && key ? lang_get_or_key(key, sheet) : ""; }

void msgs_update(float dt) {
    if (journalT > 0) journalT -= dt;
    if (G.state == M_NONE) return;
    G.t += dt;
    switch (G.state) {
    case M_WAIT_ITEM: {     // the FSM sets Msg Control's "Item" right after creating the message
        Fsm *f = G.o && G.o->nfsm ? G.o->fsm[0] : NULL; FVar *it = f ? fsm_var(f, "Item") : NULL;
        if (it && it->s[0]) {
            for (unsigned i = 0; i < sizeof ITEM_MSGS / sizeof *ITEM_MSGS; i++) if (!strcmp(ITEM_MSGS[i].item, it->s)) G.m = &ITEM_MSGS[i];
            G.state = M_TOP; G.t = 0;
            sfx_play("spell_information_screen", 1);
        } else if (G.t > 0.5f) { G.state = M_DOWN; G.t = 0; }
        break; }
    case M_TOP: G.topA = G.t / 0.5f; if (G.topA > 1) G.topA = 1; if (G.t >= 2.5f) { G.state = M_BOT; G.t = 0; } break;
    case M_BOT: G.botA = G.t / 0.5f; if (G.botA > 1) G.botA = 1; if (G.t >= 3.25f) { G.state = M_STOP; G.t = 0; } break;
    case M_STOP:
        if (gin.jumpP || gin.attackP || gin.castP || gin.submitP) { G.state = M_DOWN; G.t = 0; sfx_play("button", 1); }
        break;
    case M_DOWN:
        G.topA -= dt; G.botA -= dt;
        if (G.t >= 1.0f) {
            G.state = M_NONE;
            game_hud_out(false);
            uo_broadcast("GET ITEM MSG END");
            if (G.o) uo_destroy(G.o);
            G.o = NULL;
        }
        break;
    }
}

void msgs_draw(void) {
    if (journalT > 0) {
        float a = journalT > 2.5f ? (3 - journalT) * 2 : journalT < 0.5f ? journalT * 2 : 1;
        const char *t = lang_get(journalFull ? "JOURNAL_ENTRY_COMPLETE" : "JOURNAL_UPDATED", "Journal");
        font_draw(&A.fontText, t ? t : (journalFull ? TR("Journal entry complete") : TR("Hunter's Journal updated")), TOP_W - 180, TOP_H - 26, 0.9f, C2D_Color32f(1, 1, 1, a));
    }
    if (G.state == M_NONE || G.state == M_WAIT_ITEM) return;
    float ta = G.topA < 0 ? 0 : G.topA, ba = G.botA < 0 ? 0 : G.botA;
    C2D_DrawRectSolid(0, 0, 0.85f, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, 0.55f * (ta > ba ? ta : ba)));
    const ItemMsg *m = G.m;
    if (m) {
        font_draw_centered(&A.fontText, txt(m->prefixSheet, m->prefix), TOP_W / 2, 34, 1, C2D_Color32f(1, 1, 1, ta));
        font_draw_centered(&A.fontBig, txt(m->nameSheet, m->name), TOP_W / 2, 50, 1, C2D_Color32f(1, 1, 1, ta));
    }
    if (G.iconFrame >= 0) {
        const SpriteSet *ss = G.iconSet == 1 ? game_icons() : uo_spriteset;
        if (ss) spr_draw(ss, G.iconFrame, TOP_W / 2, 120, 1.6f, 1.6f, ta);
    }
    if (m) {
        char line[200];
        snprintf(line, sizeof line, "%s  [%s]  %s", txt(m->pressSheet, m->press), m->action, txt(m->msg1Sheet, m->msg1));
        font_draw_centered(&A.fontText, line, TOP_W / 2, 168, 1, C2D_Color32f(1, 1, 1, ba));
        font_draw_centered(&A.fontText, txt(m->msg2Sheet, m->msg2), TOP_W / 2, 186, 1, C2D_Color32f(1, 1, 1, ba));
    }
    if (G.state == M_STOP) {
        int fr = spr_anim(&A.ui, "pointer");
        (void)fr;
        font_draw_centered(&A.fontText, "~", TOP_W / 2, 206, 1, C2D_Color32f(1, 1, 1, 0.8f));
    }
}
