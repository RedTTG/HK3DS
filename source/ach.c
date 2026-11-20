// Achievements: the game's own list (romfs/text/Achievements.txt), unlocked by the scripts' AwardAchievement calls and by
// GameManager.CheckAllAchievements-style checks on PlayerData. Kept across save files in sdmc:/3ds/hk3ds/achievements.txt.
#include "ach.h"
#include "pd.h"
#include "gfx.h"
#include "game.h"
#include "lang.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAXA 80
typedef struct { char key[32], title[64], text[160]; bool got; } Ach;
static Ach ach[MAXA]; static int nach; static bool loaded;
static char popTitle[64]; static float popT;

// the game's order on its achievements page
static const char *ORDER[] = { "PROTECTED", "MASKED", "SOULFUL", "WORLDSOUL", "FALSEHOOD", "FK_DEFEAT", "TEACHER", "WATCHER", "BEAST",
    "HORNET_1", "HORNET_2", "MANTIS_LORDS", "SOUL_MASTER_DEFEAT", "DUNG_DEFENDER", "BROKEN_VESSEL", "TRAITOR_LORD", "COLLECTOR",
    "CHARMED", "ENCHANTED", "BLESSED", "GRUBFRIEND", "METAMORPHOSIS", "STAG_STATION_HALF", "STAG_STATION_ALL", "MAP",
    "HUNTER_1", "HUNTER_2", "ATTUNEMENT", "AWAKENING", "ASCENSION", "DREAM_FK", "DREAM_BROKEN_VESSEL", "DREAM_SOUL_MASTER_DEFEAT",
    "GREY_PRINCE", "WHITE_DEFENDER", "GRIMM", "NIGHTMARE_GRIMM", "BANISHMENT", "NEGLECT", "MOURNER", "QUIRREL_EPILOGUE",
    "NAILSMITH_KILL", "NAILSMITH_SPARE", "ZOTE", "MR_MUSHROOM", "COLOSSEUM_1", "COLOSSEUM_2", "COLOSSEUM_3", "VOID",
    "PANTHEON1", "PANTHEON2", "PANTHEON3", "PANTHEON4", "ENDINGD", "ENDING_A", "ENDING_B", "ENDING_C", "COMPLETION",
    "SPEEDRUN_1", "SPEEDRUN_2", "SPEED_COMPLETION", "STEELSOUL", "STEELSOUL_COMPLETION", NULL };

static void lookup(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return;
    char ln[256];
    while (fgets(ln, sizeof ln, f)) {
        ln[strcspn(ln, "\r\n")] = 0;
        char *tab = strchr(ln, '\t'); if (!tab) continue; *tab = 0;
        size_t n = strlen(ln); bool title = n > 6 && !strcmp(ln + n - 6, "_TITLE"), text = n > 5 && !strcmp(ln + n - 5, "_TEXT");
        if (!title && !text) continue;
        ln[n - (title ? 6 : 5)] = 0;
        for (int i = 0; i < nach; i++) if (!strcmp(ach[i].key, ln)) {
            if (title) snprintf(ach[i].title, sizeof ach[i].title, "%s", tab + 1); else snprintf(ach[i].text, sizeof ach[i].text, "%s", tab + 1);
        }
    }
    fclose(f);
}

void ach_reload(void) { loaded = false; ach_load(); }     // the titles in the language just chosen
void ach_load(void) {
    if (loaded) return;
    loaded = true; nach = 0;
    for (int i = 0; ORDER[i] && nach < MAXA; i++) { memset(&ach[nach], 0, sizeof ach[nach]); snprintf(ach[nach].key, 32, "%s", ORDER[i]); nach++; }
    { char path[96]; lang_path("Achievements", path, sizeof path); lookup(path); }
    int k = 0;      // keep only the ones the text file names (FALSEHOOD has its title in the game's achievement data instead)
    for (int i = 0; i < nach; i++) if (ach[i].title[0] || !strcmp(ach[i].key, "FALSEHOOD")) { if (!ach[i].title[0]) { snprintf(ach[i].title, sizeof ach[i].title, "%s", TR("Falsehood")); snprintf(ach[i].text, sizeof ach[i].text, "%s", TR("Defeat the False Knight")); } ach[k++] = ach[i]; }
    nach = k;
    FILE *f = fopen("sdmc:/3ds/hk3ds/achievements.txt", "rb");
    if (f) { char ln[64]; while (fgets(ln, sizeof ln, f)) { ln[strcspn(ln, "\r\n")] = 0; for (int i = 0; i < nach; i++) if (!strcmp(ach[i].key, ln)) ach[i].got = true; } fclose(f); }
}

static void save(void) {
    mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/hk3ds", 0777);
    FILE *f = fopen("sdmc:/3ds/hk3ds/achievements.txt", "wb"); if (!f) return;
    for (int i = 0; i < nach; i++) if (ach[i].got) fprintf(f, "%s\n", ach[i].key);
    fclose(f);
}

void ach_award(const char *key) {
    if (!key || !key[0]) return;
    ach_load();
    if (!strcmp(key, "FK_DEFEAT")) ach_award("FALSEHOOD");
    for (int i = 0; i < nach; i++) if (!strcmp(ach[i].key, key)) {
        if (ach[i].got) return;
        ach[i].got = true; save();
        snprintf(popTitle, sizeof popTitle, "%s", ach[i].title); popT = 4.0f;
        hk_log("achievement %s", key);
        return;
    }
}

// GameManager.CheckAllAchievements: the collection achievements
void ach_check_all(void) {
    if (pd_int("charmsOwned") >= 1) ach_award("CHARMED");
    if (pd_int("charmsOwned") >= 20) ach_award("ENCHANTED");
    if (pd_int("maxHealthBase") >= 6) ach_award("PROTECTED");
    if (pd_int("maxHealthBase") >= 9) ach_award("MASKED");
    if (pd_int("MPReserveMax") >= 33) ach_award("SOULFUL");
    if (pd_int("MPReserveMax") >= 99) ach_award("WORLDSOUL");
    if (pd_int("grubsCollected") >= 23) ach_award("GRUBFRIEND");
    if (pd_int("grubsCollected") >= 46) ach_award("METAMORPHOSIS");
    if (pd_int("dreamOrbs") >= 600) ach_award("ATTUNEMENT");
    if (pd_int("stationsOpened") >= 5) ach_award("STAG_STATION_HALF");
    if (pd_bool("falseKnightDefeated")) ach_award("FALSEHOOD");
    if (pd_bool("completedEndingA")) ach_award("ENDING_A");
    if (pd_bool("completedEndingB")) ach_award("ENDING_B");
    if (pd_bool("completedEndingC")) ach_award("ENDING_C");
    if (pd_bool("completedEndingD")) ach_award("ENDINGD");
}

int ach_count(void) { ach_load(); return nach; }
int ach_unlocked(void) { ach_load(); int n = 0; for (int i = 0; i < nach; i++) n += ach[i].got; return n; }
bool ach_get(int i, const char **title, const char **text) {
    ach_load(); if (i < 0 || i >= nach) return false;
    *title = ach[i].title; *text = ach[i].text; return ach[i].got;
}

void ach_update(float dt) { if (popT > 0) popT -= dt; }
// the unlock notice in the corner of the top screen
void ach_draw_popup(void) {
    if (popT <= 0) return;
    float a = popT > 3.5f ? (4.0f - popT) * 2 : popT < 0.5f ? popT * 2 : 1;
    int al = (int)(a * 255);
    float w = 220, x = TOP_W - w - 8, y = TOP_H - 46;
    C2D_DrawRectSolid(x, y, 0.95f, w, 38, C2D_Color32(0, 0, 0, (int)(a * 200)));
    font_draw(&A.fontText, TR("Achievement unlocked"), x + 8, y + 3, 0.85f, C2D_Color32(180, 180, 180, al));
    font_draw(&A.fontText, popTitle, x + 8, y + 19, 1, C2D_Color32(255, 255, 255, al));
}
