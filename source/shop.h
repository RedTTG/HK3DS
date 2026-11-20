#pragma once
#include <stdbool.h>
#include "uo.h"

void shop_setup(UObj *menu);     // a scene "Shop Menu" (shop_control FSM): handled natively from here on
void shop_reset(void);
bool shop_busy(void);
void shop_update(float dt);
void shop_draw(void);
