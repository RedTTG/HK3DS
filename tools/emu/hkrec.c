// Minimal headless libretro frontend for testing HK3DS on the Azahar core (software renderer).
// usage: hkfront <core.so> <game.3dsx> <outdir>
// env: HK_SCRIPT "frames:KEYS;..." (KEYS joined with '+': A B X Y L R START SELECT UP DOWN LEFT RIGHT)
//      HK_SHOTS  "f1,f2,..." frames to save as PPM; HK_EVERY n = also save every n frames
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "libretro.h"

static const char *outdir = ".";
static char sysdir[512], savedir[512];
static long frame;
static int nseg; static int segFrames[8192]; static unsigned segKeys[8192]; static long totalFrames;
static long shots[1024]; static int nshots; static long every;
static unsigned curKeys;
static const void *lastFb; static unsigned lastW, lastH; static size_t lastPitch;

enum { K_A = 1, K_B = 2, K_X = 4, K_Y = 8, K_L = 16, K_R = 32, K_START = 64, K_SELECT = 128, K_UP = 256, K_DOWN = 512, K_LEFT = 1024, K_RIGHT = 2048 };

static unsigned parse_keys(const char *s) {
    unsigned k = 0; char tmp[128]; strncpy(tmp, s, 127); tmp[127] = 0;
    const char *n[] = { "A", "B", "X", "Y", "L", "R", "START", "SELECT", "UP", "DOWN", "LEFT", "RIGHT" };
    for (char *sv, *t = strtok_r(tmp, "+", &sv); t; t = strtok_r(NULL, "+", &sv))
        for (int i = 0; i < 12; i++) if (!strcmp(t, n[i])) k |= 1u << i;
    return k;
}

static void log_cb(enum retro_log_level level, const char *fmt, ...) {
    if (level < RETRO_LOG_WARN && !getenv("HK_VERBOSE")) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}

static const char *get_var(const char *key) {
    if (!strcmp(key, "citra_graphics_api")) return "Software";
    if (!strcmp(key, "citra_is_new_3ds")) return getenv("HK_EMU_O3DS") ? "Old 3DS" : "New 3DS";
    if (!strcmp(key, "citra_cpu_clock_percentage")) return getenv("HK_EMU_CLOCK") ? getenv("HK_EMU_CLOCK") : "100";
    if (!strcmp(key, "citra_layout_option")) return "default";
    if (!strcmp(key, "citra_use_cpu_jit")) return "enabled";
    if (!strcmp(key, "citra_use_virtual_sd")) return "enabled";
    if (!strcmp(key, "citra_use_libretro_save_path")) return "LibRetro Default";
    if (!strcmp(key, "citra_resolution_factor")) return getenv("HK_EMU_RES") ? getenv("HK_EMU_RES") : "1";
    if (!strcmp(key, "citra_audio_emulation")) return "hle";
    return NULL;
}

static bool env_cb(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = savedir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data; v->value = get_var(v->key); return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 0; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_GEOMETRY: case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS: case RETRO_ENVIRONMENT_SET_MESSAGE:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_JIT_CAPABLE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned *)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: return false;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: return false;
    case RETRO_ENVIRONMENT_GET_CURRENT_SOFTWARE_FRAMEBUFFER: return false;
    default: return false;
    }
}

static void save_ppm(const char *name) {
    if (!lastFb) return;
    char path[600]; snprintf(path, sizeof path, "%s/%s", outdir, name);
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", lastW, lastH);
    for (unsigned y = 0; y < lastH; y++) {
        const uint32_t *row = (const uint32_t *)((const uint8_t *)lastFb + y * lastPitch);
        for (unsigned x = 0; x < lastW; x++) { uint32_t p = row[x]; uint8_t c[3] = { p >> 16, p >> 8, p }; fwrite(c, 1, 3, f); }
    }
    fclose(f);
}

static uint8_t *fbCopy; static size_t fbCopySize;
static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;   // duped frame
    size_t sz = pitch * h;
    if (sz > fbCopySize) { fbCopy = realloc(fbCopy, sz); fbCopySize = sz; }
    memcpy(fbCopy, data, sz);
    lastFb = fbCopy; lastW = w; lastH = h; lastPitch = pitch;
}
// recording (HK_REC=prefix, HK_REC_FROM=frame): every frame from then on to prefix.mp4 (through ffmpeg) and the
// sound to prefix.s16 (signed 16-bit stereo at the core's rate, printed at load)
static FILE *recV, *recA; static long recFrom = -1; static bool recOn;
// HK_REC_LEN=N: after HK_REC_FROM, wait for the room load's black screen, start at the first frame the room fades in
// (top screen brighter than HK_REC_LIGHT, default 4) and stop after N frames
static long recLen = 0, recStart = -1, darkRun = 0; static int recLight = 4;
static int top_brightness(void) {
    if (!lastFb) return 0;
    unsigned long sum = 0, n = 0;
    for (unsigned y = 0; y < lastH / 2; y += 6) { const uint32_t *row = (const uint32_t *)((const uint8_t *)lastFb + y * lastPitch);
        for (unsigned x = 0; x < lastW; x += 6) { uint32_t p = row[x]; sum += ((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255); n += 3; } }
    return n ? (int)(sum / n) : 0;
}
static void audio_cb(int16_t l, int16_t r) { if (recOn && recA) { int16_t s2[2] = { l, r }; fwrite(s2, 2, 2, recA); } }
static size_t audio_batch_cb(const int16_t *d, size_t n) { if (recOn && recA) fwrite(d, 4, n, recA); return n; }
static void input_poll_cb(void) {}
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        int16_t m = 0;
        static const int map[12] = { RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_X, RETRO_DEVICE_ID_JOYPAD_Y,
            RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT,
            RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT };
        for (int i = 0; i < 12; i++) if (curKeys & (1u << i)) m |= 1 << map[i];
        return m;
    }
    (void)index;
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_A: return !!(curKeys & K_A);
    case RETRO_DEVICE_ID_JOYPAD_B: return !!(curKeys & K_B);
    case RETRO_DEVICE_ID_JOYPAD_X: return !!(curKeys & K_X);
    case RETRO_DEVICE_ID_JOYPAD_Y: return !!(curKeys & K_Y);
    case RETRO_DEVICE_ID_JOYPAD_L: return !!(curKeys & K_L);
    case RETRO_DEVICE_ID_JOYPAD_R: return !!(curKeys & K_R);
    case RETRO_DEVICE_ID_JOYPAD_START: return !!(curKeys & K_START);
    case RETRO_DEVICE_ID_JOYPAD_SELECT: return !!(curKeys & K_SELECT);
    case RETRO_DEVICE_ID_JOYPAD_UP: return !!(curKeys & K_UP);
    case RETRO_DEVICE_ID_JOYPAD_DOWN: return !!(curKeys & K_DOWN);
    case RETRO_DEVICE_ID_JOYPAD_LEFT: return !!(curKeys & K_LEFT);
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: return !!(curKeys & K_RIGHT);
    }
    return 0;
}

#define SYM(name) name##_t name = (name##_t)dlsym(h, #name); if (!name) { fprintf(stderr, "missing %s\n", #name); return 1; }
typedef void (*retro_init_t)(void); typedef void (*retro_deinit_t)(void); typedef void (*retro_run_t)(void);
typedef bool (*retro_load_game_t)(const struct retro_game_info *); typedef void (*retro_unload_game_t)(void);
typedef void (*retro_set_environment_t)(retro_environment_t); typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_t)(retro_audio_sample_t); typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t); typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_get_system_av_info_t)(struct retro_system_av_info *);
typedef void (*retro_set_controller_port_device_t)(unsigned, unsigned);

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: hkfront core.so game.3dsx outdir\n"); return 1; }
    outdir = argv[3]; mkdir(outdir, 0777);
    snprintf(sysdir, sizeof sysdir, "%s/system", getenv("HK_EMUDIR") ? getenv("HK_EMUDIR") : "emu_run");
    snprintf(savedir, sizeof savedir, "%s/saves", getenv("HK_EMUDIR") ? getenv("HK_EMUDIR") : "emu_run");
    mkdir(getenv("HK_EMUDIR") ? getenv("HK_EMUDIR") : "emu_run", 0777); mkdir(sysdir, 0777); mkdir(savedir, 0777);
    const char *sc = getenv("HK_SCRIPT"); if (!sc) sc = "600:";
    char *scr = strdup(sc);
    for (char *sv, *seg = strtok_r(scr, ";", &sv); seg && nseg < 8192; seg = strtok_r(NULL, ";", &sv)) {
        char *c = strchr(seg, ':'); segFrames[nseg] = atoi(seg); segKeys[nseg] = c ? parse_keys(c + 1) : 0; totalFrames += segFrames[nseg]; nseg++;
    }
    if (getenv("HK_SHOTS")) { char *t = strdup(getenv("HK_SHOTS")); for (char *x = strtok(t, ","); x && nshots < 1024; x = strtok(NULL, ",")) shots[nshots++] = atol(x); }
    if (getenv("HK_EVERY")) every = atol(getenv("HK_EVERY"));

    void *h = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    SYM(retro_set_environment) SYM(retro_set_video_refresh) SYM(retro_set_audio_sample) SYM(retro_set_audio_sample_batch)
    SYM(retro_set_input_poll) SYM(retro_set_input_state) SYM(retro_init) SYM(retro_load_game) SYM(retro_run)
    SYM(retro_get_system_av_info) SYM(retro_set_controller_port_device) SYM(retro_unload_game) SYM(retro_deinit)
    retro_set_environment(env_cb);
    retro_set_video_refresh(video_cb); retro_set_audio_sample(audio_cb); retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb); retro_set_input_state(input_state_cb);
    retro_init();
    struct retro_game_info gi = { .path = argv[2] };
    if (!retro_load_game(&gi)) { fprintf(stderr, "load_game failed\n"); return 2; }
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    struct retro_system_av_info av; retro_get_system_av_info(&av);
    fprintf(stderr, "loaded: %ux%u @ %.2f fps, sound %.1f Hz\n", av.geometry.base_width, av.geometry.base_height, av.timing.fps, av.timing.sample_rate);
    if (getenv("HK_REC_LEN")) recLen = atol(getenv("HK_REC_LEN"));
    if (getenv("HK_REC_LIGHT")) recLight = atoi(getenv("HK_REC_LIGHT"));
    if (getenv("HK_REC")) { recFrom = getenv("HK_REC_FROM") ? atol(getenv("HK_REC_FROM")) : 0; char ap[600]; snprintf(ap, sizeof ap, "%s.s16", getenv("HK_REC")); recA = fopen(ap, "wb"); }
    for (frame = 0; frame < totalFrames; frame++) {
        long acc = 0; curKeys = 0;
        for (int i = 0; i < nseg; i++) { if (frame < acc + segFrames[i]) { curKeys = segKeys[i]; break; } acc += segFrames[i]; }
        if (recLen) recOn = recStart >= 0;
        else recOn = recFrom >= 0 && frame >= recFrom;
        retro_run();
        if (recLen && recStart < 0 && recFrom >= 0 && frame >= recFrom) {      // waiting for the room to show
            int b = top_brightness();
            if (b < recLight) darkRun++;
            else if (darkRun >= 30) { recStart = frame + 1; fprintf(stderr, "room shows at frame %ld (brightness %d)\n", frame, b); }
            else darkRun = 0;
        }
        if (recLen && recStart >= 0 && frame - recStart >= recLen) break;
        if (recOn && lastFb) {
            if (!recV) {
                char cmd[1024]; snprintf(cmd, sizeof cmd, "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgr0 -s %ux%u -r 60 -i - -c:v libx264 -preset veryfast -crf 10 -pix_fmt yuv444p %s.mp4", lastW, lastH, getenv("HK_REC"));
                recV = popen(cmd, "w"); fprintf(stderr, "rec %ux%u from frame %ld\n", lastW, lastH, frame);
            }
            for (unsigned y = 0; y < lastH; y++) fwrite((const uint8_t *)lastFb + y * lastPitch, 4, lastW, recV);
        }
        bool want = every && frame % every == 0;
        {   // HK_SHOT_ON_CYCLE: a screenshot named after each room as the game logs "cycle <room>:" (end of its stay)
            static FILE *lf; static long lpos; static int tick;
            if (getenv("HK_SHOT_ON_CYCLE") && ++tick % 4 == 0) {
                if (!lf) { char lp[600]; snprintf(lp, sizeof lp, "%s/Azahar/sdmc/%s", savedir, getenv("HK_SDLOG") ? getenv("HK_SDLOG") : "3ds/hk3ds/log.txt"); lf = fopen(lp, "rb"); }
                if (lf) {
                    char ln[512]; fseek(lf, lpos, SEEK_SET);
                    while (fgets(ln, sizeof ln, lf)) {
                        if (!strchr(ln, '\n')) break;
                        lpos = ftell(lf);
                        char *c = strstr(ln, "] cycle "); char room[64];
                        if (c && sscanf(c + 8, "%63[^:]:", room) == 1 && strncmp(room, "done", 4)) { char n[96]; snprintf(n, sizeof n, "room_%s.ppm", room); save_ppm(n); }
                    }
                    clearerr(lf);
                }
            }
        }
        for (int i = 0; i < nshots; i++) if (shots[i] == frame) want = true;
        if (want) { char n[64]; snprintf(n, sizeof n, "e%05ld.ppm", frame); save_ppm(n); }
    }
    if (recV) pclose(recV);
    if (recA) fclose(recA);
    retro_unload_game(); retro_deinit();
    fprintf(stderr, "done %ld frames\n", frame);
    return 0;
}
