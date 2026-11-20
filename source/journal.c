// The Hunter's Journal: the Inventory's Journal pane (_GameCameras/HudCamera/Inventory/Journal; JournalList,
// JournalEntryStats). An entry is listed once PlayerData "killed<name>" is set (or "fillJournal"); "kills<name>"
// counts down to the Hunter's notes; "newData<name>" puts the new dot on it until it's looked at.
// Bottom screen: the list (icon + name). Top screen: the selected entry's portrait, name, description, and the notes
// or how many more to defeat. Data: romfs:/text/journal_list.txt, gfx/journal_icons.spr, gfx/journal/<convo>.spr
// (tools/build_journal.py).
#include "journal.h"
#include "game.h"
#include "gfx.h"
#include "pd.h"
#include "lang.h"
#include "input.h"
#include "audio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct { char pd[40], convo[40]; bool warrior, grimm; } JEntry;
static JEntry *ents; static int nents; static bool loaded;
static int vis[256], nvis;                 // indices of the listed entries
static int sel, top;                       // selected row, first row shown
static SpriteSet icons, portrait; static char portraitOf[40];
static float holdT;

static void load(void) {
    if (loaded) return;
    loaded = true;
    FILE *f = fopen("romfs:/text/journal_list.txt", "rb"); if (!f) return;
    char ln[160]; int cap = 0;
    while (fgets(ln, sizeof ln, f)) {
        char a[40] = "", b[40] = "", c[8] = "";
        if (sscanf(ln, "%39[^\t]\t%39[^\t]\t%7s", a, b, c) < 2) continue;
        if (nents >= cap) { cap = cap ? cap * 2 : 192; ents = realloc(ents, cap * sizeof *ents); }
        JEntry *e = &ents[nents++]; memset(e, 0, sizeof *e);
        snprintf(e->pd, sizeof e->pd, "%s", a); snprintf(e->convo, sizeof e->convo, "%s", b);
        e->warrior = strchr(c, 'w') != NULL; e->grimm = strchr(c, 'g') != NULL;
    }
    fclose(f);
}

static bool listed(const JEntry *e) { char k[64]; snprintf(k, sizeof k, "killed%s", e->pd); return pd_bool(k) || pd_bool("fillJournal"); }
static int kills_left(const JEntry *e) { char k[64]; snprintf(k, sizeof k, "kills%s", e->pd); return pd_int(k); }
static bool is_new(const JEntry *e) { char k[64]; snprintf(k, sizeof k, "newData%s", e->pd); return pd_bool(k); }

bool journal_available(void) { return pd_bool("hasJournal"); }

// JournalList.UpdateEnemyList: the listed entries, and the first new one (or the last new data field) selected
void journal_open(void) {
    load();
    if (!icons.loaded) spr_load(&icons, "romfs:/gfx/journal_icons.spr");
    nvis = 0; int firstNew = -1;
    for (int i = 0; i < nents && nvis < 256; i++) if (listed(&ents[i])) {
        if (firstNew < 0 && is_new(&ents[i])) firstNew = nvis;
        vis[nvis++] = i;
    }
#ifdef __HOST__
    if (getenv("HK_JOURNALTRACE")) fprintf(stderr, "[journal] %d of %d entries listed, first new %d\n", nvis, nents, firstNew);
#endif
    if (firstNew >= 0) sel = firstNew;
    if (sel >= nvis) sel = nvis - 1;
    if (sel < 0) sel = 0;
}

void journal_close(void) {
    if (portrait.loaded) spr_free(&portrait);
    portraitOf[0] = 0;
    if (icons.loaded) spr_free(&icons);       // GPU memory goes back to the rooms while the pane is shut
}

void journal_update(float dt) {
    if (!nvis) return;
    int d = 0;
    if (gin.upP) d = -1; else if (gin.downP) d = 1;
    else if (gin.upH || gin.downH) { holdT += dt; if (holdT > 0.35f) { holdT -= 0.08f; d = gin.upH ? -1 : 1; } }
    if (!gin.upH && !gin.downH) holdT = 0;
    if (d) {
        int n = sel + d; if (n < 0) n = 0; if (n > nvis - 1) n = nvis - 1;
        if (n != sel) { sel = n; sfx_play("ui_change_selection", 1.0f); }
    }
    // JournalEntryStats.Update: an entry in the centre loses its new dot
    const JEntry *e = &ents[vis[sel]];
    if (is_new(e)) { char k[64]; snprintf(k, sizeof k, "newData%s", e->pd); pd_set_bool(k, false); }
}

static float wrap(const Font *f, const char *t, float x, float y, float w, float sc, u32 col, bool draw) {
    char line[320]; int ll = 0; float lw = 0, sp = font_width(f, " ") * sc;
    const char *p = t;
    while (*p) {
        bool nl = (p[0] == '\\' && p[1] == 'n') || *p == '\n';
        if (nl) { line[ll] = 0; if (draw) font_draw(f, line, x, y, sc, col); y += f->lineh * sc; ll = 0; lw = 0; p += *p == '\n' ? 1 : 2; continue; }
        const char *ws = p; while (*p && *p != ' ' && *p != '\n' && !(p[0] == '\\' && p[1] == 'n')) p++;
        char word[128]; int wl = (int)(p - ws); if (wl > 127) wl = 127; memcpy(word, ws, wl); word[wl] = 0;
        float ww = font_width(f, word) * sc;
        if (ll > 0 && lw + sp + ww > w) { line[ll] = 0; if (draw) font_draw(f, line, x, y, sc, col); y += f->lineh * sc; ll = 0; lw = 0; }
        if (ll > 0 && ll < 318) { line[ll++] = ' '; lw += sp; }
        if (ll + wl < 318) { memcpy(line + ll, word, wl); ll += wl; lw += ww; }
        while (*p == ' ') p++;
    }
    if (ll > 0) { line[ll] = 0; if (draw) font_draw(f, line, x, y, sc, col); y += f->lineh * sc; }
    return y;
}

static const char *jtext(const char *pre, const JEntry *e) {
    static char k[3][64]; static int r; r = (r + 1) % 3;
    snprintf(k[r], sizeof k[r], "%s%s", pre, e->convo);
    const char *t = lang_get(k[r], "Journal");
    return t ? t : "";
}

void journal_draw_top(void) {
    font_draw_centered(&A.fontMenu, lang_get_or_key("PANE_JOURNAL", "UI"), TOP_W / 2, 6, 1, C2D_Color32(230, 230, 230, 255));
    if (!nvis) {
        font_draw_centered(&A.fontText, lang_get_or_key("HUNTER_1_TEXT", "UI"), TOP_W / 2, 110, 1, C2D_Color32(170, 170, 180, 255));
        return;
    }
    const JEntry *e = &ents[vis[sel]];
    // the portrait, loaded for the entry on show
    if (strcmp(portraitOf, e->convo)) {
        if (portrait.loaded) spr_free(&portrait);
        char p[96]; snprintf(p, sizeof p, "romfs:/gfx/journal/%s.spr", e->convo);
        spr_load(&portrait, p);
        snprintf(portraitOf, sizeof portraitOf, "%s", e->convo);
    }
    const float PX = 78, TX = 150, TW = TOP_W - TX - 10;
    if (portrait.loaded) { int f = spr_named(&portrait, "portrait"); if (f >= 0) spr_draw(&portrait, f, PX, 126, 1, 1, 1); }
    const char *nm = jtext("NAME_", e);
    float sc = font_fit(&A.fontBig, nm, TW, 0.8f);
    font_draw(&A.fontBig, nm, TX, 30, sc, C2D_Color32(255, 255, 255, 255));
    // description, then the Hunter's notes or the kills still needed; shrunk if they'd run off the screen
    const char *desc = jtext("DESC_", e);
    int left = kills_left(e);
    char need[160] = "";
    if (left > 0 && !e->warrior)
        snprintf(need, sizeof need, "%s %d %s", lang_get_or_key("KILL_COUNT_1", "Journal"), left, lang_get_or_key("KILL_COUNT_2", "Journal"));
    const char *notes = left > 0 && !e->warrior ? need : jtext("NOTE_", e);
    float y0 = 30 + A.fontBig.lineh * sc + 4, ts = 0.85f;
    for (int i = 0; i < 4; i++) {
        float h = wrap(&A.fontText, desc, TX, y0, TW, ts, 0, false) + 6;
        h = wrap(&A.fontText, notes, TX, h, TW, ts, 0, false);
        if (h <= TOP_H - 6) break;
        ts -= 0.08f;
    }
    float y = wrap(&A.fontText, desc, TX, y0, TW, ts, C2D_Color32(225, 225, 225, 255), true) + 6;
    C2D_DrawRectSolid(TX, y - 4, 0.95f, TW, 1, C2D_Color32(255, 255, 255, 70));
    wrap(&A.fontText, notes, TX, y, TW, ts, left > 0 && !e->warrior ? C2D_Color32(170, 170, 185, 255) : C2D_Color32(205, 200, 180, 255), true);
}

void journal_draw_bottom(void) {
    // encountered / completed (PlayerData.CountJournalEntries)
    int enc = 0, done = 0;
    for (int i = 0; i < nvis; i++) { enc++; if (kills_left(&ents[vis[i]]) <= 0) done++; }
    char hdr[96];
    snprintf(hdr, sizeof hdr, "%s %d     %s %d", lang_get_or_key("ENCOUNTERED", "UI"), enc, lang_get_or_key("COMPLETION", "UI"), done);
    font_draw(&A.fontText, hdr, 10, 4, 0.85f, C2D_Color32(190, 190, 200, 255));
    if (!nvis) return;
    const int ROWS = 6; const float RH = 32, Y0 = 26;
    if (sel < top) top = sel;
    if (sel > top + ROWS - 1) top = sel - ROWS + 1;
    if (top > nvis - ROWS) top = nvis - ROWS;
    if (top < 0) top = 0;
    for (int r = 0; r < ROWS && top + r < nvis; r++) {
        const JEntry *e = &ents[vis[top + r]];
        float y = Y0 + r * RH, cy = y + RH / 2;
        bool on = top + r == sel;
        if (on) C2D_DrawRectSolid(4, y + 1, 0.6f, BOT_W - 8, RH - 2, C2D_Color32(255, 255, 255, 28));
        if (icons.loaded) { int f = spr_named(&icons, e->convo); if (f >= 0) spr_draw(&icons, f, 30, cy, 1, 1, kills_left(e) > 0 && !e->warrior ? 0.75f : 1); }
        const char *nm = jtext("NAME_", e);
        float sc = font_fit(&A.fontText, nm, BOT_W - 80, 1);
        font_draw(&A.fontText, nm, 56, cy - A.fontText.lineh * sc / 2, sc, on ? C2D_Color32(255, 255, 255, 255) : C2D_Color32(170, 170, 180, 255));
        if (is_new(e)) C2D_DrawRectSolid(47, cy - 3, 0.7f, 5, 5, C2D_Color32(255, 230, 140, 255));   // the new dot
    }
    // scroll position
    if (nvis > ROWS) {
        float h = ROWS * RH, bh = h * ROWS / nvis, by = Y0 + (h - bh) * top / (float)(nvis - ROWS);
        C2D_DrawRectSolid(BOT_W - 4, Y0, 0.6f, 2, h, C2D_Color32(255, 255, 255, 40));
        C2D_DrawRectSolid(BOT_W - 4, by, 0.65f, 2, bh, C2D_Color32(255, 255, 255, 160));
    }
    if (g_hints) font_draw(&A.fontText, TR("L/R Inventory   B/SELECT Close"), 10, 224, 0.85f, C2D_Color32(140, 140, 150, 255));
}
