#pragma once
#include <stdbool.h>
#include "fsm.h"

UObj *msg_create_getitem(FVar *spriteVar);   // CreateUIMsgGetItem: returns the message object ("Msg Control" FSM: Item)
void msgs_reset(void);
bool msgs_busy(void);
void msgs_update(float dt);
void msgs_draw(void);
