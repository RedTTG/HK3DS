#pragma once
#include <stdbool.h>

void masks_load(const char *room);      // romfs:/rooms/<room>.msk + _msk.spr (none: no masks)
void masks_unload(void);
void masks_update(float dt, float hx0, float hy0, float hx1, float hy1);   // Knight collider box
void masks_draw(float camx, float camy);
void masks_draw_range(float camx, float camy, float flo, float fhi);   // only those with parallax F in [flo, fhi)
void masks_hit(long fid);               // a Breakable's hitEventReciever outside the object tree (scene GameObject fileID)
