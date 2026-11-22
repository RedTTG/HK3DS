#pragma once
// the release number, shown at the bottom of the title screen (bump it with each build that goes out)
#define HK3DS_VERSION "1.1.0"
// the release whose game data the patches (tools/make_patch.py) go over
#define HK3DS_PATCH_BASE "v1.1.0"
#ifdef HK_HD
#define HK3DS_EDITION " HD"     // the New 3DS build (800x240)
#define HK3DS_DATAFILE "hk3ds_hd.romfs"
#else
#define HK3DS_EDITION ""
#define HK3DS_DATAFILE "hk3ds_std.romfs"
#endif
// From v1.1 the game data is a file on the SD card (SD:/3ds/hk3ds/<HK3DS_DATAFILE>, a RomFS image made by mkromfs3ds:
// make data), and the CIA / .3dsx hold only the code: updates are a small CIA plus changed files (source/patch.c).
// A full CIA or .3dsx with the data inside still works when there's no data file.
