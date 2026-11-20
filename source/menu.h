#pragma once
#include <3ds.h>
enum { MENU_NONE, MENU_NEWGAME, MENU_CONTINUE, MENU_QUIT };
void menu_enter(void);
int  menu_update(float dt, u32 down);
void menu_draw_top(void);
void menu_draw_bottom(void);
void menu_draw_pointers(float cx, float y, float halfw, float pt);
