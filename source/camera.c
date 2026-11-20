// Hollow Knight's camera: CameraTarget.Update + CameraController.LateUpdate and CameraLockArea zones, ported from
// the decompiled sources with the _GameCameras prefab values. Camera-centre limits keep HK's view half-size
// (14.6 x 8.3 units) so the 3DS view (25 x 15 units) never shows past what the game frames.
#include "camera.h"
#include "hero.h"
#include "gfx.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HALF_X 14.6f
#define HALF_Y 8.3f

typedef struct { float x0, y0, x1, y1, xmin, xmax, ymin, ymax; bool noUp, noDown, maxPri, inside; int np; float px[32], py[32]; } Lock;   // np > 0: polygon outline
static Lock locks[64]; static int nlocks;
static int lockList[64]; static int nlist;          // CameraController.lockZoneList (indices)
static int curLock = -1;

enum { CM_FROZEN, CM_FOLLOWING, CM_LOCKED };
enum { TM_FOLLOW, TM_LOCK, TM_FREE };

// CameraController
static int mode = CM_FOLLOWING;
static float camX, camY, cvx, cvy;                 // transform + SmoothDamp velocities
static float xLimit, yLimit, startLockedTimer;
static float xLockMin, xLockMax, yLockMin, yLockMax;
static float lookOffset;
static const float dampTimeC = 0.15f;
// CameraTarget
static int tmode = TM_FOLLOW;
static float tX, tY, tvx, tvy, xOffset, dashOffset, fallCatcher;
static float txLockMin, txLockMax, tyLockMin, tyLockMax;
static bool stickX, stickY, fallStick, enteredLeft, enteredRight, enteredTop, enteredBot, exitedLeft, exitedRight, exitedTop, exitedBot;
static float dampTimeX = 0.075f, dampTimeY = 0.075f, slowTimer;
static float heroPrevX, heroPrevY;
static const float dampTimeNormal = 0.075f, dampTimeSlow = 0.5f, slowTime = 0.5f, xLookAhead = 1, dashLookAhead = 1.5f, snapDistance = 0.15f;

static float HX, HY;              // hero transform (interpolated render position)
void cam_set_hero(float x, float y) { HX = x; HY = y; }
float cam_x(void) { return camX; }
float cam_y(void) { return camY; }

// Mathf.SmoothDamp (maxSpeed = infinity)
static float smooth_damp(float cur, float target, float *vel, float smoothTime, float dt) {
    if (dt <= 0) return cur;
    smoothTime = fmaxf(0.0001f, smoothTime);
    float omega = 2 / smoothTime, x = omega * dt, e = 1 / (1 + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = cur - target, orig = target;
    target = cur - change;
    float temp = (*vel + omega * change) * dt;
    *vel = (*vel - omega * temp) * e;
    float out = target + (change + temp) * e;
    if ((orig - cur > 0) == (out > orig)) { out = orig; *vel = (out - orig) / dt; }
    return out;
}

void cam_load(const char *room, float sceneW, float sceneH) {
    nlocks = 0; nlist = 0; curLock = -1;
    xLimit = sceneW - HALF_X; yLimit = sceneH - HALF_Y;
    if (xLimit < HALF_X) xLimit = HALF_X;
    if (yLimit < HALF_Y) yLimit = HALF_Y;
    char path[96]; snprintf(path, sizeof path, "romfs:/rooms/%s.cam", room);
    size_t sz; char *txt = read_file(path, &sz);
    if (txt) {
        char *sv, *ln = strtok_r(txt, "\n", &sv);
        while (ln && nlocks < 64) {
            Lock *l = &locks[nlocks]; int a, b, c;
            if (sscanf(ln, "L %f %f %f %f %f %f %f %f %d %d %d", &l->x0, &l->y0, &l->x1, &l->y1, &l->xmin, &l->xmax, &l->ymin, &l->ymax, &a, &b, &c) == 11) {
                l->noUp = a; l->noDown = b; l->maxPri = c; l->inside = false; l->np = 0;
                char *pp = strstr(ln, " P ");      // polygon zone: " P n x y x y ..."
                if (pp) { int n = (int)strtol(pp + 3, &pp, 10); for (int k = 0; k < n && k < 32; k++) { l->px[k] = strtof(pp, &pp); l->py[k] = strtof(pp, &pp); } l->np = n > 32 ? 32 : n; }
                // CameraLockArea.ValidateBounds (Start): unset limits become the defaults; a zone with all four at 0 is ignored
                if (l->xmin == -1) l->xmin = HALF_X; if (l->xmax == -1) l->xmax = xLimit;
                if (l->ymin == -1) l->ymin = HALF_Y; if (l->ymax == -1) l->ymax = yLimit;
                if (!(l->xmin == 0 && l->xmax == 0 && l->ymin == 0 && l->ymax == 0)) nlocks++;
            }
            ln = strtok_r(NULL, "\n", &sv);
        }
        free(txt);
    }
    // SceneInit
    startLockedTimer = 0.5f; cvx = cvy = 0;
    xLockMin = 0; xLockMax = xLimit; yLockMin = 0; yLockMax = yLimit;
    mode = CM_FOLLOWING;
    tmode = TM_FOLLOW; stickX = stickY = true; fallCatcher = 0;
    txLockMin = 0; txLockMax = xLimit; tyLockMin = 0; tyLockMax = yLimit;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static void keep_scene(float *x, float *y) {
    if (*x < HALF_X) *x = HALF_X;
    if (*x > xLimit) *x = xLimit;
    if (*y < HALF_Y) *y = HALF_Y;
    if (*y > yLimit) *y = yLimit;
}

/* ---------------- CameraTarget lock modes ---------------- */
static void target_enter_lock(float a, float b, float c, float d) {
    txLockMin = a; txLockMax = b; tyLockMin = c; tyLockMax = d; tmode = TM_LOCK;
    if ((!enteredLeft || txLockMin != HALF_X) && (!enteredRight || txLockMax != xLimit)) dampTimeX = dampTimeSlow;
    if ((!enteredBot || tyLockMin != HALF_Y) && (!enteredTop || tyLockMax != yLimit)) dampTimeY = dampTimeSlow;
    slowTimer = slowTime;
    stickX = fabsf(tX - HX) <= snapDistance;
    stickY = fabsf(tY - HY) <= snapDistance;
}
static void target_enter_lock_instant(float a, float b, float c, float d) {
    txLockMin = a; txLockMax = b; tyLockMin = c; tyLockMax = d; tmode = TM_LOCK;
    tX = clampf(tX, txLockMin, txLockMax); tY = clampf(tY, tyLockMin, tyLockMax);
    stickX = stickY = true;
}
static void target_exit_lock(void) {
    if (tmode == TM_FREE) return;
    tmode = (P.c.hazardDeath || P.dead || (P.trans != TS_WAITING_TO_TRANSITION && P.trans != TS_WAITING_TO_ENTER_LEVEL)) ? TM_FREE : TM_FOLLOW;
    if ((!exitedLeft || txLockMin != HALF_X) && (!exitedRight || txLockMax != xLimit)) dampTimeX = dampTimeSlow;
    if ((!exitedBot || tyLockMin != HALF_Y) && (!exitedTop || tyLockMax != yLimit)) dampTimeY = dampTimeSlow;
    slowTimer = slowTime;
    fallStick = false;
    txLockMin = 0; txLockMax = xLimit; tyLockMin = 0; tyLockMax = yLimit;
    stickX = fabsf(tX - HX) <= snapDistance;
    stickY = fabsf(tY - HY) <= snapDistance;
}

/* ---------------- CameraController lock list ---------------- */
static void lock_to_area(int i) {
    for (int k = 0; k < nlist; k++) if (lockList[k] == i) return;
    if (nlist < 64) lockList[nlist++] = i;
    Lock *l = &locks[i];
    if (curLock >= 0 && locks[curLock].maxPri && !l->maxPri) return;
    curLock = i; mode = CM_LOCKED;
    xLockMin = l->xmin < 0 ? HALF_X : l->xmin;
    xLockMax = l->xmax < 0 ? xLimit : l->xmax;
    yLockMin = l->ymin < 0 ? HALF_Y : l->ymin;
    yLockMax = l->ymax < 0 ? yLimit : l->ymax;
    if (startLockedTimer > 0) {
        tX = clampf(HX, xLockMin, xLockMax); tY = clampf(HY, yLockMin, yLockMax);
        target_enter_lock_instant(xLockMin, xLockMax, yLockMin, yLockMax);
        camX = tX; camY = tY;
    } else target_enter_lock(xLockMin, xLockMax, yLockMin, yLockMax);
}
static void release_lock(int i) {
    int k = 0; for (; k < nlist; k++) if (lockList[k] == i) break;
    if (k < nlist) { memmove(&lockList[k], &lockList[k + 1], sizeof(int) * (nlist - k - 1)); nlist--; }
    if (i != curLock) return;
    if (nlist > 0) {
        curLock = lockList[nlist - 1]; Lock *l = &locks[curLock];
        xLockMin = l->xmin; xLockMax = l->xmax; yLockMin = l->ymin; yLockMax = l->ymax;   // (raw values, as the game does)
        target_enter_lock(xLockMin, xLockMax, yLockMin, yLockMax);
        return;
    }
    target_exit_lock();
    curLock = -1;
    if (!P.c.hazardDeath && !P.dead) mode = CM_FOLLOWING;
}

static bool in_poly(const Lock *l, float x, float y) {
    bool c = false;
    for (int i = 0, j = l->np - 1; i < l->np; j = i++)
        if ((l->py[i] > y) != (l->py[j] > y) && x < (l->px[j] - l->px[i]) * (y - l->py[i]) / (l->py[j] - l->py[i]) + l->px[i]) c = !c;
    return c;
}
static bool seg_hits_box(float ax, float ay, float bx, float by, float x0, float y0, float x1, float y1) {
    for (int k = 0; k <= 8; k++) { float t = k / 8.0f, x = ax + (bx - ax) * t, y = ay + (by - ay) * t; if (x > x0 && x < x1 && y > y0 && y < y1) return true; }
    return false;
}
static bool box_in_lock(const Lock *l, float hx0, float hy0, float hx1, float hy1) {
    if (!(hx0 < l->x1 && hx1 > l->x0 && hy0 < l->y1 && hy1 > l->y0)) return false;
    if (l->np < 3) return true;
    // polygon trigger against the Knight's box: a corner or the centre inside, or an edge crossing the box
    if (in_poly(l, hx0, hy0) || in_poly(l, hx1, hy0) || in_poly(l, hx0, hy1) || in_poly(l, hx1, hy1) || in_poly(l, (hx0 + hx1) / 2, (hy0 + hy1) / 2)) return true;
    for (int i = 0, j = l->np - 1; i < l->np; j = i++) if (seg_hits_box(l->px[j], l->py[j], l->px[i], l->py[i], hx0, hy0, hx1, hy1)) return true;
    return false;
}
// OnTriggerEnter2D / Exit2D of the zones against the Knight's collider
static void update_zones(float hx0, float hy0, float hx1, float hy1) {
    for (int i = 0; i < nlocks; i++) {
        Lock *l = &locks[i];
        bool in = box_in_lock(l, hx0, hy0, hx1, hy1);
        if (in == l->inside) continue;
        l->inside = in;
        float x = HX, y = HY;
        if (in) {
            enteredLeft = x > l->x0 - 1 && x < l->x0 + 1; enteredRight = x > l->x1 - 1 && x < l->x1 + 1;
            enteredTop = y > l->y1 - 2 && y < l->y1 + 2; enteredBot = y > l->y0 - 1 && y < l->y0 + 1;
            lock_to_area(i);
        } else {
            exitedLeft = x > l->x0 - 1 && x < l->x0 + 1; exitedRight = x > l->x1 - 1 && x < l->x1 + 1;
            exitedTop = y > l->y1 - 2 && y < l->y1 + 2; exitedBot = y > l->y0 - 1 && y < l->y0 + 1;
            release_lock(i);
        }
    }
}

/* ---------------- per frame ---------------- */
static void set_damp_time(float dt) {
    if (slowTimer > 0) { slowTimer -= dt; return; }
    if (dampTimeX > dampTimeNormal) dampTimeX -= 0.007f; else if (dampTimeX < dampTimeNormal) dampTimeX = dampTimeNormal;
    if (dampTimeY > dampTimeNormal) dampTimeY -= 0.007f; else if (dampTimeY < dampTimeNormal) dampTimeY = dampTimeNormal;
}

static void target_update(float dt) {
    float x = HX, y = HY, num, num2;
    if (tmode == TM_FOLLOW || tmode == TM_LOCK) {
        set_damp_time(dt);
        float dx = x, dy = y;
        if (tmode == TM_LOCK) { dx = clampf(dx, txLockMin, txLockMax); dy = clampf(dy, tyLockMin, tyLockMax); }
        float nx = smooth_damp(tX, dx, &tvx, dampTimeX, dt);
        if (!fallStick && fallCatcher <= 0) tY = smooth_damp(tY, dy, &tvy, dampTimeY, dt);
        tX = nx;
        num = tX; num2 = tY;
        if ((heroPrevX < num && x > num) || (heroPrevX > num && x < num) || (num >= x - snapDistance && num <= x + snapDistance)) stickX = true;
        if ((heroPrevY < num2 && y > num2) || (heroPrevY > num2 && y < num2) || (num2 >= y - snapDistance && num2 <= y + snapDistance)) stickY = true;
        if (stickX) {
            bool ok = tmode == TM_FOLLOW || (x >= txLockMin && x <= txLockMax) || (x <= txLockMax && x >= num) || (x >= txLockMin && x <= num);
            if (ok) { tX = x; num = x; }
        }
        if (stickY) {
            bool ok = tmode == TM_FOLLOW || (y >= tyLockMin && y <= tyLockMax) || (y <= tyLockMax && y >= num2) || (y >= tyLockMin && y <= num2);
            if (ok) { tY = y; num2 = y; }
        }
    }
    num = tX;
    if (P.c.facingRight) { if (xOffset < xLookAhead) xOffset += dt * 6; }
    else if (xOffset > -xLookAhead) xOffset -= dt * 6;
    xOffset = clampf(xOffset, -xLookAhead, xLookAhead);
    if (tmode == TM_LOCK) {
        if (x < txLockMin && P.c.facingRight) xOffset = x - num + 1;
        if (x > txLockMax && !P.c.facingRight) xOffset = x - num - 1;
        if (num + xOffset > txLockMax) xOffset = txLockMax - num;
        if (num + xOffset < txLockMin) xOffset = txLockMin - num;
    }
    xOffset = clampf(xOffset, -xLookAhead, xLookAhead);
    if (P.c.dashing && (P.vx > 5 || P.vx < -5)) {
        dashOffset = P.c.facingRight ? dashLookAhead : -dashLookAhead;
        if (tmode == TM_LOCK && (num + dashOffset > txLockMax || num + dashOffset < txLockMin || x > txLockMax || x < txLockMin)) dashOffset = 0;
    } else dashOffset = 0;
    heroPrevX = x; heroPrevY = y;
    if (!P.c.falling) { fallCatcher = 0; fallStick = false; }
    if (tmode == TM_FOLLOW || tmode == TM_LOCK) {
        if (P.c.falling && camY > y + 0.1f && !fallStick && !P.c.transitioning && (camY - 0.1f >= tyLockMin || tmode != TM_LOCK)) {
            camY -= fallCatcher * dt;
            if (tmode == TM_LOCK && camY < tyLockMin) camY = tyLockMin;
            if (camY < HALF_Y) camY = HALF_Y;
            if (fallCatcher < 25) fallCatcher += 80 * dt;
            if (camY < y + 0.1f) fallStick = true;
            tY = camY;
        }
        if (fallStick) {
            fallCatcher = 0;
            if (y + 0.1f >= tyLockMin || tmode != TM_LOCK) { camY = y + 0.1f; tY = camY; }
            if (tmode == TM_LOCK && camY < tyLockMin) camY = tyLockMin;
            if (camY < HALF_Y) camY = HALF_Y;
        }
    }
}

static void controller_late(float dt) {
    if (mode != CM_FROZEN) {
        if (P.c.lookingUp) lookOffset = HY - tY + 6;
        else if (P.c.lookingDown) lookOffset = HY - tY - 6;
        else lookOffset = 0;
        float destX = tX + xOffset + dashOffset, destY = tY + lookOffset;
        if (mode == CM_LOCKED && curLock >= 0) {
            Lock *l = &locks[curLock];
            if (lookOffset > 0 && l->noUp && destY > l->ymax) destY = camY > l->ymax ? destY - lookOffset : l->ymax;
            if (lookOffset < 0 && l->noDown && destY < l->ymin) destY = camY < l->ymin ? destY - lookOffset : l->ymin;
        }
        keep_scene(&destX, &destY);
        camX = smooth_damp(camX, destX, &cvx, dampTimeC, dt);
        camY = smooth_damp(camY, destY, &cvy, dampTimeC, dt);
    }
    keep_scene(&camX, &camY);
    if (startLockedTimer > 0) startLockedTimer -= dt;
}

void cam_update(float dt) {
    float x0 = HX - HALF_W, x1 = HX + HALF_W, y0 = HY + COL_OFF_Y - HALF_H, y1 = HY + COL_OFF_Y + HALF_H;
    update_zones(x0, y0, x1, y1);
    target_update(dt);
    controller_late(dt);
}

void cam_freeze(bool frozen) { mode = frozen ? CM_FROZEN : (curLock >= 0 ? CM_LOCKED : CM_FOLLOWING); }

// CameraController.PositionToHero (scene entry): lock zones the Knight starts in apply instantly
void cam_position_to_hero(void) {
    tX = HX; tY = HY; tvx = tvy = cvx = cvy = 0;
    heroPrevX = HX; heroPrevY = HY;
    startLockedTimer = 0.5f;
    float x0 = HX - HALF_W, x1 = HX + HALF_W, y0 = HY + COL_OFF_Y - HALF_H, y1 = HY + COL_OFF_Y + HALF_H;
    update_zones(x0, y0, x1, y1);
    xOffset = P.c.facingRight ? 1.0f : -1.0f;
    if (tmode == TM_LOCK) {
        if (HX < txLockMin && P.c.facingRight) xOffset = HX - tX + 1;
        if (HX > txLockMax && !P.c.facingRight) xOffset = HX - tX - 1;
        if (tX + xOffset > txLockMax) xOffset = txLockMax - tX;
        if (tX + xOffset < txLockMin) xOffset = txLockMin - tX;
        tX = clampf(tX, txLockMin, txLockMax); tY = clampf(tY, tyLockMin, tyLockMax);
    }
    xOffset = clampf(xOffset, -xLookAhead, xLookAhead);
    float nx = tX, ny = tY; keep_scene(&nx, &ny);
    if (curLock >= 0) { camX = clampf(nx + xOffset, xLockMin, xLockMax); camY = clampf(ny, yLockMin, yLockMax); }
    else { camX = nx + (P.c.facingRight ? 1.0f : -1.0f); camY = ny; }
    keep_scene(&camX, &camY);
}
