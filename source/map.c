// The game map on the bottom screen (GameMap / RoughMapRoom): the current area's rooms, Cornifer's rough drawing when
// you own the area map, the full drawing for rooms mapped with the quill, the Knight's marker with Wayward Compass.
// Visited / mapped scenes are PlayerData lists in the game; here they are bools "svis:<scene>" / "smap:<scene>".
#include "map.h"
#include "gfx.h"
#include "game.h"
#include "pd.h"
#include "lang.h"
#include "charms.h"
#include "hero.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAP_PPU 36.0f
typedef struct { char name[32], pdb[24]; } MArea;
typedef struct { int area; char scene[32]; float cx, cy, w, h; bool act; u32 col; int fr, ff; } MRoom;
static MArea areas[20]; static int nareas;
static MRoom *rooms; static int nrooms;
// map pins (Iselda's): P lines in map.txt, children of a room's drawing in the game's map and shown by their FSMs
typedef struct { int room; float x, y, w, h; int fr, alt; signed char kind; char a[28], b[28], c[28]; } MPin;
static MPin *pins; static int npins;
static SpriteSet MS; static bool loaded;
static int curArea = -1; static char curScene[32];

void map_init(void) {
    if (loaded) return;
    loaded = true;
    spr_load(&MS, "romfs:/gfx/map.spr");
    FILE *f = fopen("romfs:/text/map.txt", "rb"); if (!f) return;
    int cap = 512; rooms = calloc(cap, sizeof(MRoom));
    char ln[256];
    while (fgets(ln, sizeof ln, f)) {
        if (ln[0] == 'A' && nareas < 20) { sscanf(ln + 2, "%31s %23s", areas[nareas].name, areas[nareas].pdb); for (char *p = areas[nareas].name; *p; p++) if (*p == '_') *p = ' '; nareas++; }
        else if (ln[0] == 'R') {
            if (nrooms >= cap) { cap *= 2; rooms = realloc(rooms, cap * sizeof(MRoom)); }
            MRoom *r = &rooms[nrooms]; unsigned rgba = 0xffffffff; int act = 1;
            if (sscanf(ln + 2, "%d %31s %f %f %f %f %d %x", &r->area, r->scene, &r->cx, &r->cy, &r->w, &r->h, &act, &rgba) < 7) continue;
            r->act = act;
            r->col = C2D_Color32((rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255, rgba & 255);
            char nm[40];
            snprintf(nm, sizeof nm, "R:%s", r->scene); r->fr = spr_named(&MS, nm);
            snprintf(nm, sizeof nm, "F:%s", r->scene); r->ff = spr_named(&MS, nm);
            nrooms++;
        } else if (ln[0] == 'P') {
            static int pcap; if (npins >= pcap) { pcap = pcap ? pcap * 2 : 64; pins = realloc(pins, pcap * sizeof(MPin)); }
            MPin *q = &pins[npins]; char sc[32], fn[24], an[24]; int kind;
            if (sscanf(ln + 2, "%31s %f %f %f %f %23s %d %27s %27s %27s %23s", sc, &q->x, &q->y, &q->w, &q->h, fn, &kind, q->a, q->b, q->c, an) < 11) continue;
            q->kind = (signed char)kind; q->fr = spr_named(&MS, fn); q->alt = an[0] == '-' ? -1 : spr_named(&MS, an);
            q->room = -1; for (int i = nrooms - 1; i >= 0; i--) if (!strcmp(rooms[i].scene, sc)) { q->room = i; break; }
            if (q->room >= 0 && q->fr >= 0) npins++;
        }
    }
    fclose(f);
}

static int room_index(const char *scene) {
    for (int i = 0; i < nrooms; i++) if (!strcmp(rooms[i].scene, scene)) return i;
    return -1;
}
static bool has_map(int area) { return area >= 0 && area < nareas && pd_bool(areas[area].pdb); }
static bool mapped(const char *scene) { char k[48]; snprintf(k, sizeof k, "smap:%s", scene); return pd_bool(k) || pd_bool("mapAllRooms"); }

void map_enter_scene(const char *scene) {      // GameManager.AddToScenesVisited
    map_init();
    char k[48]; snprintf(k, sizeof k, "svis:%s", scene); pd_set_bool(k, true);
    snprintf(curScene, sizeof curScene, "%s", scene);
    int ri = room_index(scene);
    if (ri >= 0) curArea = rooms[ri].area;
}
bool map_visited(const char *scene) { char k[48]; snprintf(k, sizeof k, "svis:%s", scene); return pd_bool(k); }

// PlayerData.UpdateGameMap: with the quill, every visited scene whose area map you own becomes mapped
bool map_update(void) {
    map_init();
    if (!pd_bool("hasQuill")) return false;
    bool any = false;
    for (int i = 0; i < nrooms; i++) {
        char k[48]; snprintf(k, sizeof k, "svis:%s", rooms[i].scene);
        if (!pd_bool(k) || !has_map(rooms[i].area) || mapped(rooms[i].scene)) continue;
        snprintf(k, sizeof k, "smap:%s", rooms[i].scene); pd_set_bool(k, true); any = true;
    }
    return any;
}

static bool room_shown(const MRoom *r) {      // the room object is active (Cornifer's map) or SetupMap enabled it
    return r->act || (mapped(r->scene) && pd_bool("hasQuill"));
}

// draws the area containing the current scene into the box (x, y, w, h)
void map_draw_area(float bx, float by, float bw, float bh, float alpha) {
    map_init();
    int a = curArea;
    if (a < 0 || !has_map(a)) {
        font_draw_centered(&A.fontText, a < 0 ? "" : TR("No map of this area"), bx + bw / 2, by + bh / 2 - 8, 1, C2D_Color32(150, 150, 160, (int)(255 * alpha)));
        return;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < nrooms; i++) {
        const MRoom *r = &rooms[i]; if (r->area != a || !room_shown(r)) continue;
        if (r->cx - r->w / 2 < x0) x0 = r->cx - r->w / 2;
        if (r->cx + r->w / 2 > x1) x1 = r->cx + r->w / 2;
        if (r->cy - r->h / 2 < y0) y0 = r->cy - r->h / 2;
        if (r->cy + r->h / 2 > y1) y1 = r->cy + r->h / 2;
    }
    if (x1 <= x0 || y1 <= y0) return;
    float sc = fminf(bw / (x1 - x0), bh / (y1 - y0));        // px per map unit
    float ox = bx + (bw - (x1 - x0) * sc) / 2, oy = by + (bh - (y1 - y0) * sc) / 2;
    float k = sc / MAP_PPU;
    for (int i = 0; i < nrooms; i++) {
        const MRoom *r = &rooms[i]; if (r->area != a || !room_shown(r)) continue;
        int fr = (mapped(r->scene) && r->ff >= 0) ? r->ff : r->fr;
        if (fr < 0) continue;
        float sx = ox + (r->cx - x0) * sc, sy = oy + (y1 - r->cy) * sc;
        spr_draw_tint(&MS, fr, sx, sy, k, k, r->col, 0.3f);
    }
    // pins: the pin FSM ("Check": the pin type bought from Iselda, then the specific bool if it has one - a vendor met,
    // a grave not yet cleared), or a toll bench's sprite swap (paid / not paid)
    for (int i = 0; i < npins; i++) {
        const MPin *q = &pins[i]; const MRoom *r = &rooms[q->room];
        if (r->area != a || !room_shown(r)) continue;
        int fr = q->fr;
        if (q->kind == 1) {
            if (!pd_bool(q->a) || (q->b[0] != '-' && !pd_bool(q->b)) || (q->c[0] != '-' && pd_bool(q->c))) continue;
        } else if (q->kind == 2) { if (!pd_bool(q->a) && q->alt >= 0) fr = q->alt; }
        const Frame *pf = &MS.frames[fr];
        float px = ox + (q->x - x0) * sc, py = oy + (y1 - q->y) * sc;
        float wpx = fmaxf(q->w * sc, 9.0f);       // a few pixels at least: the bottom screen shows a whole area at once
        float s = wpx / (pf->w > 0 ? pf->w : 1);
        spr_draw(&MS, fr, px, py, s, s, alpha);
#ifdef __HOST__
        if (getenv("HK_PINTRACE")) { static int n; if (n++ < 40) fprintf(stderr, "[pin] %s %s at %.0f,%.0f\n", r->scene, q->a, px, py); }
#endif
    }
    // Wayward Compass: the Knight's position within his room's drawing (GameMap.PositionCompass)
    int ri = room_index(curScene);
    if (ri >= 0 && rooms[ri].area == a && charm_compass() && game_room_w() > 0) {
        const MRoom *r = &rooms[ri];
        float fx = P.x / game_room_w() - 0.5f, fy = P.y / game_room_h() - 0.5f;
        float mx = ox + (r->cx + fx * r->w - x0) * sc, my = oy + (y1 - (r->cy + fy * r->h)) * sc;
        const SpriteSet *ic = game_icons();
        int f = ic ? spr_named(ic, "charm_2") : -1;
        if (f >= 0) spr_draw(ic, f, mx, my, 0.35f, 0.35f, alpha);
        else C2D_DrawRectSolid(mx - 2, my - 3, 0.6f, 4, 5, C2D_Color32(255, 255, 255, 255));
    }
}
const char *map_area_name(void) { return curArea >= 0 && curArea < nareas ? areas[curArea].name : ""; }
