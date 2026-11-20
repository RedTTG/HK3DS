#pragma once
// Patches on top of an installed CIA, so a fix doesn't need the whole game downloaded again:
//  - the game code comes from Luma3DS's game patching: SD:/luma/titles/<title id>/code.bin + exheader.bin
//  - changed data files sit in SD:/3ds/hk3ds/patch/<std|hd>/ at their romfs paths, listed in files.txt, whose first
//    line is "HKPATCH <version>". Only a patch made for this code's version is used.
#include <stdbool.h>
#include <stddef.h>
#ifdef HK_HD
#define HK_TITLE_ID "000400000F4C3E00"
#else
#define HK_TITLE_ID "000400000F4C3D00"
#endif
void hk_patch_init(void);                                    // read files.txt (once; also done on the first romfs open)
bool hk_patch_path(const char *path, char *out, size_t size); // a romfs:/ path the patch replaces -> its SD path
int hk_patch_files(void);                                    // how many files the active patch replaces
const char *hk_patch_warning(void);                          // non-NULL: an old patch is overriding a newer install
