// World queries shared by the hero, enemies and the FSM runtime (implemented in game.c on the room's collision).
#pragma once
#include <stdbool.h>

bool  w_solid_box(float x0, float y0, float x1, float y1);          // terrain (tile grid + solid room entities)
// move a box (centre, half size) by dx,dy against terrain; flags 1=hit x, 2=hit floor, 4=hit ceiling
int   w_move_box(float *cx, float *cy, float hw, float hh, float dx, float dy, float stepUp);
bool  w_unstick_box(float *cx, float *cy, float hw, float hh, float maxd);
// Physics2D.Raycast against terrain (layer 8). Returns true on hit; *hitDist = distance to the hit point.
bool  w_raycast(float x, float y, float dx, float dy, float dist, float *hitDist);
float w_room_w(void);
float w_room_h(void);
