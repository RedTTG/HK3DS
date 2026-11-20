// The Knight. A port of HeroController / HeroAnimationController / NailSlash from the game's decompiled
// Assembly-CSharp. Function names follow the original so the two can be compared side by side.
// Differences: Box2D is replaced by box moves on the room's collision grid (world.h); coroutines are small timers;
// audio, particles and vibration are reduced to the sounds the demake has.
#include "hero.h"
bool g_testGod;     // room-cycle test runs (game.c)
#include "charms.h"
#include "uo.h"
#include "world.h"
#include "audio.h"
#include "game.h"
#include "pd.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

Player P;

/* ---------------- HeroController constants (Knight prefab) ---------------- */
#define RUN_SPEED            8.3f
#define WALK_SPEED           6.0f
#define JUMP_SPEED           16.65f
#define JUMP_STEPS           9
#define JUMP_STEPS_MIN       4
#define DOUBLE_JUMP_STEPS    9
#define WJLOCK_STEPS_SHORT   5
#define WJLOCK_STEPS_LONG    10
#define WJ_KICKOFF_SPEED     16.0f
#define WALL_STICKY_STEPS    3
#define DASH_SPEED           20.0f
#define DASH_SPEED_SHARP     28.0f
#define SHADOW_DASH_COOLDOWN 1.5f
#define DASH_TIME            0.25f
#define DASH_QUEUE_STEPS     10
#define DASH_COOLDOWN        0.6f
#define WALLSLIDE_SPEED      (-8.0f)
#define WALLSLIDE_DECEL      0.0f
#define SPEED_TO_ENTER_SCENE_HOR  6.0f
#define SPEED_TO_ENTER_SCENE_UP   9.4f
#define SPEED_TO_ENTER_SCENE_DOWN (-12.0f)
#define TIME_TO_ENTER_SCENE_BOT   0.1f
#define DEFAULT_GRAVITY      0.79f
#define PHYS_GRAVITY         60.0f          // Physics2D gravity magnitude the game runs with
#define ATTACK_DURATION      0.35f
#define ALT_ATTACK_RESET     0.5f
#define ATTACK_RECOVERY_TIME 0.1f
#define ATTACK_COOLDOWN_TIME 0.41f
#define BOUNCE_TIME          0.25f
#define BOUNCE_VELOCITY      12.0f
#define RECOIL_HOR_VELOCITY  3.75f
#define RECOIL_HOR_VELOCITY_LONG 16.0f
#define SHROOM_BOUNCE_VELOCITY 25.0f        // Knight prefab: a down-slash off a Fungal Wastes bounce shroom
#define RECOIL_HOR_STEPS     8
#define RECOIL_DOWN_VELOCITY 0.0f
#define BIG_FALL_TIME        1.1f
#define HARD_LANDING_TIME    0.8f
#define DOWN_DASH_TIME       0.0f
#define MAX_FALL_VELOCITY    20.0f
#define RECOIL_DURATION      0.2f
#define RECOIL_VELOCITY      15.0f
#define DAMAGE_FREEZE_DOWN   0.001f
#define DAMAGE_FREEZE_WAIT   0.25f
#define DAMAGE_FREEZE_UP     0.05f
#define INVUL_TIME           1.3f
#define JUMP_QUEUE_STEPS     2
#define JUMP_RELEASE_QUEUE_STEPS 2
#define DOUBLE_JUMP_QUEUE_STEPS 10
#define ATTACK_QUEUE_STEPS   5
#define LOOK_DELAY           0.85f
#define LOOK_ANIM_DELAY      0.25f
#define HAZARD_DEATH_CHECK_TIME 3.0f
#define FLOATING_CHECK_TIME  0.18f
#define BUMP_VELOCITY        4.0f
#define BUMP_VELOCITY_DASH   5.0f
#define LANDING_BUFFER_STEPS 5
#define LEDGE_BUFFER_STEPS   2
#define HEAD_BUMP_STEPS      3
#define EPS                  1.401298e-45f

static CState *C = &P.c;

/* ---------------- collider helpers ---------------- */
static float bminx(void) { return P.x - HALF_W; }
static float bmaxx(void) { return P.x + HALF_W; }
static float bminy(void) { return P.y + COL_OFF_Y - HALF_H; }
static float bmaxy(void) { return P.y + COL_OFF_Y + HALF_H; }
static float bcy(void) { return P.y + COL_OFF_Y; }

static bool ray(float x, float y, float dx, float dy, float d) { float h; return w_raycast(x, y, dx, dy, d, &h); }
static float fsSoftLandT;            // footsteps (hero_footsteps_update): the soft landing sound still playing

/* ---------------- animation ---------------- */
static void anim_play_from(AnimPlayer *a, const char *name, int frame) {
    anim_play(a, &A.knight, name, true);
    if (a->anim >= 0) { const Anim *an = &A.knight.anims[a->anim]; if (an->fps > 0) a->t = frame / an->fps + 1e-4f; a->frame = frame < an->count ? frame : an->count - 1; }
}
static bool playing(const char *n) { return anim_is(&P.anim, n) && !P.anim.done; }
static void Play(const char *n) { if (!anim_is(&P.anim, n)) anim_play(&P.anim, &A.knight, n, true); }   // tk2d Play: no restart if already current
static void PlayFromFrame(const char *n, int f) { anim_play_from(&P.anim, n, f); }

/* ---------------- forward decls ---------------- */
static void SetState(ActorState s);
static void PlayIdle(void);
static void BackOnGround(void);
static void CancelJump(void);
static void CancelDoubleJump(void);
static void CancelDash(void);
static void CancelWallsliding(void);
static void CancelAttack(void);
static void CancelBounce(void);
static void CancelRecoilHorizontal(void);
static void CancelDamageRecoil(void);
static void ResetMotion(void);
static void ResetLook(void);
static void ResetAttacks(void);
static void FinishedEnteringScene(bool setHazardMarker);

static void AffectedByGravity(bool applies) {
    if (P.gravityScale > EPS && !applies) { P.prevGravityScale = P.gravityScale; P.gravityScale = 0; }
    else if (P.gravityScale <= EPS && applies) { P.gravityScale = P.prevGravityScale; P.prevGravityScale = 0; }
}

static void FaceRight(void) { C->facingRight = true; P.facing = 1; }
static void FaceLeft(void) { C->facingRight = false; P.facing = -1; }
static void FlipSprite(void) { C->facingRight = !C->facingRight; P.facing = C->facingRight ? 1 : -1; }
void hero_face(bool right) { if (right) FaceRight(); else FaceLeft(); }

/* ---------------- raycast checks ---------------- */
bool hero_check_touching_ground(void) {
    float d = HALF_H + 0.16f;
    return ray(bminx(), bcy(), 0, -1, d) || ray(P.x, bcy(), 0, -1, d) || ray(bmaxx(), bcy(), 0, -1, d);
}
static bool CheckTouchingGround(void) { return hero_check_touching_ground(); }

static bool CheckStillTouchingWall(CollSide side) {
    float d = 0.1f;
    if (side == CS_LEFT) return ray(bminx(), bcy(), -1, 0, d) || ray(bminx(), bminy(), -1, 0, d);
    return ray(bmaxx(), bcy(), 1, 0, d) || ray(bmaxx(), bminy(), 1, 0, d);
}

static bool CheckNearRoof(void) {
    return ray(bminx(), bmaxy(), -0.4472136f, 0.8944272f, 2) || ray(bmaxx(), bmaxy(), 0.4472136f, 0.8944272f, 2) ||
           ray(P.x + HALF_W / 2, bmaxy(), 0, 1, 1) || ray(P.x - HALF_W / 2, bmaxy(), 0, 1, 1);
}

static bool CheckForBump(CollSide side) {
    float num = 0.025f, num2 = 0.2f, len = 0.32f + num2, h1, h2;
    float y1 = bminy() + 0.2f, y2 = bminy() - num;
    float x = side == CS_RIGHT ? bmaxx() - num2 : bminx() + num2, dir = side == CS_RIGHT ? 1 : -1;
    bool lowHit = w_raycast(x, y2, dir, 0, len, &h2), highHit = w_raycast(x, y1, dir, 0, len, &h1);
    if (lowHit && !highHit) {
        float px = x + dir * h2;
        float hd3, hd4;
        bool r3 = w_raycast(px + dir * 0.1f, y2 + 1, 0, -1, 1.5f, &hd3);
        bool r4 = w_raycast(px - dir * 0.1f, y2 + 1, 0, -1, 1.5f, &hd4);
        if (r3) {
            if (!r4) return true;
            if ((y2 + 1 - hd3) - (y2 + 1 - hd4) > 0) return true;
        }
    }
    return false;
}

static float FindGroundPointY(float x, float y) {
    float h;
    if (!w_raycast(x, y, 0, -1, 50, &h)) return y;
    return (y - h) + HALF_H - COL_OFF_Y + 0.01f;
}

void hero_find_ground_point(float *x, float *y) { *y = FindGroundPointY(*x, *y + 0.5f); }   // HeroController.FindGroundPoint from where he stands

/* ---------------- Can* ---------------- */
static bool CanInput(void) { return P.acceptingInput; }
static bool CanJump(void) {
    if (P.state != AS_NO_INPUT && P.state != AS_HARD_LANDING && P.state != AS_DASH_LANDING && !C->wallSliding && !C->dashing &&
        !C->backDashing && !C->jumping && !C->bouncing && !C->shroomBouncing) {
        if (C->onGround) return true;
        if (P.ledgeBufferSteps > 0 && !C->dead && !C->hazardDeath && !P.controlRelinquished && P.headBumpSteps <= 0 && !CheckNearRoof()) { P.ledgeBufferSteps = 0; return true; }
    }
    return false;
}
static bool CanDoubleJump(void) {
    return P.hasDoubleJump && !P.controlRelinquished && !P.doubleJumped && P.state != AS_NO_INPUT && P.state != AS_HARD_LANDING &&
           P.state != AS_DASH_LANDING && !C->dashing && !C->wallSliding && !C->backDashing && !C->bouncing && !C->shroomBouncing && !C->onGround;
}
static bool CanDash(void) {
    return P.state != AS_NO_INPUT && P.state != AS_HARD_LANDING && P.state != AS_DASH_LANDING && P.dashCooldownTimer <= 0 &&
           !C->dashing && !C->backDashing && (!C->attacking || !(P.attack_time < ATTACK_RECOVERY_TIME)) && !C->preventDash &&
           (C->onGround || !P.airDashed || C->wallSliding) && !C->hazardDeath && P.hasDash;
}
static bool CanAttack(void) {
    return P.attack_cooldown <= 0 && !C->attacking && !C->dashing && !C->dead && !C->hazardDeath && !C->hazardRespawning &&
           !P.controlRelinquished && P.state != AS_NO_INPUT && P.state != AS_HARD_LANDING && P.state != AS_DASH_LANDING;
}
static bool CanWallSlide(void) {
    return !C->touchingNonSlider && !C->dashing && P.hasWalljump && !C->onGround && !C->recoiling && !P.controlRelinquished &&
           !C->transitioning && (C->falling || C->wallSliding) && !C->doubleJumping && CanInput();
}
static bool CanTakeDamage(void) {
    return P.trans == TS_WAITING_TO_TRANSITION && !C->invulnerable && !C->recoiling && !C->dead && !C->hazardDeath && P.parryInvulnTimer <= 0;
}
static bool CanWallJump(void) {
    if (!P.hasWalljump || C->touchingNonSlider) return false;
    if (C->wallSliding) return true;
    return C->touchingWall && !C->onGround;
}
static bool ShouldHardLand(void) { return C->willHardLand && P.state != AS_HARD_LANDING; }

/* ---------------- state ---------------- */
static void animUpdateState(ActorState s) {   // HeroAnimationController.UpdateState
    if (s == P.animState) return;
    if (P.animState == AS_AIRBORNE && s == AS_IDLE && !P.playLanding) P.playLanding = true;
    if (P.animState == AS_RUNNING && s == AS_IDLE && !P.playRunToIdle && !C->inWalkZone && !C->attacking) P.playRunToIdle = true;
    P.animState = s;
}
static void SetState(ActorState s) {
    if (P.state != AS_NO_INPUT || (!C->dead)) {
        if (s == AS_GROUNDED) s = fabsf(P.move_input) > EPS ? AS_RUNNING : AS_IDLE;
        else if (s == AS_PREVIOUS) s = P.prevState;
        if (s != P.state) { P.prevState = P.state; P.state = s; animUpdateState(s); }
    }
}

/* ---------------- cancels / resets ---------------- */
static void CancelJump(void) { C->jumping = false; P.jumpReleaseQueuing = false; P.jump_steps = 0; }
static void CancelDoubleJump(void) { C->doubleJumping = false; P.doubleJump_steps = 0; }
static void CancelDash(void) { C->shadowDashing = false; C->dashing = false; P.dash_timer = 0; AffectedByGravity(true); hero_sharp_shadow(false); }
static void CancelWallsliding(void) { C->wallSliding = false; P.wallSlidingL = P.wallSlidingR = false; P.touchingWallL = P.touchingWallR = false; }
static void CancelBackDash(void) { C->backDashing = false; }
static void ResetAttacks(void) { C->nailCharging = false; C->attacking = false; C->upAttacking = false; C->downAttacking = false; P.attack_time = 0; }
static void ResetAttacksDash(void) { C->attacking = false; C->upAttacking = false; C->downAttacking = false; P.attack_time = 0; }
static void SlashCancel(void) { P.slashing = false; P.slashPoly = false; }
static void CancelAttack(void) { if (C->attacking) { SlashCancel(); ResetAttacks(); } }
static void CancelDownAttack(void) { if (C->downAttacking) { SlashCancel(); ResetAttacks(); } }
static void CancelBounce(void) { C->bouncing = false; C->shroomBouncing = false; P.bounceTimer = 0; }
static void CancelRecoilHorizontal(void) { C->recoilingLeft = C->recoilingRight = false; P.recoilSteps = 0; }
static void CancelDamageRecoil(void) { C->recoiling = false; P.recoilTimer = 0; ResetMotion(); AffectedByGravity(true); }
static void ResetMotion(void) {
    CancelJump(); CancelDoubleJump(); CancelDash(); CancelBackDash(); CancelBounce(); CancelRecoilHorizontal(); CancelWallsliding();
    P.vx = P.vy = 0; P.transVx = P.transVy = 0; P.wallLocked = false;
}
static void ResetLook(void) { C->lookingUp = C->lookingDown = C->lookingUpAnim = C->lookingDownAnim = false; P.lookDelayTimer = 0; }
static void ResetInput(void) { P.move_input = 0; P.vertical_input = 0; }
static void ResetHardLandingTimer(void) { P.hardLandingTimer = 0; P.fallTimer = 0; C->willHardLand = false; }

static void BackOnGround(void) {
    if (P.landingBufferSteps <= 0) {
        P.landingBufferSteps = LANDING_BUFFER_STEPS;
        if (!C->onGround && !P.hardLanded) { sfx_play("hero_land_soft", 0.8f); fsSoftLandT = 0.18f; }
    }
    C->falling = false; P.fallTimer = 0; P.dashLandingTimer = 0; C->willHardLand = false;
    P.hardLandingTimer = 0; P.hardLanded = false; P.jump_steps = 0;
    if (C->doubleJumping) { C->jumping = true; P.jumpQueueSteps = 0; P.jumped_steps = 0; }
    SetState(AS_GROUNDED);
    C->onGround = true;
    P.airDashed = false; P.doubleJumped = false;
}

static void DoHardLanding(void) {
    AffectedByGravity(true); ResetInput(); SetState(AS_HARD_LANDING); CancelAttack(); P.hardLanded = true;
    sfx_play("hero_land_hard", 1.0f); game_shake(0.15f);
}

static void JumpReleased(void) {
    if (P.vy > 0 && P.jumped_steps >= JUMP_STEPS_MIN && !C->shroomBouncing) { P.vy = 0; CancelJump(); }
    P.jumpQueuing = false; P.doubleJumpQueuing = false;
}

static void FinishedDashing(void) {
    CancelDash(); AffectedByGravity(true); P.playDashToIdle = true;
    if (C->touchingWall && !C->onGround && P.hasWalljump && (P.touchingWallL || P.touchingWallR)) {
        C->wallSliding = true; C->willHardLand = false;
        if (P.touchingWallL) P.wallSlidingL = true;
        if (P.touchingWallR) P.wallSlidingR = true;
        if (P.dashingDown) FlipSprite();
    }
}

/* ---------------- actions ---------------- */
static void HeroJump(void) {
    sfx_play("hero_jump", 0.8f);
    ResetLook(); C->recoiling = false; C->jumping = true; P.jumpQueueSteps = 0; P.jumped_steps = 0; P.doubleJumpQueuing = false;
}
static void DoWallJump(void) {
    sfx_play("hero_wall_jump", 0.9f);
    if (P.touchingWallL) { FaceRight(); P.wallJumpedR = true; P.wallJumpedL = false; }
    else if (P.touchingWallR) { FaceLeft(); P.wallJumpedR = false; P.wallJumpedL = true; }
    CancelWallsliding();
    C->touchingWall = false; P.touchingWallL = P.touchingWallR = false;
    P.airDashed = false; P.doubleJumped = false;
    P.currentWalljumpSpeed = WJ_KICKOFF_SPEED;
    P.walljumpSpeedDecel = (WJ_KICKOFF_SPEED - RUN_SPEED) / (float)WJLOCK_STEPS_LONG;
    C->jumping = true; P.wallLockSteps = 0; P.wallLocked = true; P.jumpQueueSteps = 0; P.jumped_steps = 0;
}
static void DoDoubleJump(void) {
    sfx_play("hero_double_jump", 0.9f);
    ResetLook(); C->jumping = false; C->doubleJumping = true; P.doubleJump_steps = 0; P.doubleJumped = true;
}

static void StartSlash(void) {       // NailSlash.StartSlash
    static const char *swords[] = { "sword_1", "sword_2", "sword_3", "sword_4", "sword_5" };
    sfx_play(swords[rand() % 5], 0.85f);
    // the Knight's Attacks children, each a NailSlash with its own animName (WallSlash plays SlashEffect, mirrored)
    static const char *names[5] = { "SlashEffect", "SlashEffectAlt", "UpSlashEffect", "DownSlashEffect", "SlashEffect" };
    if (P.attackDir == 1) P.slashKind = 2;
    else if (P.attackDir == 2) P.slashKind = 3;
    else if (C->wallSliding) P.slashKind = 4;
    else P.slashKind = C->altAttack ? 0 : 1;   // altAttack was already toggled for the next swing
    // Mark of Pride plays the " M" slash, Fury of the Fallen (at 1 mask) the red " F" one over either
    char fx[40]; snprintf(fx, sizeof fx, "%s%s", names[P.slashKind],
                          charm_act(6) && P.health == 1 ? " F" : charm_act(13) ? " M" : "");
    anim_play(&P.slashFx, &A.knight, fx, true);
    if (P.slashFx.anim < 0 || !anim_is(&P.slashFx, fx)) anim_play(&P.slashFx, &A.knight, names[P.slashKind], true);
#ifdef __HOST__
    if (getenv("HK_SLASHFX")) { float ox, oy, sx, sy; hero_slash_xf(&ox, &oy, &sx, &sy);
        fprintf(stderr, "[slashfx] %s at %.2f,%.2f scale %.3f,%.3f\n", P.slashFx.anim >= 0 ? fx : "-", ox, oy, sx, sy); }
#endif
    P.slashStep = 0; P.slashPolyCount = 0; P.slashPoly = false; P.slashAnimDone = false; P.slashing = true;
    P.swing++;
}

static void Attack(int dir) {
    if (P.timeSinceLevelLoad - P.altAttackTime > ALT_ATTACK_RESET) C->altAttack = false;
    C->attacking = true;
    P.attackDuration = ATTACK_DURATION;
    if (C->wallSliding) {
        P.attackDir = 0; P.slashAngle = C->facingRight ? 180 : 0;
    } else {
        P.attackDir = dir;
        if (dir == 0) { C->altAttack = !C->altAttack; P.slashAngle = C->facingRight ? 0 : 180; }
        else if (dir == 1) { C->upAttacking = true; P.slashAngle = 90; }
        else { C->downAttacking = true; P.slashAngle = 270; }
    }
    P.altAttackTime = P.timeSinceLevelLoad;
    StartSlash();
}

static void DoAttack(void) {
    ResetLook(); C->recoiling = false;
    P.attack_cooldown = charm_attack_cooldown(ATTACK_COOLDOWN_TIME);
    if (P.vertical_input > EPS) Attack(1);
    else if (P.vertical_input < -EPS) { if (P.state != AS_IDLE && P.state != AS_RUNNING) Attack(2); else Attack(0); }
    else Attack(0);
}

static void HeroDash(void) {
    if (!C->onGround) P.airDashed = true;
    ResetAttacksDash(); CancelBounce();
    sfx_play("hero_dash", 1.0f);
    ResetLook(); C->recoiling = false;
    if (C->wallSliding) FlipSprite();
    else if (P.in.rightH) FaceRight();
    else if (P.in.leftH) FaceLeft();
    C->dashing = true; P.dashQueueSteps = 0;
    P.dashingDown = P.in.downH && !C->onGround && charm_act(31) && !P.in.leftH && !P.in.rightH;   // Dashmaster: dash down
    P.dashCooldownTimer = charm_dash_cooldown(DASH_COOLDOWN);
    if (pd_bool("hasShadowDash") && P.shadowDashTimer <= 0) {
        P.shadowDashTimer = SHADOW_DASH_COOLDOWN;
        C->shadowDashing = true;
        sfx_play(charm_act(16) ? "hero_shade_dash_charm" : "hero_shade_dash", 1.0f);
        hero_sharp_shadow(charm_act(16));
    }
}

/* ---------------- recoil / bounce (called by the nail slash and enemies) ---------------- */
void hero_recoil_left(void);
void hero_recoil_right(void);
void hero_recoil_down(void);
static void RecoilLeft(void) {
    if (!C->recoilingLeft && !C->recoilingRight && !P.controlRelinquished) {
        CancelDash(); P.recoilSteps = 0; C->recoilingLeft = true; C->recoilingRight = false; P.recoilLarge = false; P.vx = -RECOIL_HOR_VELOCITY;
    }
}
static void RecoilRight(void) {
    if (!C->recoilingLeft && !C->recoilingRight && !P.controlRelinquished) {
        CancelDash(); P.recoilSteps = 0; C->recoilingRight = true; C->recoilingLeft = false; P.recoilLarge = false; P.vx = RECOIL_HOR_VELOCITY;
    }
}
static void RecoilDown(void) {
    CancelJump();
    if (P.vy > RECOIL_DOWN_VELOCITY && !P.controlRelinquished) P.vy = RECOIL_DOWN_VELOCITY;
}
void hero_conveyor(int kind, bool on, float speed, bool vertical) {
    if (kind == 2) { if (on) P.conveyorSpeed = speed; return; }
    if (on) {
        if (vertical) { P.conveyorSpeedV = speed; C->onConveyorV = true; }
        else { P.conveyorSpeed = speed; if (kind == 0) C->onConveyor = true; else C->inConveyorZone = true; }
    } else {
        P.conveyorSpeedV = 0;
        if (P.convGravityOff) { if (!C->superDashing) AffectedByGravity(true); P.convGravityOff = false; }
        if (kind == 0) C->onConveyor = false; else C->inConveyorZone = false;
        C->onConveyorV = false;
    }
}
static void conveyor_late(void) {         // ConveyorMovementHero.LateUpdate: vertical belts carry a wall-clinging Knight
    if (!C->onConveyorV) return;
    if (P.conveyorSpeedV != 0 && (C->wallSliding || C->superDashOnWall)) {
        if (C->superDashOnWall) P.vx = P.vy = 0;
        P.vy = P.conveyorSpeedV;
        if (!P.convGravityOff) { AffectedByGravity(false); P.convGravityOff = true; }
    } else if (P.convGravityOff && !C->superDashing) { AffectedByGravity(true); P.convGravityOff = false; }
}
void hero_set_sliding(bool l, bool r, bool on) { if (l) C->slidingLeft = on; if (r) C->slidingRight = on; }
void hero_shroom_recoil(void) { hero_bounce(); if (rand() & 1) RecoilLeft(); else RecoilRight(); }
static void RecoilLong(bool left) {      // RecoilLeftLong / RecoilRightLong: a side slash into a bounce shroom
    if (!C->recoilingLeft && !C->recoilingRight && !P.controlRelinquished) {
        CancelDash(); ResetAttacks(); P.recoilSteps = 0;
        C->recoilingLeft = left; C->recoilingRight = !left; P.recoilLarge = true;
        P.vx = left ? -RECOIL_HOR_VELOCITY_LONG : RECOIL_HOR_VELOCITY_LONG;
    }
}
void hero_slash_hit_shroom(void) {       // NailSlash.OnTriggerEnter2D on a BounceShroom
    if (P.slashAngle == 0) RecoilLong(true);
    else if (P.slashAngle == 180) RecoilLong(false);
    else if (P.slashAngle == 90) RecoilDown();
    else {                                // ShroomBounce: a big bounce, double jump and air dash back
        P.doubleJumped = false; P.airDashed = false;
        C->bouncing = false; C->shroomBouncing = true;
        P.vy = SHROOM_BOUNCE_VELOCITY;
    }
}
void hero_bounce(void) {
    if (!C->bouncing && !C->shroomBouncing && !P.controlRelinquished) { P.doubleJumped = false; P.airDashed = false; C->bouncing = true; }
}
void hero_slash_hit_enemy(void) {       // NailSlash.OnTriggerEnter2D, layer 11
    if (P.slashAngle == 0) { if (!charm_steady_body()) RecoilLeft(); }      // RecoilLeft/Right skip with equippedCharm_14
    else if (P.slashAngle == 180) { if (!charm_steady_body()) RecoilRight(); }
    else if (P.slashAngle == 90) RecoilDown();
    else hero_bounce();
}
void hero_slash_hit_bouncer(void) {     // spikes' Tink / hero-attack-layer objects: only the down slash bounces
    if (P.slashAngle == 270) hero_bounce();
}
bool hero_slash_active(void) { return P.slashing && P.slashPoly; }
void hero_nail_box(float *x0, float *y0, float *x1, float *y1) {
    // bounding boxes of the game's slash polygons (Knight/Attacks/Slash, UpSlash, DownSlash, WallSlash), trimmed a little
    if (P.slashAngle == 90) { *x0 = P.x - 1.35f; *x1 = P.x + 1.35f; *y0 = P.y - 0.5f; *y1 = P.y + 2.6f; }
    else if (P.slashAngle == 270) { *x0 = P.x - 1.4f; *x1 = P.x + 1.4f; *y0 = P.y - 3.5f; *y1 = P.y - 0.7f; }
    else if (P.slashAngle == 0) { *x0 = P.x; *x1 = P.x + 3.1f; *y0 = P.y - 1.4f; *y1 = P.y + 1.1f; }
    else { *x0 = P.x - 3.1f; *x1 = P.x; *y0 = P.y - 1.4f; *y1 = P.y + 1.1f; }
    float s = charm_nail_scale();               // Longnail / Mark of Pride scale the slash object, about its own position
    if (s != 1) {
        float ox, oy, sx, sy; hero_slash_xf(&ox, &oy, &sx, &sy);
        float cx = P.x + ox, cy = P.y + oy;
        *x0 = cx + (*x0 - cx) * s; *x1 = cx + (*x1 - cx) * s; *y0 = cy + (*y0 - cy) * s; *y1 = cy + (*y1 - cy) * s;
    }
}
// where the slash effect is drawn: the NailSlash object's local position on the Knight (the Knight faces left at scale
// 1, so it mirrors when facing right) and its NailSlash.scale, times Longnail / Mark of Pride (NailSlash.StartSlash)
void hero_slash_xf(float *ox, float *oy, float *sx, float *sy) {
    static const float xf[5][4] = {
        { -0.01f, -0.41f, 1.601078f, 1.645244f },    // Slash
        { 0.08f, -0.436f, 1.25697f, 1.422434f },     // AltSlash
        { 0.0f, 0.69f, 1.15f, 1.4f },                // UpSlash
        { 0.16f, -1.59f, 1.125f, 1.28002f },         // DownSlash
        { 0.04f, -0.41f, -1.62f, 1.645244f },        // WallSlash (mirrored: it swings away from the wall)
    };
    int k = P.slashKind < 0 || P.slashKind > 4 ? 0 : P.slashKind;
    float flip = C->facingRight ? -1 : 1, m = charm_nail_scale();
    *ox = xf[k][0] * flip; *oy = xf[k][1];
    *sx = xf[k][2] * flip * m; *sy = xf[k][3] * m;
}

/* ---------------- damage ---------------- */
static void StartInvulnerable(float duration) {
    if (!P.invulRoutine) { C->invulnerable = true; P.invulFreeze = DAMAGE_FREEZE_DOWN; P.invulDuration = duration; P.invulRoutine = true; }
    else if (P.invulFreeze + P.invulDuration < duration + DAMAGE_FREEZE_DOWN) {
        if (P.invulFreeze > 0) P.invulFreeze = DAMAGE_FREEZE_DOWN; else duration += DAMAGE_FREEZE_DOWN;
        P.invulDuration = duration;
    }
}
static void DieFromHazard(void) {
    if (C->hazardDeath) return;
    SetState(AS_NO_INPUT); C->hazardDeath = true; ResetMotion(); ResetHardLandingTimer(); AffectedByGravity(false);
    anim_play(&P.anim, &A.knight, "Spike Death", true);
    game_hero_hazard_death();
}
static void StartRecoil(int side) {   // side: CS_LEFT = hit from the left
    if (C->recoiling) return;
    pd_set_bool("disablePause", true);      // HeroController.StartRecoil: no pausing during the hit freeze
    ResetMotion(); AffectedByGravity(false);
    if (side == CS_LEFT) { P.recoilVx = RECOIL_VELOCITY; P.recoilVy = RECOIL_VELOCITY * 0.5f; if (C->facingRight) FlipSprite(); }
    else if (side == CS_RIGHT) { P.recoilVx = -RECOIL_VELOCITY; P.recoilVy = RECOIL_VELOCITY * 0.5f; if (!C->facingRight) FlipSprite(); }
    else { P.recoilVx = P.recoilVy = 0; }
    SetState(AS_NO_INPUT);
    C->recoilFrozen = true;
    StartInvulnerable(charm_invul_time(INVUL_TIME));
    P.freezeT = DAMAGE_FREEZE_DOWN + DAMAGE_FREEZE_WAIT + DAMAGE_FREEZE_UP;   // GameManager.FreezeMoment
    game_freeze(P.freezeT);
}
void hero_take_damage(float fromX, int amount, int hazardType) {
#ifdef __HOST__
    if (getenv("HK_GOD") || g_testGod) amount = 0;      /* test: the Knight takes no damage at all */
#endif
    if (amount <= 0) return;
    int side = (fromX > P.x) ? CS_RIGHT : CS_LEFT;
    if (CanTakeDamage()) {
        if ((P.damageMode == 1 && hazardType == 1) || (C->shadowDashing && hazardType == 1) || (P.parryInvulnTimer > 0 && hazardType == 1)) return;
        CancelAttack();
        if (C->wallSliding) C->wallSliding = false;
        if (C->touchingWall) C->touchingWall = false;
        if (C->recoilingLeft || C->recoilingRight) CancelRecoilHorizontal();
        if (C->bouncing || C->shroomBouncing) { CancelBounce(); P.vy = 0; }
        sfx_play("hero_damage", 1.0f);
        amount = game_absorb_blue(charm_damage_taken(amount));
        P.health -= amount; if (P.health < 0) P.health = 0;
#ifdef __HOST__
        if (getenv("HK_TANK") && P.health < 1) P.health = P.maxHealth;     /* test: hits and recoils happen, the Knight never dies */
#endif
        P.focusing = false;
        if (charm_damage_soul()) { P.soul += charm_damage_soul(); if (P.soul > 99) P.soul = 99; }
        uo_broadcast("HERO DAMAGED");
        if (P.health == 0) { C->dead = true; SetState(AS_NO_INPUT); ResetMotion(); game_hero_dead(); return; }
        if (hazardType == 2 || hazardType == 3 || hazardType == 5) DieFromHazard();
        else StartRecoil(side);
    } else {
        if (!C->invulnerable || C->hazardDeath) return;
        if (hazardType == 2 || hazardType == 3) {       // hazards still hurt through invulnerability
            P.health -= amount; if (P.health < 0) P.health = 0;
            sfx_play("hero_damage", 1.0f);
            if (P.health == 0) { C->dead = true; SetState(AS_NO_INPUT); ResetMotion(); game_hero_dead(); return; }
            DieFromHazard();
        }
    }
}
void hero_hazard_respawn(float x, float y, bool facingRight) {
    SetState(AS_NO_INPUT);
    ResetLook(); C->hazardDeath = false; C->onGround = true; C->hazardRespawning = true;
    ResetMotion(); ResetHardLandingTimer(); ResetAttacks(); ResetInput(); C->recoiling = false;
    P.airDashed = false; P.doubleJumped = false;
    P.x = x; P.y = FindGroundPointY(x, y);
    { float cx = P.x, cy = bcy(); w_unstick_box(&cx, &cy, HALF_W, HALF_H, 2.0f); P.x = cx; P.y = cy - COL_OFF_Y; }
    P.px = P.x; P.py = P.y;
    if (facingRight) FaceRight(); else FaceLeft();
    StartInvulnerable(INVUL_TIME * 2);
    anim_play(&P.anim, &A.knight, "Hazard Respawn", true);
    P.enterPhase = 100; P.enterT = anim_length(&P.anim);     // wait for the clip, then FinishedEnteringScene
}

void hero_relinquish_control(void) {
    if (!P.controlRelinquished) { P.acceptingInput = false; P.controlRelinquished = true; ResetMotion(); ResetInput(); ResetAttacks(); SetState(AS_IDLE); }
}
void hero_stop_anim_control(void) { P.animCtrlStopped = true; }
void hero_start_anim_control(void) {     // HeroAnimationController.StartControl: back to the state's clip
    P.animCtrlStopped = false; P.wasFacingRight = C->facingRight;
    if (C->onGround) PlayIdle(); else Play("Airborne");
}
bool hero_can_input(void) { return P.acceptingInput; }
void hero_ignore_input_without_reset(void) { P.acceptingInput = false; }
void hero_leave_scene(GatePos side) {     // HeroController.LeaveScene: an exit fired; the Knight walks (or flies) out by himself
    P.acceptingInput = false;              // IgnoreInputWithoutReset
    ResetHardLandingTimer();
    SetState(AS_NO_INPUT);
    P.trans = TS_EXITING_SCENE; P.stopWalkingOut = false; P.transVx = P.transVy = 0;
    if (side == GP_TOP) C->onGround = false;     // keeps rising at his current speed (gravity is off while exiting)
    else if (side == GP_RIGHT) P.transVx = RUN_SPEED;
    else if (side == GP_LEFT) P.transVx = -RUN_SPEED;
    else if (side == GP_BOTTOM) C->onGround = false;
    C->transitioning = true;
}
bool hero_get_state(const char *n) {     // HeroController.GetState: a HeroControllerStates field by name
    if (!n) return false;
#define S(name, v) if (!strcmp(n, name)) return v;
    S("onGround", C->onGround) S("attacking", C->attacking) S("upAttacking", C->upAttacking) S("downAttacking", C->downAttacking)
    S("dashing", C->dashing) S("backDashing", C->backDashing) S("jumping", C->jumping) S("doubleJumping", C->doubleJumping)
    S("falling", C->falling) S("wallSliding", C->wallSliding) S("touchingWall", C->touchingWall) S("recoiling", C->recoiling)
    S("dead", C->dead) S("transitioning", C->transitioning) S("facingRight", C->facingRight) S("invulnerable", C->invulnerable)
    S("focusing", P.focusing) S("casting", C->casting) S("lookingUp", C->lookingUp) S("lookingDown", C->lookingDown)
    S("hazardDeath", C->hazardDeath) S("hazardRespawning", C->hazardRespawning) S("isPaused", false) S("shadowDashing", C->shadowDashing)
    S("bouncing", C->bouncing) S("recoilFrozen", C->recoilFrozen) S("nailCharging", C->nailCharging) S("spellQuake", C->spellQuake)
#undef S
    return false;
}
bool hero_can_talk(void) {               // HeroController.CanTalk / CanInspect / CanInteract
    // also never while leaving the room: the game takes input away as an exit fires (HeroController.LeaveScene), and a
    // tablet read or a talk started in that frame was cut off by the room change with pausing still blocked (sweep, Abyss_08)
    extern bool game_leaving_room(void);
    return P.acceptingInput && P.state != AS_NO_INPUT && !P.controlRelinquished && C->onGround && !C->attacking && !C->dashing && !P.sitting && !P.focusing &&
        !C->backDashing && !C->recoiling && !C->recoilFrozen && !C->transitioning && !C->hazardDeath && !C->hazardRespawning &&
        P.trans == TS_WAITING_TO_TRANSITION && !game_leaving_room();
}
void hero_anim_play(const char *clip, bool restart) { if (clip) anim_play(&P.anim, &A.knight, clip, restart); }
void hero_anim_play_from(const char *clip, int frame) {
    if (clip) anim_play_from(&P.anim, clip, frame);
    else if (P.anim.anim >= 0) anim_play_from(&P.anim, A.knight.anims[P.anim.anim].name, frame);
}
bool hero_anim_is(const char *clip) { return clip && anim_is(&P.anim, clip); }
float hero_anim_clip_len(const char *clip) {
    int i = spr_anim(&A.knight, clip); if (i < 0) return 0;
    return A.knight.anims[i].fps > 0 ? A.knight.anims[i].count / A.knight.anims[i].fps : 0;
}
void hero_face_left(void) { FaceLeft(); }
void hero_affected_by_gravity(bool on) { AffectedByGravity(on); }
void hero_set_gravity_scale(float g) { P.gravityScale = g; if (g > EPS) P.prevGravityScale = 0; }
void hk_log(const char *fmt, ...);
// safety net (playtest: the Knight hovering after Vengeful Spirit, only able to move sideways): in the air with control,
// no gravity, and none of the moves that switch it off running (spell, dash, super dash, wall cling, recoil, swimming,
// scene entry) for half a second: gravity comes back, and the log says what state he was in
void hero_float_watchdog(float dt, bool knightFsmsIdle) {
    static float t;
    bool floating = P.gravityScale <= EPS && P.acceptingInput && !P.controlRelinquished && !C->onGround && knightFsmsIdle &&
        !C->dashing && !C->superDashing && !C->superDashOnWall && !C->wallSliding && !C->recoiling && !C->recoilFrozen &&
        !C->swimming && !C->onConveyorV && !C->transitioning && !C->hazardDeath && !C->hazardRespawning && !C->dead &&
        !C->casting && !C->focusing && !C->spellQuake && P.trans == TS_WAITING_TO_TRANSITION && P.enterPhase == 0;
    t = floating ? t + dt : 0;
    if (t > 0.5f) {
        hk_log("watchdog: Knight floating with control (g %.2f prev %.2f state %d vy %.2f): gravity back on", P.gravityScale, P.prevGravityScale, (int)P.state, P.vy);
#ifdef __HOST__
        fprintf(stderr, "[watchdog %d] floating at %.2f,%.2f state %d anim %s\n", uo_frame, P.x, P.y, (int)P.state, P.anim.set && P.anim.anim >= 0 ? P.anim.set->anims[P.anim.anim].name : "-");
#endif
        P.gravityScale = DEFAULT_GRAVITY; P.prevGravityScale = 0; t = 0;
    }
}
#ifdef __HOST__
// test runs: report any spell of floating with control (whatever the Knight's scripts are doing) and any spell of
// pause being blocked while the Knight is free to move; the release watchdog above only covers the known-safe case
void hero_suspect_check(float dt, const char *spellState, bool pauseBlocked) {
    static float tf, tp; static bool rf, rp;
    bool fl = P.gravityScale <= EPS && P.acceptingInput && !P.controlRelinquished && !C->onGround && !C->swimming &&
        !C->superDashing && !C->superDashOnWall && !C->wallSliding && !C->dashing && !C->recoiling && !C->recoilFrozen &&
        !C->transitioning && !C->dead && !C->hazardDeath && !C->hazardRespawning && P.enterPhase == 0;
    tf = fl ? tf + dt : 0;
    if (tf > 1.5f && !rf) { rf = true; fprintf(stderr, "[float-suspect %d] at %.2f,%.2f g %.2f prev %.2f spell %s anim %s state %d\n", uo_frame, P.x, P.y,
        P.gravityScale, P.prevGravityScale, spellState ? spellState : "-", P.anim.set && P.anim.anim >= 0 ? P.anim.set->anims[P.anim.anim].name : "-", (int)P.state); }
    if (!fl) rf = false;
    {   // a spell's state that holds the Knight (velocity every frame, gravity off) still running long after it should end
        static float ts; static bool rs;
        bool sp = spellState && strcmp(spellState, "Inactive") && strcmp(spellState, "Button Down") && strncmp(spellState, "Focus", 5) &&
            strcmp(spellState, "Can Focus?") && strcmp(spellState, "Cancel") && strncmp(spellState, "Quake1 Down", 11) && strncmp(spellState, "Quake2 Down", 11);
        ts = sp ? ts + dt : 0;
        if (ts > 2.5f && !rs) { rs = true; fprintf(stderr, "[spell-stuck %d] at %.2f,%.2f spell %s input %d relinq %d g %.2f anim %s\n", uo_frame, P.x, P.y, spellState,
            P.acceptingInput, P.controlRelinquished, P.gravityScale, P.anim.set && P.anim.anim >= 0 ? P.anim.set->anims[P.anim.anim].name : "-"); }
        if (!sp) rs = false;
    }
    bool pb = pauseBlocked && P.acceptingInput && !P.controlRelinquished && !C->transitioning && !C->dead;
    tp = pb ? tp + dt : 0;
    if (tp > 3.0f && !rp) { rp = true; fprintf(stderr, "[pause-suspect %d] at %.2f,%.2f pause blocked with control, spell %s\n", uo_frame, P.x, P.y, spellState ? spellState : "-"); }
    if (!pb) rp = false;
}
#endif
void hero_face_right(void) { FaceRight(); }
static bool startWithJump;
void hero_set_start_with_jump(void) { startWithJump = true; }     // HeroController.SetStartWithJump (jumping out of water)
void hero_regain_control(void) {
    P.acceptingInput = true; P.controlRelinquished = false;
    if (C->onGround) SetState(AS_IDLE); else SetState(AS_AIRBORNE);
    if (startWithJump) {      // HeroJumpNoEffect: the Knight springs out of the water
        ResetLook(); P.jump_steps = 5; C->jumping = true; P.jumpQueueSteps = 0; P.jumped_steps = 0; P.doubleJumpQueuing = false;
        startWithJump = false;
    }
}

/* ---------------- scene entry (HeroController.EnterScene) ---------------- */
static int enterFlags; static float enterGx, enterGy, enterOx, enterOy;
void hero_enter_scene(GatePos pos, float gx, float gy, float ox, float oy, int flags) {
    enterFlags = flags; enterGx = gx; enterGy = gy; enterOx = ox; enterOy = oy;
    ResetMotion(); P.airDashed = false; P.doubleJumped = false; ResetHardLandingTimer(); ResetAttacksDash();
    P.acceptingInput = false;
    // gravity already off with nothing to restore (a cutscene that set the Knight's gravity scale to 0 was cut off by the
    // room change): entering the room brings normal gravity back instead of leaving him floating
    if (P.gravityScale <= EPS && P.prevGravityScale <= EPS) P.prevGravityScale = DEFAULT_GRAVITY;
    AffectedByGravity(false);
    SetState(AS_NO_INPUT);
    P.trans = TS_WAITING_TO_ENTER_LEVEL; C->transitioning = true; P.gatePos = pos; P.setEntryAnim = false;
    P.enterT = 0; P.enterPhase = 1;
    if (flags & 1) FaceRight();
    if (flags & 2) FaceLeft();
    if (pos == GP_TOP) { C->onGround = false; P.x = gx + ox; P.y = gy + oy; }
    else if (pos == GP_BOTTOM) { C->onGround = false; P.x = gx + ox; P.y = gy + oy + 3; }
    else if (pos == GP_LEFT) { C->onGround = true; float x = gx + ox - 1; P.x = x; P.y = FindGroundPointY(x + 2, gy); FaceRight(); }
    else if (pos == GP_RIGHT) { C->onGround = true; float x = gx + ox + 1; P.x = x; P.y = FindGroundPointY(x - 2, gy); FaceLeft(); }
    else { C->onGround = true; P.x = gx; P.y = FindGroundPointY(gx, gy); anim_play(&P.anim, &A.knight, "Idle", true); }
    P.px = P.x; P.py = P.y;
}

static void enter_update(float dt) {
    if (P.enterPhase == 100) {          // hazard respawn clip
        P.enterT -= dt;
        if (P.enterT <= 0) { C->hazardRespawning = false; P.enterPhase = 0; FinishedEnteringScene(false); }
        return;
    }
    if (P.enterPhase <= 0) return;
    P.enterT += dt;
    float horExtra = 1.0f / RUN_SPEED;
    switch (P.gatePos) {
    case GP_TOP:
        if (P.enterPhase == 1 && P.enterT >= 0.365f) { P.vy = SPEED_TO_ENTER_SCENE_DOWN; P.trans = TS_DROPPING_DOWN; AffectedByGravity(true); if (enterFlags & 4) C->willHardLand = true; P.enterPhase = 2; P.enterT = 0; }
        else if (P.enterPhase == 2 && P.enterT >= 0.33f) { P.trans = TS_ENTERING_SCENE; P.enterPhase = 0; FinishedEnteringScene(true); }
        break;
    case GP_BOTTOM:
        if (P.enterPhase == 1 && P.enterT >= 0.365f) { P.transVx = C->facingRight ? SPEED_TO_ENTER_SCENE_HOR : -SPEED_TO_ENTER_SCENE_HOR; P.transVy = SPEED_TO_ENTER_SCENE_UP; P.trans = TS_ENTERING_SCENE; P.enterPhase = 2; P.enterT = 0; }
        else if (P.enterPhase == 2 && P.enterT >= TIME_TO_ENTER_SCENE_BOT) { P.transVx = P.vx; P.transVy = 0; AffectedByGravity(true); P.trans = TS_DROPPING_DOWN; P.enterPhase = 3; }
        break;
    case GP_LEFT: case GP_RIGHT:
        if (P.enterPhase == 1 && P.enterT >= 0.365f) { P.transVx = P.gatePos == GP_LEFT ? RUN_SPEED : -RUN_SPEED; P.transVy = 0; P.trans = TS_ENTERING_SCENE; P.enterPhase = 2; P.enterT = 0; AffectedByGravity(true); }
        else if (P.enterPhase == 2 && P.enterT >= 0.33f + horExtra) { P.enterPhase = 0; FinishedEnteringScene(true); }
        break;
    default:   // door
        if (P.enterPhase == 1 && P.enterT >= 0.4f) { P.enterPhase = 2; P.enterT = 0; }
        else if (P.enterPhase == 2 && P.enterT >= 0.33f) { P.enterPhase = 0; FinishedEnteringScene(true); }
        break;
    }
}

static void SetStartingMotionState(void) {
    P.move_input = P.acceptingInput ? P.in.mx : 0;
    C->touchingWall = false;
    if (CheckTouchingGround()) { C->onGround = true; SetState(AS_GROUNDED); P.airDashed = false; P.doubleJumped = false; }
    else { C->onGround = false; SetState(AS_AIRBORNE); }
    animUpdateState(P.state);
}

static void FinishedEnteringScene(bool setHazardMarker) {
    (void)setHazardMarker;              // the game keeps the hazard marker (gate marker) itself
    P.animWaitingToEnter = false;
    pd_set_bool("disablePause", false);     // HeroController.FinishedEnteringScene: whatever was cut off by the room change, pausing works again
    C->transitioning = false; P.trans = TS_WAITING_TO_TRANSITION; P.stopWalkingOut = false;
    // HK: SetStartingMotionState then gravity on, then AcceptInput
    P.state = AS_NO_INPUT;              // CanExitNoInput: allowed (not dead, not hazard respawning)
    SetStartingMotionState();
    AffectedByGravity(true);
    P.acceptingInput = true; P.controlRelinquished = false;
}

/* ---------------- HeroController.Update ---------------- */
static void FallCheck(float dt) {
    if (P.vy <= -1e-6f) {
        if (CheckTouchingGround()) return;
        C->falling = true; C->onGround = false; C->wallJumping = false;
        if (P.state != AS_NO_INPUT) SetState(AS_AIRBORNE);
        if (C->wallSliding) P.fallTimer = 0; else P.fallTimer += dt;
        if (P.fallTimer > BIG_FALL_TIME) { if (!C->willHardLand) C->willHardLand = true; }
    } else {
        C->falling = false; P.fallTimer = 0;
        if (P.trans != TS_ENTERING_SCENE) C->willHardLand = false;
    }
}

static void FailSafeChecks(float dt) {
    if (P.state == AS_HARD_LANDING) {
        P.hardLandFailSafeTimer += dt;
        if (P.hardLandFailSafeTimer > HARD_LANDING_TIME + 0.3f) { SetState(AS_GROUNDED); BackOnGround(); P.hardLandFailSafeTimer = 0; }
    } else P.hardLandFailSafeTimer = 0;
    if (P.vy != 0 || C->onGround || C->falling || C->jumping || C->dashing || P.state == AS_HARD_LANDING || P.state == AS_NO_INPUT) return;
    if (CheckTouchingGround()) {
        P.floatingBufferTimer += dt;
        if (P.floatingBufferTimer > FLOATING_CHECK_TIME) { if (C->recoiling) CancelDamageRecoil(); BackOnGround(); P.floatingBufferTimer = 0; }
    } else P.floatingBufferTimer = 0;
}

static void FilterInput(void) {
    if (P.move_input > 0.3f) P.move_input = 1; else if (P.move_input < -0.3f) P.move_input = -1; else P.move_input = 0;
    if (P.vertical_input > 0.5f) P.vertical_input = 1; else if (P.vertical_input < -0.5f) P.vertical_input = -1; else P.vertical_input = 0;
}

static void LookForInput(void) {
    const HeroInput *in = &P.in;
    if (!P.acceptingInput) return;
    P.move_input = in->mx; P.vertical_input = in->my;
    FilterInput();
    if (P.hasWalljump && CanWallSlide() && !C->attacking) {
        if (P.touchingWallL && in->leftH && !C->wallSliding) {
            P.airDashed = false; P.doubleJumped = false; C->wallSliding = true; C->willHardLand = false; P.wallSlidingL = true; P.wallSlidingR = false; FaceLeft();
        }
        if (P.touchingWallR && in->rightH && !C->wallSliding) {
            P.airDashed = false; P.doubleJumped = false; C->wallSliding = true; C->willHardLand = false; P.wallSlidingL = false; P.wallSlidingR = true; FaceRight();
        }
    }
    if (C->wallSliding && in->downP) { CancelWallsliding(); FlipSprite(); }
    if (P.wallLocked && P.wallJumpedL && in->rightH && P.wallLockSteps >= WJLOCK_STEPS_SHORT) P.wallLocked = false;
    if (P.wallLocked && P.wallJumpedR && in->leftH && P.wallLockSteps >= WJLOCK_STEPS_SHORT) P.wallLocked = false;
    if (!in->jumpH) JumpReleased();
    if (!in->dashH) { if (C->preventDash && !C->dashCooldown) C->preventDash = false; P.dashQueuing = false; }
    if (!in->atkH) P.attackQueuing = false;
}

static void LookForQueueInput(void) {
    const HeroInput *in = &P.in;
    if (!P.acceptingInput) return;
    if (in->jumpP) {
        if (CanWallJump()) DoWallJump();
        else if (CanJump()) HeroJump();
        else if (CanDoubleJump()) DoDoubleJump();
        else { P.jumpQueueSteps = 0; P.jumpQueuing = true; P.doubleJumpQueueSteps = 0; P.doubleJumpQueuing = true; }
    }
    if (in->dashP) { if (CanDash()) HeroDash(); else { P.dashQueueSteps = 0; P.dashQueuing = true; } }
    if (in->atkP) { if (CanAttack()) DoAttack(); else { P.attackQueueSteps = 0; P.attackQueuing = true; } }
    if (in->jumpH) {
        if (P.jumpQueueSteps <= JUMP_QUEUE_STEPS && CanJump() && P.jumpQueuing) HeroJump();
        else if (P.doubleJumpQueueSteps <= DOUBLE_JUMP_QUEUE_STEPS && CanDoubleJump() && P.doubleJumpQueuing) { if (C->onGround) HeroJump(); else DoDoubleJump(); }
    }
    if (in->dashH && P.dashQueueSteps <= DASH_QUEUE_STEPS && CanDash() && P.dashQueuing) HeroDash();
    if (in->atkH && P.attackQueueSteps <= ATTACK_QUEUE_STEPS && CanAttack() && P.attackQueuing) DoAttack();
}

static void UpdateAnimation(void);

// footsteps: HeroController.Update starts HeroAudioController's run or walk source while the Knight runs and stops it
// otherwise; each is one looping clip (about 2 s, several steps). Replaying the whole clip as a one-shot every 0.55 s
// layered three or four copies, out of step with his feet: the echo and the lag.
static int fsSlot = -1, fsKind;      // fsKind: FootstepSound (0 none, 1 walk, 2 run)
static bool fsTouched;
void hero_footsteps_stop(void) { if (fsSlot >= 0) sfx_loop_stop(fsSlot); fsSlot = -1; fsKind = 0; }
void hero_footsteps_end_frame(void) { if (!fsTouched) hero_footsteps_stop(); fsTouched = false; }   // a frame without the Knight's update (pause, menus): silent
static void hero_footsteps_update(float dt) {
    fsTouched = true;
    int want = 0;
    if (P.state == AS_RUNNING && !C->dashing && !C->backDashing && !P.controlRelinquished && !C->dead) want = C->inWalkZone ? 1 : 2;
    else if (P.forceFootstep && C->onGround && fabsf(P.vx) > 0.1f) want = P.forceFootstep == 1 ? 1 : 2;     // ForceHeroFootstepSound (NPC walk-ups)
    if (fsSoftLandT > 0) { fsSoftLandT -= dt; if (!fsKind) want = 0; }
    if (want == fsKind && (want == 0 || sfx_loop_playing(fsSlot))) return;
#ifdef __HOST__
    if (getenv("HK_STEPTRACE")) fprintf(stderr, "[steps] %s -> %s (state %d x %.2f)\n", fsKind == 2 ? "run" : fsKind == 1 ? "walk" : "none", want == 2 ? "run" : want == 1 ? "walk" : "none", P.state, P.x);
#endif
    hero_footsteps_stop();
    // volume: the game's footstep sources play at 1 and its jump at 0.5 (0.8 here); the clip itself is quiet
    if (want) { fsSlot = sfx_loop_start(want == 1 ? "hero_walk_footsteps_stone" : "hero_run_footsteps_stone", 0.7f, 1.0f); if (fsSlot >= 0) fsKind = want; }
}

void hero_update(float dt, const HeroInput *in) {
    P.in = *in;
    P.timeSinceLevelLoad += dt;
    if (P.drainMP) {                    // HeroController.Update: focus soul drain (StartMPDrain / StopMPDrain)
        P.drainMPTimer += dt;
        while (P.drainMPTime > 0 && P.drainMPTimer >= P.drainMPTime) {
            P.mpDrained++; P.drainMPTimer -= P.drainMPTime;
            hero_take_mp(1);
            if (P.mpDrained == P.focusMPAmount) { P.mpDrained -= (int)P.drainMPTime; hero_proxy_event("HeroCtrl-FocusCompleted"); }
        }
    }
    if (P.parryInvulnTimer > 0) P.parryInvulnTimer -= dt;
    // nail art charge (HeroController.Update): hold the nail with a nail art learnt
    P.nailChargeTime = pd_bool("equippedCharm_26") ? 0.75f : 1.35f;
    if (in->atkH && !C->attacking && !P.controlRelinquished && !C->recoiling && !C->recoilingLeft && !C->recoilingRight && pd_bool("hasNailArt")) {
        if (!C->nailCharging && P.nailChargeTimer == 0) { }
        C->nailCharging = true; P.nailChargeTimer += dt;
        if (P.nailChargeTimer >= P.nailChargeTime && !P.nailCharged) { P.nailCharged = true; sfx_play("hero_nail_art_charge_complete", 1.0f); game_shake(0.1f); }
    } else if (C->nailCharging || P.nailChargeTimer != 0) { C->nailCharging = false; P.nailCharged = false; if (!in->atkH) P.nailChargeTimer = 0; }
    anim_update(&P.anim, dt);
    anim_update(&P.slashFx, dt);
    if (P.slashing && P.slashFx.done) P.slashAnimDone = true;
    if (P.freezeT > 0) {                // GameManager.FreezeMoment, then StartRecoil continues
        P.freezeT -= dt;
        if (P.freezeT <= 0 && C->recoilFrozen) { C->recoilFrozen = false; C->recoiling = true; pd_set_bool("disablePause", false); }
        fsTouched = true;       // a freeze moment doesn't cut the footsteps (Update still runs in the game)
        return;
    }
    // Invulnerable() coroutine
    if (P.invulRoutine) {
        if (P.invulFreeze > 0) P.invulFreeze -= dt;
        else { P.invulDuration -= dt; if (P.invulDuration <= 0) { C->invulnerable = false; C->recoiling = false; P.invulRoutine = false; } }
    }
    enter_update(dt);
    FallCheck(dt);
    FailSafeChecks(dt);
    hero_footsteps_update(dt);
    if (P.state == AS_DASH_LANDING) { P.dashLandingTimer += dt; if (P.dashLandingTimer > DOWN_DASH_TIME) BackOnGround(); }
    if (P.state == AS_HARD_LANDING) {
        P.hardLandingTimer += dt;
        if (P.hardLandingTimer > HARD_LANDING_TIME) { SetState(AS_GROUNDED); BackOnGround(); }
    } else if (P.state == AS_NO_INPUT) {
        if (C->recoiling) {
            if (P.recoilTimer < RECOIL_DURATION) P.recoilTimer += dt;
            else {
                CancelDamageRecoil();
                if ((P.prevState == AS_IDLE || P.prevState == AS_RUNNING) && !CheckTouchingGround()) { C->onGround = false; SetState(AS_AIRBORNE); }
                else SetState(AS_PREVIOUS);
            }
        }
    } else {
        LookForInput();
        if (C->recoiling) { C->recoiling = false; AffectedByGravity(true); }
        if (C->attacking && !C->dashing) {
            P.attack_time += dt;
            if (P.attack_time >= P.attackDuration) {
                ResetAttacks();
                if (anim_is(&P.anim, "UpSlash") || anim_is(&P.anim, "DownSlash")) P.anim.done = true;   // animCtrl.StopAttack
            }
        }
        if (C->bouncing) {
            if (P.bounceTimer < BOUNCE_TIME) P.bounceTimer += dt;
            else { CancelBounce(); P.vy = 0; }
        }
        if (C->shroomBouncing && P.vy <= 0) C->shroomBouncing = false;
        if (P.state == AS_IDLE) {
            if (!P.controlRelinquished) {
                if (in->upH) {
                    C->lookingDown = false; C->lookingDownAnim = false;
                    if (P.lookDelayTimer >= LOOK_DELAY) C->lookingUp = true; else P.lookDelayTimer += dt;
                    C->lookingUpAnim = P.lookDelayTimer >= LOOK_ANIM_DELAY;
                } else if (in->downH) {
                    C->lookingUp = false; C->lookingUpAnim = false;
                    if (P.lookDelayTimer >= LOOK_DELAY) C->lookingDown = true; else P.lookDelayTimer += dt;
                    C->lookingDownAnim = P.lookDelayTimer >= LOOK_ANIM_DELAY;
                } else ResetLook();
            }
        }
    }
    LookForQueueInput();
    if (C->wallSliding) {
        if (P.airDashed) P.airDashed = false;
        if (P.doubleJumped) P.doubleJumped = false;
        if (C->onGround) { FlipSprite(); CancelWallsliding(); }
        if (!C->touchingWall) { FlipSprite(); CancelWallsliding(); }
        if (!CanWallSlide()) CancelWallsliding();
    }
    if (P.attack_cooldown > 0) P.attack_cooldown -= dt;
    if (P.dashCooldownTimer > 0) P.dashCooldownTimer -= dt;
    if (P.shadowDashTimer > 0) P.shadowDashTimer -= dt;
    if (!P.animWaitingToEnter && !P.animCtrlStopped) UpdateAnimation();
}

/* ---------------- HeroController.FixedUpdate ---------------- */
static void Move(float dir) {
    if (C->onGround) SetState(AS_GROUNDED);
    if (P.acceptingInput && !C->wallSliding) P.vx = dir * (C->inWalkZone ? WALK_SPEED : charm_run_speed(RUN_SPEED));
}
static void Jump(void) {
    if (P.jump_steps <= JUMP_STEPS) { P.vy = JUMP_SPEED; P.jump_steps++; P.jumped_steps++; P.ledgeBufferSteps = 0; }
    else CancelJump();
}
static void DoubleJump(void) {
    if (P.doubleJump_steps <= DOUBLE_JUMP_STEPS) { if (P.doubleJump_steps > 3) P.vy = JUMP_SPEED * 1.1f; P.doubleJump_steps++; }
    else CancelDoubleJump();
    if (C->onGround) CancelDoubleJump();
}
static void Dash(void) {
    AffectedByGravity(false); ResetHardLandingTimer();
    if (P.dash_timer > DASH_TIME) { FinishedDashing(); return; }
    float s = (charm_act(16) && C->shadowDashing) ? DASH_SPEED_SHARP : DASH_SPEED;
    if (P.dashingDown) { P.vx = 0; P.vy = -s; }
    else if (C->facingRight) { P.vx = s; P.vy = CheckForBump(CS_RIGHT) ? (C->onGround ? BUMP_VELOCITY : BUMP_VELOCITY_DASH) : 0; }
    else { P.vx = -s; P.vy = CheckForBump(CS_LEFT) ? (C->onGround ? BUMP_VELOCITY : BUMP_VELOCITY_DASH) : 0; }
    P.dash_timer += HERO_FIXED_DT;
}

static void slash_fixed(void) {        // NailSlash.FixedUpdate
    if (!P.slashing) return;
    if (P.slashStep == 1) P.slashPoly = true;
    if (P.slashStep >= 5 && P.slashPolyCount > 0) P.slashPoly = false;
    if (P.slashAnimDone && P.slashPolyCount > 1) SlashCancel();
    if (P.slashPoly) P.slashPolyCount++;
    P.slashStep++;
}

static void OnCollisionEnter(int side) {
    if (side == CS_BOTTOM && CheckTouchingGround()) {}
    if (P.state != AS_NO_INPUT) {
        if (side == CS_TOP) {
            P.headBumpSteps = HEAD_BUMP_STEPS;
            if (C->jumping) { CancelJump(); CancelDoubleJump(); }
            if (C->bouncing || C->shroomBouncing) { CancelBounce(); P.vy = 0; }
        }
        if (side == CS_BOTTOM) {
            if (C->attacking) CancelDownAttack();
            if (ShouldHardLand()) DoHardLanding();
            else if (P.state != AS_HARD_LANDING) BackOnGround();
            if (C->dashing && P.dashingDown) { AffectedByGravity(true); SetState(AS_DASH_LANDING); P.hardLanded = true; }
        }
    } else if (P.trans == TS_DROPPING_DOWN && (P.gatePos == GP_BOTTOM || P.gatePos == GP_TOP) && side == CS_BOTTOM) {
        P.enterPhase = 0; FinishedEnteringScene(true);
    }
}
static void OnCollisionStay(void) {
    if (P.state == AS_NO_INPUT) return;
    C->touchingNonSlider = false;
    if (CheckStillTouchingWall(CS_LEFT)) { C->touchingWall = true; P.touchingWallL = true; P.touchingWallR = false; }
    else if (CheckStillTouchingWall(CS_RIGHT)) { C->touchingWall = true; P.touchingWallL = false; P.touchingWallR = true; }
    else { C->touchingWall = false; P.touchingWallL = P.touchingWallR = false; }
    if (C->touchingWall) {      // a NonSlider collider: the Knight can't cling or wall-jump there
        float x0 = P.touchingWallL ? bminx() - 0.1f : bmaxx(), x1 = P.touchingWallL ? bminx() : bmaxx() + 0.1f;
        if (game_nonslider(x0, bminy() + 0.06f, x1, bmaxy())) { C->touchingNonSlider = true; C->touchingWall = false; P.touchingWallL = P.touchingWallR = false; }
    }
    if (CheckTouchingGround()) {
        if (ShouldHardLand()) DoHardLanding();
        else if (P.state != AS_HARD_LANDING && P.state != AS_DASH_LANDING && C->falling) BackOnGround();
    } else if (C->jumping || C->falling) { C->onGround = false; SetState(AS_AIRBORNE); }
}
static void OnCollisionExit(void) {
    if (C->recoilingLeft || C->recoilingRight) { C->touchingWall = false; P.touchingWallL = P.touchingWallR = false; C->touchingNonSlider = false; }
    if (P.touchingWallL && !CheckStillTouchingWall(CS_LEFT)) { C->touchingWall = false; P.touchingWallL = false; }
    if (P.touchingWallR && !CheckStillTouchingWall(CS_RIGHT)) { C->touchingWall = false; P.touchingWallR = false; }
    if (P.state == AS_NO_INPUT || C->recoiling || CheckTouchingGround()) return;
    C->onGround = false;
    SetState(AS_AIRBORNE);
    if (C->wasOnGround) P.ledgeBufferSteps = LEDGE_BUFFER_STEPS;
}

static int probe_contacts(void) {
    int m = 0; const float e = 0.03f, in = 0.02f;
    if (w_solid_box(bminx() + in, bminy() - e, bmaxx() - in, bminy())) m |= 1 << CS_BOTTOM;
    if (w_solid_box(bminx() + in, bmaxy(), bmaxx() - in, bmaxy() + e)) m |= 1 << CS_TOP;
    if (w_solid_box(bminx() - e, bminy() + 0.06f, bminx(), bmaxy() - in)) m |= 1 << CS_LEFT;
    if (w_solid_box(bmaxx(), bminy() + 0.06f, bmaxx() + e, bmaxy() - in)) m |= 1 << CS_RIGHT;
    return m;
}

static void physics_step(void) {
    const float dt = HERO_FIXED_DT;
    // Rigidbody2D: gravity, then integrate with collisions
    P.vy -= PHYS_GRAVITY * P.gravityScale * dt;
    P.px = P.x; P.py = P.y;
    float cx = P.x, cy = bcy();
    if (!w_unstick_box(&cx, &cy, HALF_W, HALF_H, 0.5f)) w_unstick_box(&cx, &cy, HALF_W, HALF_H, 3.0f);
    bool grounded = C->onGround && !C->jumping && P.vy <= 0.01f;
    int fl = w_move_box(&cx, &cy, HALF_W, HALF_H, P.vx * dt, P.vy * dt, grounded ? 0.3f : 0);
    // stand on downward slopes (polygon slopes in the original; stairs on the grid)
    if (grounded && !(fl & 2) && !(fl & 4)) { float cy2 = cy; int f2 = w_move_box(&cx, &cy2, HALF_W, HALF_H, 0, -0.3f, 0); if (f2 & 2) { cy = cy2; fl |= 2; } }
    P.x = cx; P.y = cy - COL_OFF_Y;
    if (fl & 1) P.vx = 0;
    if ((fl & 2) && P.vy < 0) P.vy = 0;
    if ((fl & 4) && P.vy > 0) P.vy = 0;
    // collision callbacks
    int cur = probe_contacts(), prev = P.contacts;
    int newly = cur & ~prev;
    bool entered = false;
    if (newly & (1 << CS_TOP)) { OnCollisionEnter(CS_TOP); entered = true; }
    if (newly & (1 << CS_BOTTOM)) { OnCollisionEnter(CS_BOTTOM); entered = true; }
    if (newly & ((1 << CS_LEFT) | (1 << CS_RIGHT))) { OnCollisionEnter(newly & (1 << CS_LEFT) ? CS_LEFT : CS_RIGHT); entered = true; }
    if (cur && !entered) OnCollisionStay();
    else if (cur && entered) OnCollisionStay();   // Unity also reports the other, ongoing contacts this step
    if (prev & ~cur) OnCollisionExit();
    P.contacts = cur;
}

void hero_fixed(void) {
#ifdef __HOST__
    { static int tr = -1; if (tr < 0) tr = getenv("HK_HEROTRACE") != NULL; if (tr) fprintf(stderr, "[hero] x=%.2f y=%.2f vx=%.2f vy=%.2f st=%d g=%.2f sd=%d ctl=%d\n", P.x, P.y, P.vx, P.vy, P.state, P.gravityScale, C->superDashing, P.controlRelinquished); }
    {   // HK_WALLTRACE: in the air, holding toward a wall that is within 0.1: the gap, the contacts and whether the Knight clings
        static int tr = -1; if (tr < 0) tr = getenv("HK_WALLTRACE") != NULL;
        if (tr && !C->onGround && (P.in.rightH || P.in.leftH)) { float h = -1; bool r = P.in.rightH;
            if (w_raycast(r ? bmaxx() : bminx(), bcy(), r ? 1 : -1, 0, 0.1f, &h))
                fprintf(stderr, "[wall] x=%.3f gap=%.4f contacts=%d touching=%d sliding=%d falling=%d\n", P.x, h, P.contacts, C->touchingWall, C->wallSliding, C->falling); } }
#endif
    if (P.freezeT > 0) return;
    conveyor_late();
    if (C->recoilingLeft || C->recoilingRight) { if (P.recoilSteps <= RECOIL_HOR_STEPS) P.recoilSteps++; else CancelRecoilHorizontal(); }
    if (C->dead) { P.vx = P.vy = 0; }
    if ((P.state == AS_HARD_LANDING) || P.state == AS_DASH_LANDING) ResetMotion();
    else if (P.state == AS_NO_INPUT) {
        if (C->transitioning) {
            if (P.trans == TS_EXITING_SCENE) { AffectedByGravity(false); if (!P.stopWalkingOut) { P.vx = P.transVx; P.vy = P.transVy + P.vy; } }
            else if (P.trans == TS_ENTERING_SCENE) { P.vx = P.transVx; P.vy = P.transVy; }
            else if (P.trans == TS_DROPPING_DOWN) { P.vx = P.transVx; }
        } else if (C->recoiling) { AffectedByGravity(false); P.vx = P.recoilVx; P.vy = P.recoilVy; }
    } else {
        if (P.state == AS_RUNNING) {
            if (P.move_input > 0) { if (CheckForBump(CS_RIGHT)) P.vy = BUMP_VELOCITY; }
            else if (P.move_input < 0 && CheckForBump(CS_LEFT)) P.vy = BUMP_VELOCITY;
        }
        if (!C->backDashing && !C->dashing) {
            Move(P.move_input);
            if ((!C->attacking || !(P.attack_time < ATTACK_RECOVERY_TIME)) && !C->wallSliding && !P.wallLocked) {
                if (P.move_input > 0 && !C->facingRight) { FlipSprite(); CancelAttack(); }
                else if (P.move_input < 0 && C->facingRight) { FlipSprite(); CancelAttack(); }
            }
            if (C->recoilingLeft) { float n = P.recoilLarge ? RECOIL_HOR_VELOCITY_LONG : RECOIL_HOR_VELOCITY; P.vx = P.vx > -n ? -n : P.vx - n; }
            if (C->recoilingRight) { float n = P.recoilLarge ? RECOIL_HOR_VELOCITY_LONG : RECOIL_HOR_VELOCITY; P.vx = P.vx < n ? n : P.vx + n; }
        }
        if ((C->lookingUp || C->lookingDown) && fabsf(P.move_input) > 0.6f) ResetLook();
        if (C->jumping) Jump();
        if (C->doubleJumping) DoubleJump();
        if (C->dashing) Dash();
        if (C->bouncing) P.vy = BOUNCE_VELOCITY;
        if (C->superDashOnWall && !C->onConveyorV) { P.vx = P.vy = 0; }
        if (C->onConveyor && ((C->onGround && !C->superDashing) || P.state == AS_HARD_LANDING)) {
            if (C->freezeCharge || P.state == AS_HARD_LANDING || P.controlRelinquished) { P.vx = P.vy = 0; }
            P.vx += P.conveyorSpeed;
        }
        if (C->slidingLeft || C->slidingRight) {       // SteepSlope: the Knight slides down
            P.vy = -20; ResetHardLandingTimer();
            if (C->slidingLeft && P.vx > -5) P.vx = -5;
            if (C->slidingRight && P.vx < 5) P.vx = 5;
        }
        if (C->inConveyorZone) {
            if (C->freezeCharge || P.state == AS_HARD_LANDING) { P.vx = P.vy = 0; }
            P.vx += P.conveyorSpeed;
        }
        if (P.wallLocked) {
            if (P.wallJumpedR) P.vx = P.currentWalljumpSpeed;
            else if (P.wallJumpedL) P.vx = -P.currentWalljumpSpeed;
            P.wallLockSteps++;
            if (P.wallLockSteps > WJLOCK_STEPS_LONG) P.wallLocked = false;
            P.currentWalljumpSpeed -= P.walljumpSpeedDecel;
        }
        if (C->wallSliding) {
            if (P.wallSlidingL && P.in.rightH) P.wallUnstickSteps++;
            else if (P.wallSlidingR && P.in.leftH) P.wallUnstickSteps++;
            else P.wallUnstickSteps = 0;
            if (P.wallUnstickSteps >= WALL_STICKY_STEPS) CancelWallsliding();
            if (P.wallSlidingL) { if (!CheckStillTouchingWall(CS_LEFT)) { FlipSprite(); CancelWallsliding(); } }
            else if (P.wallSlidingR && !CheckStillTouchingWall(CS_RIGHT)) { FlipSprite(); CancelWallsliding(); }
        }
    }
    if (P.vy < -MAX_FALL_VELOCITY) P.vy = -MAX_FALL_VELOCITY;
    if (P.jumpQueuing) P.jumpQueueSteps++;
    if (P.doubleJumpQueuing) P.doubleJumpQueueSteps++;
    if (P.dashQueuing) P.dashQueueSteps++;
    if (P.attackQueuing) P.attackQueueSteps++;
    if (C->wallSliding && !C->onConveyorV) {
        if (P.vy > WALLSLIDE_SPEED) { P.vy -= WALLSLIDE_DECEL; if (P.vy < WALLSLIDE_SPEED) P.vy = WALLSLIDE_SPEED; }
        if (P.vy < WALLSLIDE_SPEED) { P.vy += WALLSLIDE_DECEL; if (P.vy < WALLSLIDE_SPEED) P.vy = WALLSLIDE_SPEED; }
    }
    if (P.landingBufferSteps > 0) P.landingBufferSteps--;
    if (P.ledgeBufferSteps > 0) P.ledgeBufferSteps--;
    if (P.headBumpSteps > 0) P.headBumpSteps--;
    if (P.jumpReleaseQueueSteps > 0) P.jumpReleaseQueueSteps--;
    C->wasOnGround = C->onGround;
    slash_fixed();
    physics_step();
    P.onGround = C->onGround;
}

/* ---------------- HeroAnimationController.UpdateAnimation ---------------- */
static bool CanPlayIdle(void) {
    static const char *b[] = { "Land", "Run To Idle", "Dash To Idle", "Backdash Land", "Backdash Land 2", "LookUpEnd", "LookDownEnd", "Exit Door To Idle", "Wake Up Ground", "Hazard Respawn" };
    for (unsigned i = 0; i < sizeof b / sizeof *b; i++) if (playing(b[i])) return false;
    return true;
}
static void PlayIdle(void) {
    if (P.health == 1) Play("Idle Hurt");
    else if (anim_is(&P.anim, "LookUp") && !anim_is(&P.anim, "LookUpEnd")) Play("LookUpEnd");
    else if (anim_is(&P.anim, "LookDown") && !anim_is(&P.anim, "LookDownEnd")) Play("LookDownEnd");
    else Play("Idle");
}
static void UpdateAnimation(void) {
    // animation-complete callbacks (Land / Run To Idle / Dash To Idle -> Idle)
    if ((anim_is(&P.anim, "Land") || anim_is(&P.anim, "Run To Idle") || anim_is(&P.anim, "Dash To Idle")) && P.anim.done) PlayIdle();
    if (P.playLanding) { anim_play(&P.anim, &A.knight, "Land", true); P.playLanding = false; }
    if (P.playRunToIdle) { anim_play(&P.anim, &A.knight, "Run To Idle", true); P.playRunToIdle = false; }
    if (P.playDashToIdle) { anim_play(&P.anim, &A.knight, "Dash To Idle", true); P.playDashToIdle = false; }
    ActorState as = P.animState;
    if (as == AS_NO_INPUT) {
        if (C->recoilFrozen) Play("Stun");
        else if (C->recoiling) Play("Recoil");
        else if (C->transitioning) {
            if (C->onGround) {
                if (P.trans == TS_EXITING_SCENE) { if (!anim_is(&P.anim, "Run")) Play("Run"); }
                else if (P.trans == TS_ENTERING_SCENE) { if (!anim_is(&P.anim, "Run")) PlayFromFrame("Run", 3); }
            } else if (P.trans == TS_EXITING_SCENE || P.trans == TS_WAITING_TO_ENTER_LEVEL) { if (!anim_is(&P.anim, "Airborne")) PlayFromFrame("Airborne", 7); }
            else if (P.trans == TS_ENTERING_SCENE && !P.setEntryAnim) {
                if (P.gatePos == GP_TOP) PlayFromFrame("Airborne", 7);
                else if (P.gatePos == GP_BOTTOM) PlayFromFrame("Airborne", 3);
                P.setEntryAnim = true;
            }
        }
    } else if (P.setEntryAnim) P.setEntryAnim = false;
    else if (C->dashing) {
        if (P.dashingDown) Play(C->shadowDashing ? (charm_act(16) ? "Shadow Dash Down Sharp" : "Shadow Dash Down") : "Dash Down");
        else if (C->shadowDashing) Play(charm_act(16) ? "Shadow Dash Sharp" : "Shadow Dash");
        else Play("Dash");
    }
    else if (C->attacking) {
        if (C->upAttacking) Play("UpSlash");
        else if (C->downAttacking) Play("DownSlash");
        else if (C->wallSliding) Play("Wall Slash");
        else if (!C->altAttack) Play("Slash");
        else Play("SlashAlt");
    } else if (C->casting) Play("Fireball");
    else if (C->wallSliding) Play("Wall Slide");
    else if (as == AS_IDLE) {
        if (C->lookingUpAnim && !anim_is(&P.anim, "LookUp")) Play("LookUp");
        else if (C->lookingDownAnim && !anim_is(&P.anim, "LookUp")) Play("LookDown");
        else if (!C->lookingUpAnim && !C->lookingDownAnim && CanPlayIdle()) PlayIdle();
    } else if (as == AS_RUNNING) {
        if (!playing("Turn")) { if (P.wasAttacking && !anim_is(&P.anim, "Run")) PlayFromFrame("Run", 3); else Play("Run"); }
    } else if (as == AS_AIRBORNE) {
        if (P.wallLocked) Play("Walljump");
        else if (C->doubleJumping) Play("Double Jump");
        else if (C->jumping) { if (!anim_is(&P.anim, "Airborne")) PlayFromFrame("Airborne", 0); }
        else if (C->falling) { if (!anim_is(&P.anim, "Airborne")) PlayFromFrame("Airborne", 5); }
        else if (!anim_is(&P.anim, "Airborne")) PlayFromFrame("Airborne", 3);
    } else if (as == AS_DASH_LANDING) Play("Dash Down Land");
    else if (as == AS_HARD_LANDING) Play("HardLand");
    bool canTurn = !playing("Wake Up Ground") && !playing("Hazard Respawn");
    if (C->facingRight) { if (!P.wasFacingRight && C->onGround && canTurn && as != AS_NO_INPUT) Play("Turn"); P.wasFacingRight = true; }
    else { if (P.wasFacingRight && C->onGround && canTurn && as != AS_NO_INPUT) Play("Turn"); P.wasFacingRight = false; }
    P.wasAttacking = C->attacking;
}

/* ---------------- reset ---------------- */
void hero_set_position(float x, float y) { P.x = P.px = x; P.y = P.py = y; }

void hero_reset(void) {
    AnimPlayer a0 = { 0 }; a0.anim = -1;
    memset(&P, 0, sizeof P);
    P.anim = a0; P.slashFx = a0;
    P.maxHealth = 5; P.health = 5;
    P.gravityScale = DEFAULT_GRAVITY;
    P.acceptingInput = true;
    FaceRight(); P.wasFacingRight = true;
    P.state = AS_AIRBORNE; P.animState = AS_AIRBORNE;
    P.trans = TS_WAITING_TO_TRANSITION;
    P.slashAngle = 0;
    anim_play(&P.anim, &A.knight, "Airborne", true);
}

void hero_recoil_left(void) { RecoilLeft(); }
void hero_recoil_right(void) { RecoilRight(); }
void hero_recoil_down(void) { RecoilDown(); }

/* ---------------- HeroController methods the Knight's own FSMs call ---------------- */
void hero_proxy_event(const char *ev) { UObj *h = uo_hero(); if (h) uo_send_event(h, ev); }
void hero_set_cstate(const char *n, bool v) {      // HeroControllerStates.SetState
    if (!n) return;
    if (!strcmp(n, "focusing")) { C->focusing = v; P.focusing = v; }
    else if (!strcmp(n, "freezeCharge")) C->freezeCharge = v;
    else if (!strcmp(n, "spellQuake")) C->spellQuake = v;
    else if (!strcmp(n, "superDashing")) C->superDashing = v;
    else if (!strcmp(n, "superDashOnWall")) C->superDashOnWall = v;
    else if (!strcmp(n, "dashing")) C->dashing = v;
    else if (!strcmp(n, "facingRight")) { if (v) FaceRight(); else FaceLeft(); }
    else if (!strcmp(n, "onGround")) C->onGround = v;
    else if (!strcmp(n, "wallSliding")) C->wallSliding = v;
    else if (!strcmp(n, "casting")) C->casting = v;
    else if (!strcmp(n, "invulnerable")) C->invulnerable = v;
    else if (!strcmp(n, "attacking")) C->attacking = v;
    else if (!strcmp(n, "swimming")) {
        // out of the water (the swim script turned gravity off on the way in and has nothing that turns it back on): the
        // Knight floated after jumping out of a pool, only able to move sideways
        if (C->swimming && !v) AffectedByGravity(true);
        C->swimming = v;
    }
    else if (!strcmp(n, "inAcid")) C->inAcid = v;
    else if (!strcmp(n, "nailCharging")) C->nailCharging = v;
    else if (!strcmp(n, "isPaused")) {}
}
static bool not_recovering(void) { return !C->attacking || !(P.attack_time < ATTACK_RECOVERY_TIME); }
bool hero_can_cast(void) {
    return !game_is_paused() && !C->dashing && P.state != AS_NO_INPUT && !C->backDashing && not_recovering() && !C->recoiling &&
           !C->recoilFrozen && !C->transitioning && !C->hazardDeath && !C->hazardRespawning && CanInput();
}
bool hero_can_focus(void) {
    return !game_is_paused() && P.state != AS_NO_INPUT && !C->dashing && !C->backDashing && not_recovering() && !C->recoiling &&
           C->onGround && !C->transitioning && !C->recoilFrozen && !C->hazardDeath && !C->hazardRespawning && CanInput();
}
bool hero_can_nail_art(void) {
    bool ok = !C->transitioning && P.state != AS_NO_INPUT && !C->attacking && !C->hazardDeath && !C->hazardRespawning && P.nailChargeTimer >= P.nailChargeTime;
    P.nailChargeTimer = 0;
    return ok;
}
bool hero_can_quick_map(void) {
    return !game_is_paused() && !P.controlRelinquished && P.state != AS_NO_INPUT && !C->dashing && !C->backDashing && not_recovering() &&
           !C->recoiling && !C->transitioning && !C->hazardDeath && !C->hazardRespawning && !C->recoilFrozen && C->onGround && CanInput();
}
bool hero_can_superdash(void) {
    return !game_is_paused() && P.state != AS_NO_INPUT && !C->dashing && !C->hazardDeath && !C->hazardRespawning && !C->backDashing &&
           not_recovering() && !P.controlRelinquished && !C->recoilFrozen && !C->recoiling && !C->transitioning && pd_bool("hasSuperDash") &&
           (C->onGround || C->wallSliding);
}
bool hero_can_dream_nail(void) {
    return !game_is_paused() && P.state != AS_NO_INPUT && !C->dashing && !C->backDashing && not_recovering() && !P.controlRelinquished &&
           !C->hazardDeath && P.vy > -0.1f && !C->hazardRespawning && !C->recoilFrozen && !C->recoiling && !C->transitioning &&
           pd_bool("hasDreamNail") && C->onGround;
}
void hero_take_mp(int n) {                // PlayerData.TakeMP (soul vessels drain first)
    if (P.soul <= 0 && pd_int("MPReserve") <= 0) return;
    P.soul -= n; if (P.soul < 0) P.soul = 0;
}
void hero_start_mp_drain(float t) { P.drainMP = true; P.drainMPTimer = 0; P.mpDrained = 0; P.drainMPTime = t; P.focusMPAmount = pd_int("focusMP_amount"); if (P.focusMPAmount <= 0) P.focusMPAmount = 33; }
void hero_stop_mp_drain(void) { P.drainMP = false; }
void hero_quake_invuln(void) { P.parryInvulnTimer = 0.2f; }       // INVUL_TIME_QUAKE
void hero_reset_air_moves(void) { P.doubleJumped = false; P.airDashed = false; }
void hero_set_damage_mode(int m) { P.damageMode = m; }
void hero_reset_hard_landing(void) { ResetHardLandingTimer(); P.hardLanded = false; }
void hero_cancel_attack(void) { CancelAttack(); }
void hero_flip_sprite(void) { FlipSprite(); }
void hero_set_mp(int v) { P.soul = v < 0 ? 0 : v > 99 ? 99 : v; }
void hero_anim_stop(void) { P.anim.stopped = true; }
bool hero_anim_done(void) { return P.anim.done; }

#ifdef __HOST__
void hero_test_stop(void) { P.vx = P.vy = 0; }    // tests that put the Knight somewhere (HK_TPSTATE): he arrives standing still
#endif
