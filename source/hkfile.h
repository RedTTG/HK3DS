#pragma once
// Force-included into every 3DS source file (Makefile: -include hkfile.h) so each fopen of a romfs:/ file can be
// served from a data patch on the SD card (patch.c). The PC test build does the same in host_fopen.
#ifndef __HOST__
#include <stdio.h>
FILE *hk_fopen(const char *path, const char *mode);
#define fopen hk_fopen
#endif
