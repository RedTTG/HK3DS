// Data patches (see patch.h). Every fopen of a romfs:/ file goes through hk_patch_path first: on the 3DS through
// hk_fopen (hkfile.h, force-included by the Makefile), on the PC through host_fopen.
#include "patch.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef fopen      // this file opens the SD card and romfs itself

void hk_log(const char *fmt, ...);

#ifdef HK_HD
#define PATCH_DIR "sdmc:/3ds/hk3ds/patch/hd/"
#else
#define PATCH_DIR "sdmc:/3ds/hk3ds/patch/std/"
#endif

static int state;            // 0 not read, 1 no patch, 2 active
static char **names; static int nnames;
static char warning[160];

static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

// "1.0.22" vs "1.0.21": >0 when a is newer
static int vercmp(const char *a, const char *b) {
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) return x > y ? 1 : -1;
        if (*a == '.') a++;
        if (*b == '.') b++;
        if (!*a && !*b) break;
    }
    return 0;
}

static FILE *open_raw(const char *path, const char *mode) {
#ifdef __HOST__
    extern FILE *host_open_mapped(const char *p, const char *m);
    return host_open_mapped(path, mode);
#else
    return fopen(path, mode);
#endif
}

void hk_patch_init(void) {
    if (state) return;
    state = 1;
    // a full install newer than this code means Luma is still loading an old patch's code.bin
    FILE *v = open_raw("romfs:/text/version.txt", "rb");
    if (v) {
        char base[24] = "";
        if (fscanf(v, "%23s", base) == 1 && vercmp(base, HK3DS_VERSION) > 0)
            snprintf(warning, sizeof warning, "v%s is installed, but an old patch (v%s) is loaded over it.\n"
                     "Delete its folder in SD:/luma/titles (named in README_PATCH.txt).", base, HK3DS_VERSION);
        fclose(v);
    }
    FILE *f = open_raw(PATCH_DIR "files.txt", "rb");
    if (!f) { hk_log("patch: none"); return; }
    char ln[256], ver[24] = "";
    if (!fgets(ln, sizeof ln, f) || sscanf(ln, "HKPATCH %23s", ver) != 1) { fclose(f); hk_log("patch: files.txt unreadable"); return; }
    if (strcmp(ver, HK3DS_VERSION)) {
        fclose(f); hk_log("patch v%s ignored: this is v" HK3DS_VERSION, ver); return;
    }
    int cap = 0;
    while (fgets(ln, sizeof ln, f)) {
        size_t l = strlen(ln);
        while (l && (ln[l - 1] == '\n' || ln[l - 1] == '\r' || ln[l - 1] == ' ')) ln[--l] = 0;
        if (!l || ln[0] == '#') continue;
        if (nnames >= cap) { cap = cap ? cap * 2 : 64; names = realloc(names, cap * sizeof *names); }
        names[nnames++] = strdup(ln);
    }
    fclose(f);
    qsort(names, nnames, sizeof *names, cmp);
    state = 2;
    hk_log("patch v%s: %d files", ver, nnames);
}

bool hk_patch_path(const char *path, char *out, size_t size) {
    if (strncmp(path, "romfs:/", 7)) return false;
    if (!state) hk_patch_init();
    if (state != 2) return false;
    const char *rel = path + 7;
    if (!bsearch(&rel, names, nnames, sizeof *names, cmp)) return false;
    snprintf(out, size, PATCH_DIR "%s", rel);
    return true;
}

int hk_patch_files(void) { return state == 2 ? nnames : 0; }
const char *hk_patch_warning(void) { return warning[0] ? warning : NULL; }

#ifndef __HOST__
// every other file's fopen (hkfile.h)
FILE *hk_fopen(const char *path, const char *mode) {
    if (mode[0] == 'r') {
        char p[256];
        if (hk_patch_path(path, p, sizeof p)) { FILE *f = fopen(p, mode); if (f) return f; }
    }
    return fopen(path, mode);
}
#endif
