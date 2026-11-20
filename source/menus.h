#pragma once
#include <stdbool.h>
#include "uo.h"

void menus_init(void);
void menus_reset(void);
bool menus_busy(void);
void menus_update(float dt);
void menus_draw(void);
void stag_menu_open(UObj *requester);      // MenuHolder: OPEN STAG MENU
