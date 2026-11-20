#pragma once
#include <stdbool.h>
void map_init(void);
void map_enter_scene(const char *scene);
bool map_visited(const char *scene);
bool map_update(void);                 // PlayerData.UpdateGameMap (bench / quill): true if anything new was mapped
void map_draw_area(float x, float y, float w, float h, float alpha);
const char *map_area_name(void);
