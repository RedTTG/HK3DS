#pragma once
// the release number, shown at the bottom of the title screen (bump it with each build that goes out)
#define HK3DS_VERSION "1.0.24"
// the CIA release the patches (tools/make_patch.py) go over
#define HK3DS_PATCH_BASE "v1.0.21"
#ifdef HK_HD
#define HK3DS_EDITION " HD"     // the New 3DS build (800x240)
#else
#define HK3DS_EDITION ""
#endif
