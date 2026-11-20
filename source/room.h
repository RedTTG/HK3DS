#pragma once
#include "gfx.h"

enum {
    EK_SOLID, EK_PLATFORM, EK_BREAKABLE_WALL, EK_COLLAPSE_FLOOR, EK_GATE_DOOR, EK_SPIKES, EK_STALACTITE,
    EK_ENEMY, EK_GATE, EK_HAZARD_RESPAWN, EK_HAZARD_RESPAWN_MARKER, EK_RESPAWN_MARKER, EK_GEO_ROCK, EK_CHEST,
    EK_LIFEBLOOD_COCOON, EK_LORE_TABLET, EK_BENCH, EK_VINE, EK_COUNT
};

typedef struct {
    u8 kind, ai;
    float x, y, w, h;      // centre + size (world units, y up)
    s16 hp, geo;
    char name[32];
    char target[32];       // gates: "room:gate"; enemies: "pivotdx pivotdy"
    char extra[96];        // enemies: "lib|walk|idle|chase|turn|deathAir|deathLand|attack"
    int image;             // frame in room->ents set, -1 none
} RoomEnt;

typedef struct {
    float F, x0, y0, res;
    u16 tx, ty;
    u8 fmt, front;
    u32 *tileOff;          // per tile offset into blob (0 = empty)
    u32 *tileSize;         // 0 = solid colour (4 bytes at offset)
    // per tile, 8x8 blocks of 16x16 texels (bit by*8+bx, top row first), learnt when the tile is unpacked:
    u64 *opq;              // blocks that are fully opaque (shrunk by a block: filtering softens their edges)
    u64 *nem;              // blocks with anything visible (grown by a block, for the same reason)
    u8 *known;             // opq and nem are valid
    u64 *vis;              // this frame: the blocks to draw (not empty, not behind opaque art drawn later)
    void **cep;            // per tile: the tile cache entry that last held it (checked before use)
} Layer;

typedef struct {
    char id[32];
    int w, h;
    u8 *solid;             // collision cells, bottom-up rows of (cw+7)/8 bytes
    int res, cw, ch;       // cells per unit, grid size in cells
    float spawnx, spawny;
    char music[32], ambience[32], title[32], subtitle[32];
    int nents;
    RoomEnt *ents;
    int nlayers;
    Layer *layers;
    u8 *blob;              // .lyr file contents
    bool zipped;           // HKL2: tiles are zlib streams
    int tile;              // tile size (256)
    int S;                 // baked px per unit
    int xd;                // texels across per screen pixel (2: HD art for the 800-wide screen)
    SpriteSet entset;      // entity images
    SpriteSet enemyset;    // enemy animations for this room
    // minimap texture
    C3D_Tex mapTex; Tex3DS_SubTexture mapSub; C2D_Image mapImg; bool hasMap;
} Room;

bool room_load(Room *r, const char *id);
void room_free(Room *r);
bool room_solid_tile(const Room *r, int x, int y);     // tile coords (1 unit)
bool room_solid_cell(const Room *r, int cx, int cy);   // cell coords (1/res unit)
void room_draw_layers(Room *r, float camx, float camy, bool front);
void room_draw_over(Room *r, float camx, float camy);
extern void (*room_plane_hook)(void);   // the 'over' layers (higher sorting layer), after the foreground
void room_tilecache_reset(void);
void room_tilecache_init(void);   // once at boot
void room_load_entity_art(Room *r, bool images, bool enemies);
