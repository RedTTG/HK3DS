#pragma once
#include "gfx.h"
#include "room.h"

typedef struct {
    SpriteSet knight, enemies, hud, ui;
    Font fontMenu, fontBig, fontText;
} Assets;
extern Assets A;

bool assets_load(void);
const SpriteSet *game_icons(void);
float game_room_w(void);
float game_room_h(void);     // icons.spr (charms, inventory items)

void game_new(void);
bool game_continue(void);      // load save
bool game_has_save(void);
extern int g_saveSlot;
void game_migrate_saves(void);
void game_set_slot(int slot);
void game_clear_slot(int slot);
bool game_slot_info(int slot, char *area, int an, int *masks, int *geo, int *seconds);
// returns: 0 keep playing, 1 quit to menu
int  game_update(float dt, u32 down, u32 held, u32 up, circlePosition cp);
void game_draw_top(void);
void game_draw_bottom(void);
void game_end(void);
