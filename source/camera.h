#pragma once
#include <stdbool.h>

void cam_load(const char *room, float sceneW, float sceneH);   // romfs:/rooms/<room>.cam lock zones + scene limits
void cam_position_to_hero(void);                               // snap after entering a scene / respawning
void cam_set_hero(float x, float y);                          // Knight transform position this frame
void cam_update(float dt);                                     // per rendered frame
void cam_freeze(bool frozen);
float cam_x(void);
float cam_y(void);
