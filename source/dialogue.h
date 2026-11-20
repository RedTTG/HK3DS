// HUD UI the NPC FSMs drive through PlayMaker globals: the DialogueManager (dialogue box + yes/no box, DialogueBox
// typewriter and the "Dialogue Page Control" FSMs), prompt markers (ShowPromptMarker "LISTEN"...) and the NPC name
// title of "Area Title Control".
#pragma once
#include <stdbool.h>
#include "uo.h"

void dlg_init(void);                       // load romfs:/gfx/dialog.spr
void dlg_reset(void);                      // leaving the scene: boxes down, conversation cancelled, prompts gone
void dlg_box_event(const char *ev);        // events sent to DialogueManager: BOX UP / BOX DOWN / BOX UP YN / BOX DOWN YN
void dlg_text_event(int yn, const char *ev);   // events sent to the DialogueText objects (HERO DAMAGED, CONVO CANCEL...)
void dlg_call(int yn, const char *method, const char *arg0, const char *arg1);   // DialogueBox methods
void dlg_set_vars(Fsm *normal, Fsm *yn);   // the stand-in "Dialogue Page Control" FSMs holding Use Stop / Toll Cost / Requester
void dlg_update(float dt);
void dlg_draw_world(float camx, float camy);   // prompt markers (world space)
void dlg_draw_hud(void);                   // boxes and titles
bool dlg_busy(void);                       // a box is up or a conversation is running

UObj *prompt_show(UObj *owner, UObj *existing, const char *label, float wx, float wy);   // returns the marker object
void prompt_hide(UObj *marker);
UObj *prompt_create(float wx, float wy);   // the "Arrow Prompt" prefab (inspect regions): UP / DOWN / CHANGE LABEL

void title_npc(Fsm *areaTitle);            // AreaTitle activated with "NPC Title" set: show the small name title
void title_event(const char *ev);          // NPC TITLE DOWN / NPC CONVO START

// area titles (AreaTitleController + Area Title Control)
void title_area(const char *ev, bool visited, bool right);
bool title_big_showing(void);
void area_titles_enter(const char *room, const char *entryGate);
bool area_titles_has(const char *room);
void area_titles_update(float dt, float hx0, float hy0, float hx1, float hy1);

void dlg_dream_msg(const char *title, int amount);   // Enemy Dream Msg: DISPLAY ENEMY DREAM
