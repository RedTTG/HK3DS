// This frame's input mapped to Hollow Knight's InputHandler actions, for FSM actions (ListenForUp, GetButtonDown...)
// and the dialogue / menu UI. Filled by game.c every frame.
#pragma once
#include <stdbool.h>

typedef struct {
    bool upP, downP, leftP, rightP;       // pressed this frame (d-pad or circle pad crossing)
    bool upH, downH, leftH, rightH;
    bool jumpP, jumpH, attackP, attackH, castP, castH, dashP, dashH;
    bool submitP, cancelP;                // menu submit (A) / cancel (B)
    bool dreamNailP, dreamNailH, quickMapP, quickMapH, inventoryP, superDashP, superDashH;
    bool quickCastP, quickCastH, quickCastR;
} GameInput;
extern GameInput gin;
