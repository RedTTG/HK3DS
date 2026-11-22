// DialogueManager / prompt markers / NPC titles (see dialogue.h).
// Box Open / Box Open YN: FadeGroupUp + iTweenScaleTo 0.75 -> 1 (0.5s, easeOutBack) on BOX UP, back on BOX DOWN.
// DialogueBox: TextMeshPro page mode with a typewriter (revealSpeed 65 normal / 100 yes-no, 200 when sped up).
// Dialogue Page Control: page end -> arrow, wait for Jump/Attack/Cast/Submit -> "button" sound, next page;
// conversation end -> stop (or arrow when "Use Stop" is false) -> input -> broadcast CONVO_FINISH.
// YN: conversation end -> geo cost shown (Toll Cost > 0) -> Yes/No list -> TakeGeo, wait 1s -> YES / NO to Requester.
#include "dialogue.h"
#include "fsm.h"
#include "game.h"
#include "hero.h"
#include "lang.h"
#include "pd.h"
#include "audio.h"
#include "input.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static SpriteSet D;            // dialog.spr: "Dlg <clip>" (Dialogue Anim), "Ttl <clip>" (Area Titles Anim)
static Fsm *varsN, *varsYN;

/* ---------------- small sprite-anim helper ---------------- */
typedef struct { int anim; float t; bool once; } Clip;
static void clip_play(Clip *c, const char *name) {
    char nm[40]; snprintf(nm, sizeof nm, "%s", name);
    c->anim = spr_anim(&D, nm); c->t = 0;
    c->once = c->anim >= 0 && (D.anims[c->anim].wrap == WRAP_ONCE || D.anims[c->anim].wrap == WRAP_SINGLE);
}
static int clip_frame(const Clip *c) {
    if (c->anim < 0) return -1;
    const Anim *a = &D.anims[c->anim];
    if (a->count == 0) return -1;
    int f = (int)(c->t * a->fps);
    if (a->wrap == WRAP_LOOPSECTION && f >= a->count) f = a->loopStart + (f - a->loopStart) % (a->count - a->loopStart);
    else if (c->once || a->wrap == WRAP_SINGLE) { if (f >= a->count) f = a->count - 1; }
    else f %= a->count;
    return D.animFrames[a->first + f];
}
static bool clip_done(const Clip *c) {
    if (c->anim < 0) return true;
    const Anim *a = &D.anims[c->anim];
    return c->once && c->t * a->fps >= a->count;
}

/* ---------------- dialogue boxes ---------------- */
#define MAX_TEXT  2400
#define MAX_PAGES 48
#define TEXT_W    316
#define TEXT_X    42
#define LINES_N   4           // lines per page in the dialogue box (TMP overflow -> next page)
#define LINES_YN  3

enum { DS_NONE, DS_TYPING, DS_PAGE_END, DS_STOP_PAUSE, DS_CONV_END, DS_YN_GEO, DS_YN_INPUT, DS_YN_TAKE, DS_DONE };

typedef struct {
    bool up; float alpha, scale, tweenT, tweenFrom, tweenTo, tweenDur; int tweenEase;
    Clip fleurTop, fleurBot, arrow, stop;
    bool showArrow, showStop;
    char text[MAX_TEXT];            // wrapped text, pages separated by '\f'
    int pstart[MAX_PAGES], plen[MAX_PAGES], npages, page;
    float shown, speed, normalSpeed;
    bool hidden, fast;
    int state; float timer;
    int sel;                        // yes/no selection: 0 yes, 1 no
    int cost; UObj *requester;
} Box;
static Box B[2];

static float ease_out_back(float t) { const float s = 1.70158f; t -= 1; return t * t * ((s + 1) * t + s) + 1; }
static float ease_in_out_sine(float t) { return -0.5f * (cosf((float)M_PI * t) - 1); }

static void box_tween(Box *b, float to, float dur, int ease) { b->tweenFrom = b->scale; b->tweenTo = to; b->tweenT = 0; b->tweenDur = dur; b->tweenEase = ease; }

static bool use_stop(void) { FVar *v = varsN ? fsm_var(varsN, "Use Stop") : NULL; return v ? v->b : true; }
static void set_use_stop(bool s) { FVar *v = varsN ? fsm_var(varsN, "Use Stop") : NULL; if (v) v->b = s; }

// word wrap one raw page into pages of at most `lines` lines
static void layout(Box *b, const char *src, int lines) {
    const Font *f = &A.fontText;
    b->npages = 0; int n = 0;
    const char *p = src;
    while (*p && b->npages < MAX_PAGES) {
        // one raw page: up to '\f'
        const char *pe = strchr(p, '\f'); if (!pe) pe = p + strlen(p);
        int line = 0; int start = n;
        b->pstart[b->npages] = n;
        char cur[512]; int cl = 0; float cw = 0;
        const char *q = p;
        while (q <= pe) {
            // next word
            while (q < pe && *q == ' ') q++;
            const char *ws = q;
            while (q < pe && *q != ' ' && *q != '\n') q++;
            int wl = (int)(q - ws);
            char word[256]; if (wl > 255) wl = 255; memcpy(word, ws, wl); word[wl] = 0;
            float ww = font_width(f, word), sp = font_width(f, " ");
            bool hardBreak = q < pe && *q == '\n';
            if (wl > 0 && cl > 0 && cw + sp + ww > TEXT_W) {     // wrap
                cur[cl] = 0;
                if (line == lines) {      // page full -> new page
                    b->plen[b->npages] = n - start - 1; b->npages++; if (b->npages >= MAX_PAGES) return;
                    start = n; b->pstart[b->npages] = n; line = 0;
                }
                if (n + cl + 2 >= MAX_TEXT) return;
                memcpy(b->text + n, cur, cl); n += cl; b->text[n++] = '\n'; line++;
                cl = 0; cw = 0;
            }
            if (wl > 0) {
                if (cl > 0) { cur[cl++] = ' '; cw += sp; }
                if (cl + wl < 500) { memcpy(cur + cl, word, wl); cl += wl; cw += ww; }
            }
            if (hardBreak || q >= pe) {
                if (cl > 0 || hardBreak) {
                    if (line == lines) {
                        b->plen[b->npages] = n - start - 1; b->npages++; if (b->npages >= MAX_PAGES) return;
                        start = n; b->pstart[b->npages] = n; line = 0;
                    }
                    if (n + cl + 2 >= MAX_TEXT) return;
                    memcpy(b->text + n, cur, cl); n += cl; b->text[n++] = '\n'; line++;
                    cl = 0; cw = 0;
                }
                if (q >= pe) break;
                q++;   // skip '\n'
            }
        }
        b->plen[b->npages] = n - start > 0 ? n - start - 1 : 0;   // drop the trailing '\n'
        b->npages++;
        p = *pe ? pe + 1 : pe;
    }
    b->text[n] = 0;
}

static void show_page(Box *b, int pg) {
    if (pg < 0 || pg >= b->npages) return;
    b->page = pg; b->shown = 0; b->hidden = false;
    b->state = DS_TYPING; b->speed = b->normalSpeed; b->fast = false;
}

static void start_conversation(int yn, const char *key, const char *sheet) {
    Box *b = &B[yn];
    const char *t = lang_get_or_key(key, sheet);
    layout(b, t, yn ? LINES_YN : LINES_N);
    b->showArrow = b->showStop = false;
    if (b->npages == 0) { b->npages = 1; b->pstart[0] = 0; b->plen[0] = 0; b->text[0] = 0; }
    show_page(b, 0);
    if (yn) {
        FVar *c = varsYN ? fsm_var(varsYN, "Toll Cost") : NULL; b->cost = c ? c->i : 0;
        FVar *r = varsYN ? fsm_var(varsYN, "Requester") : NULL; b->requester = r ? r->go : NULL;
        b->sel = 0;
    }
}

static void hide_text(Box *b) { b->hidden = true; b->shown = 0; if (b->state == DS_TYPING) b->state = DS_NONE; }

static void end_conversation(void) {        // Dialogue Page Control "End Conversation"
    Box *b = &B[0];
    set_use_stop(true);
    if (b->showStop) clip_play(&b->stop, "Dlg Stop Down");
    if (b->showArrow) clip_play(&b->arrow, "Dlg Arrow Down");
    hide_text(b);
    b->state = DS_NONE;
    uo_broadcast("CONVO_FINISH");
}

void dlg_set_vars(Fsm *normal, Fsm *yn) { varsN = normal; varsYN = yn; }

void dlg_init(void) {
    if (!D.loaded) spr_load(&D, "romfs:/gfx/dialog.spr");
    for (int i = 0; i < 2; i++) { memset(&B[i], 0, sizeof B[i]); B[i].scale = 0.75f; B[i].fleurTop.anim = B[i].fleurBot.anim = B[i].arrow.anim = B[i].stop.anim = -1; B[i].hidden = true; }
    B[0].normalSpeed = 65; B[1].normalSpeed = 100;
}

void dlg_box_event(const char *ev) {
    int yn = strstr(ev, " YN") != NULL;
    Box *b = &B[yn];
    if (!strncmp(ev, "BOX UP", 6)) {
        if (b->up) return;
        b->up = true; box_tween(b, 1.0f, 0.5f, 1);
        clip_play(&b->fleurTop, "Dlg Fleur Top Up"); clip_play(&b->fleurBot, "Dlg Fleur Bot Up");
        if (!yn) game_hud_out(true);
    } else if (!strncmp(ev, "BOX DOWN", 8)) {
        if (!b->up) return;
        b->up = false; box_tween(b, 0.75f, 0.3f, 2);
        clip_play(&b->fleurTop, "Dlg Fleur Top Down"); clip_play(&b->fleurBot, "Dlg Fleur Bot Down");
        if (!yn && strcmp(ev, "BOX DOWN NO HUD")) game_hud_out(false);
    }
}

void dlg_text_event(int yn, const char *ev) {
    Box *b = &B[yn];
    if (!yn && (!strcmp(ev, "CONVO CANCEL") || !strcmp(ev, "LEAVING SCENE") || !strcmp(ev, "HERO DAMAGED"))) {
        if (b->state != DS_NONE) end_conversation();
    } else if (yn && !strcmp(ev, "HERO DAMAGED")) { hide_text(b); b->state = DS_NONE; }
}

void dlg_call(int yn, const char *m, const char *a0, const char *a1) {
    Box *b = &B[yn];
    if (!strcmp(m, "StartConversation")) start_conversation(yn, a0, a1);
    else if (!strcmp(m, "ShowNextPage")) { if (b->page + 1 < b->npages) show_page(b, b->page + 1); }
    else if (!strcmp(m, "HideText")) hide_text(b);
    else if (!strcmp(m, "SpeedupTypewriter")) { if (b->state == DS_TYPING && !b->fast) { b->fast = true; b->speed = 200; } }
}

bool gg_challenge_busy(void);
bool dlg_busy(void) { return B[0].up || B[1].up || B[0].state != DS_NONE || B[1].state != DS_NONE || gg_challenge_busy(); }

static bool continue_pressed(void) { return gin.submitP || gin.jumpP || gin.attackP || gin.castP; }

static void update_box(int yn, float dt) {
    Box *b = &B[yn];
    // tween + fade (FadeGroup ~0.25s up, 0.2s down)
    if (b->tweenDur > 0 && b->tweenT < b->tweenDur) {
        b->tweenT += dt; float t = b->tweenT / b->tweenDur; if (t > 1) t = 1;
        float e = b->tweenEase == 1 ? ease_out_back(t) : ease_in_out_sine(t);
        b->scale = b->tweenFrom + (b->tweenTo - b->tweenFrom) * e;
    }
    if (b->up) { b->alpha += dt / 0.25f; if (b->alpha > 1) b->alpha = 1; }
    else { b->alpha -= dt / 0.2f; if (b->alpha < 0) b->alpha = 0; }
    b->fleurTop.t += dt; b->fleurBot.t += dt; b->arrow.t += dt; b->stop.t += dt;
    switch (b->state) {
    case DS_TYPING: {
        int last = b->plen[b->page];
        b->shown += b->speed * dt;
        if (!yn && !b->fast && b->shown > 0.5f && continue_pressed()) { b->fast = true; b->speed = 200; }   // Idle: SPEEDUP
        if (b->shown >= last) {
            b->shown = (float)last; b->speed = b->normalSpeed; b->fast = false;
            if (b->page == b->npages - 1) {
                if (yn) {     // Activate Geo Text?
                    if (b->cost > 0) { b->state = DS_YN_GEO; b->timer = 0.2f; }
                    else { b->state = DS_YN_INPUT; b->sel = 0; }
                } else { b->state = DS_STOP_PAUSE; b->timer = 0; }
            } else if (!yn) {
                b->state = DS_PAGE_END; b->showArrow = true; clip_play(&b->arrow, "Dlg Arrow Up");
            } else b->state = DS_NONE;
        }
        break; }
    case DS_PAGE_END:
        if (continue_pressed()) {
            sfx_play("button", 1);
            clip_play(&b->arrow, "Dlg Arrow Down");
            show_page(b, b->page + 1);
        }
        break;
    case DS_STOP_PAUSE:
        b->timer -= dt;
        if (b->timer <= 0) {
            if (use_stop()) { b->showStop = true; b->showArrow = false; clip_play(&b->stop, "Dlg Stop Up"); }
            else { b->showArrow = true; b->showStop = false; clip_play(&b->arrow, "Dlg Arrow Up"); }
            b->state = DS_CONV_END;
        }
        break;
    case DS_CONV_END:
        if (continue_pressed()) { sfx_play("button", 1); hide_text(b); end_conversation(); }
        break;
    case DS_YN_GEO:
        b->timer -= dt;
        if (b->timer <= 0) { b->state = DS_YN_INPUT; b->sel = 0; }
        break;
    case DS_YN_INPUT: {
        bool enough = b->cost <= 0 || P.geo >= b->cost;
        if (gin.leftP || gin.rightP || gin.upP || gin.downP) { b->sel ^= 1; sfx_play("ui_change_selection", 1); }
        if (gin.submitP || gin.jumpP) {
            if (b->sel == 0 && !enough) { sfx_play("ui_option_click", 1); break; }
            sfx_play(b->sel == 0 ? "ui_button_confirm" : "button", 1);
            hide_text(b);
            if (b->sel == 0) {
                if (b->cost > 0) { game_take_geo(b->cost); b->state = DS_YN_TAKE; b->timer = 1.0f; }
                else { b->state = DS_NONE; if (b->requester) uo_send_event(b->requester, "YES"); }
            } else { b->state = DS_NONE; if (b->requester) uo_send_event(b->requester, "NO"); }
        } else if (gin.cancelP) {
            sfx_play("button", 1); hide_text(b); b->state = DS_NONE;
            if (b->requester) uo_send_event(b->requester, "NO");
        }
        break; }
    case DS_YN_TAKE:
        b->timer -= dt;
        if (b->timer <= 0) { b->state = DS_NONE; if (b->requester) uo_send_event(b->requester, "YES"); }
        break;
    }
}

/* ---------------- prompt markers ---------------- */
typedef struct { UObj *o, *owner; char label[24]; float x, y, a, t; bool up; } Prompt;
static Prompt PR[8];

static void set_label(Prompt *p, const char *label) {
    char key[32]; int k = 0;
    for (const char *s = label && label[0] ? label : "Listen"; *s && k < 31; s++) key[k++] = (*s >= 'a' && *s <= 'z') ? *s - 32 : *s;
    key[k] = 0;
    snprintf(p->label, sizeof p->label, "%s", lang_get_or_key(key, "Prompts"));
}
static Prompt *prompt_of(UObj *o) { for (int i = 0; i < 8; i++) if (PR[i].o == o && o) return &PR[i]; return NULL; }
static Prompt *prompt_new(UObj *o) {
    for (int i = 0; i < 8; i++) if (!PR[i].o) { Prompt *p = &PR[i]; memset(p, 0, sizeof *p); p->o = o; return p; }
    return NULL;
}
// "Arrow Prompt" spawned by inspect FSMs: UP / DOWN / CHANGE LABEL, label in its "Prompt Control" FSM's "Prompt Name"
static void prompt_native(UObj *o, const char *ev) {
    Prompt *p = prompt_of(o); if (!p) return;
    Fsm *f = o->nfsm ? o->fsm[0] : NULL; FVar *ln = f ? fsm_var(f, "Prompt Name") : NULL;
    if (!strcmp(ev, "UP")) { if (ln) set_label(p, ln->s[0] ? ln->s : "Inspect"); p->up = true; p->t = 0; }
    else if (!strcmp(ev, "DOWN")) p->up = false;
    else if (!strcmp(ev, "CHANGE LABEL") && ln) set_label(p, ln->s[0] ? ln->s : "Inspect");
}
UObj *prompt_create(float wx, float wy) {
    UObj *o = uo_alloc_pseudo("Prompt"); if (!o) return NULL;
    Prompt *p = prompt_new(o); if (!p) return o;
    o->lx = wx; o->ly = wy; o->native = prompt_native;
    static const char *n[] = { "Prompt Name" }; static const VType t[] = { V_STRING };
    Fsm *f = fsm_create_vars("Prompt Control", o, n, t, 1);
    o->fsm = realloc(o->fsm, (o->nfsm + 1) * sizeof(Fsm *)); o->fsm[o->nfsm++] = f;
    return o;
}

UObj *prompt_show(UObj *owner, UObj *existing, const char *label, float wx, float wy) {
    Prompt *p = existing ? prompt_of(existing) : NULL;
    if (!p) { UObj *o = existing ? existing : uo_alloc_pseudo("Prompt Marker"); p = o ? (prompt_of(o) ? prompt_of(o) : prompt_new(o)) : NULL; }
    if (!p || !p->o) return NULL;
    p->o->native = prompt_native;
    if (!p->o->parent) { p->o->lx = wx; p->o->ly = wy; }
    set_label(p, label);
    p->owner = owner; p->up = true; p->t = 0;
    return p->o;
}
void prompt_hide(UObj *m) {
    for (int i = 0; i < 8; i++) if (PR[i].o == m && m) { PR[i].up = false; PR[i].owner = NULL; }
}

/* ---------------- NPC title (Area Title Control, NPC branch) ---------------- */
static struct { char sup[48], main[48], sub[48]; float a; bool up; Fsm *pending; int pendFrames; } T;

// Area Title Control starts in "Pause" (NextFrameEvent): the NPC FSM sets "NPC Title" / "Area Event" right after
// activating the object, so the variables are read a frame later
void title_npc(Fsm *at) { T.pending = at; T.pendFrames = 1; }
void title_area(const char *ev, bool visited, bool right);
static void title_npc_now(Fsm *at) {
    FVar *nt = fsm_var(at, "NPC Title");
    FVar *ev = fsm_var(at, "Area Event");
    if (!ev || !ev->s[0]) return;
    if (!nt || !nt->b) {
        // Area Title Control's area branch: a boss fight activates the title with "Visited" and "Display Right" set
        // (FalseyControl's "Music" state: FALSE_KNIGHT), which shows the boss's name in the corner. The object goes
        // inactive again (the title's FSM ends that way) so the next fight's activation shows its name too
        FVar *vis = fsm_var(at, "Visited"), *dr = fsm_var(at, "Display Right");
        title_area(ev->s, vis && vis->b, dr && dr->b);
        if (at->owner) at->owner->activeSelf = false;
        return;
    }
    char k[80];
    snprintf(k, sizeof k, "%s_MAIN", ev->s); snprintf(T.main, sizeof T.main, "%s", lang_get_or_key(k, "Titles"));
    snprintf(k, sizeof k, "%s_SUB", ev->s); snprintf(T.sub, sizeof T.sub, "%s", lang_get_or_key(k, "Titles"));
    snprintf(k, sizeof k, "%s_SUPER", ev->s); snprintf(T.sup, sizeof T.sup, "%s", lang_get_or_key(k, "Titles"));
    if (!lang_get(k, "Titles")) T.sup[0] = 0;
    snprintf(k, sizeof k, "%s_SUB", ev->s); if (!lang_get(k, "Titles")) T.sub[0] = 0;
    T.up = true;
}
void title_event(const char *ev) {
    if (!strcmp(ev, "NPC TITLE DOWN")) T.up = false;
}

static void draw_clip(const Clip *c, float cx, float y, float sc, float a);
/* ---------------- area titles (Area Title Control, AreaTitleController.cs) ---------------- */
// L: the big centred title for a newly discovered area (fleurs, super / main / sub), S: the small corner title on revisits
static struct { int mode; float t, a; bool right; char sup[48], main[48], sub[48]; Clip top, bot; } L;
static void load_title_text(const char *ev) {
    char k[80];
    snprintf(k, sizeof k, "%s_MAIN", ev); snprintf(L.main, sizeof L.main, "%s", lang_get_or_key(k, "Titles"));
    snprintf(k, sizeof k, "%s_SUB", ev); { const char *s = lang_get(k, "Titles"); snprintf(L.sub, sizeof L.sub, "%s", s ? s : ""); }
    snprintf(k, sizeof k, "%s_SUPER", ev); { const char *s = lang_get(k, "Titles"); snprintf(L.sup, sizeof L.sup, "%s", s ? s : ""); }
    for (char *p = L.main; *p; p++) if (*p == '\\' && p[1] == 'n') { *p = ' '; memmove(p + 1, p + 2, strlen(p + 2) + 1); }
}
void title_area(const char *ev, bool visited, bool right) {
    load_title_text(ev);
    L.mode = visited ? 2 : 1; L.t = 0; L.a = 0; L.right = right;
    if (!visited) { clip_play(&L.top, "Ttl Top Appear"); clip_play(&L.bot, "Ttl Bottom Appear"); }
}
bool title_big_showing(void) { return L.mode == 1; }
static void title_area_update(float dt) {
    if (!L.mode) return;
    L.t += dt; L.top.t += dt; L.bot.t += dt;
    if (L.mode == 1) {                     // Titles Up 0.6 s, Fleurs Up (Unvisited Wait ~3 s), Titles Down 2 s
        if (L.t < 0.6f) L.a = L.t / 0.6f;
        else if (L.t < 3.6f) L.a = 1;
        else if (L.t < 5.6f) { if (L.a == 1) { clip_play(&L.top, "Ttl Top Disappear"); clip_play(&L.bot, "Ttl Bottom Disappear"); } L.a = 1 - (L.t - 3.6f) / 2.0f; if (L.a > 0.999f) L.a = 0.999f; }
        else { L.mode = 0; L.a = 0; }
        if (gin.inventoryP) { L.mode = 0; L.a = 0; }
    } else {                               // Visited Appear: 6 s, Visited Disappear 1.5 s
        if (L.t < 0.5f) L.a = L.t / 0.5f;
        else if (L.t < 6.0f) L.a = 1;
        else if (L.t < 7.5f) L.a = 1 - (L.t - 6.0f) / 1.5f;
        else { L.mode = 0; L.a = 0; }
        if (dlg_busy() && L.t < 6.0f) L.t = 6.0f;    // NPC CONVO START -> Quick Disappear
    }
}
static void title_area_draw(void) {
    if (!L.mode || L.a <= 0.01f) return;
    u32 c = C2D_Color32f(1, 1, 1, L.a);
    if (L.mode == 1) {
        float y = 64;
        draw_clip(&L.top, TOP_W / 2, y - 26, 0.8f, L.a);
        if (L.sup[0]) font_draw_centered(&A.fontText, L.sup, TOP_W / 2, y - 6, 1, c);
        font_draw_centered(&A.fontBig, L.main, TOP_W / 2, y + 8, font_fit(&A.fontBig, L.main, TOP_W - 12, 1), c);
        float by = y + 8 + A.fontBig.lineh;
        if (L.sub[0]) { font_draw_centered(&A.fontText, L.sub, TOP_W / 2, by, 1, c); by += A.fontText.lineh; }
        draw_clip(&L.bot, TOP_W / 2, by + 12, 0.8f, L.a);
    } else {
        float w = font_width(&A.fontMenu, L.main) * 1.2f;
        if (L.sup[0]) { float ws = font_width(&A.fontText, L.sup) * 0.9f; if (ws > w) w = ws; }
        float x = L.right ? TOP_W - 14 - w : 14, y = 196;
        if (L.sup[0]) font_draw(&A.fontText, L.sup, x, y - 16, 0.9f, c);
        font_draw(&A.fontMenu, L.main, x, y, 1.2f, c);
        if (L.sub[0]) font_draw(&A.fontText, L.sub, x, y + 20, 0.9f, c);
    }
}

// AreaTitleController: the per-scene controllers (romfs/text/area_titles.txt, tools/area_titles.py)
typedef struct { char room[32], ev[32], door[32]; bool right, onlyRevisit, waitTrig, hasBox; float up, vp, x0, y0, x1, y1; } ATC;
static ATC *atc; static int natc;
static struct { int idx[8]; int n; bool played[8]; float pend[8]; int pendIdx; bool pendVisited; char pendEv[32]; bool pendRight; float pendT; char entry[32]; } AC;
static void atc_load(void) {
    if (atc) return;
    FILE *f = fopen("romfs:/text/area_titles.txt", "rb"); if (!f) { natc = 0; atc = calloc(1, sizeof(ATC)); return; }
    int cap = 256; atc = calloc(cap, sizeof(ATC)); char ln[256];
    while (fgets(ln, sizeof ln, f)) {
        if (natc >= cap) { cap *= 2; atc = realloc(atc, cap * sizeof(ATC)); }
        ATC *a = &atc[natc]; memset(a, 0, sizeof *a);
        int r, rv, wt; char box[96] = "";
        char *tok[9]; int nt = 0; char *s = ln;
        while (nt < 9) { tok[nt++] = s; char *t = strchr(s, '\t'); if (!t) break; *t = 0; s = t + 1; }
        if (nt < 8) continue;
        tok[nt - 1][strcspn(tok[nt - 1], "\r\n")] = 0;
        snprintf(a->room, 32, "%s", tok[0]); snprintf(a->ev, 32, "%s", strcmp(tok[1], "-") ? tok[1] : "");
        r = atoi(tok[2]); snprintf(a->door, 32, "%s", strcmp(tok[3], "-") ? tok[3] : ""); rv = atoi(tok[4]);
        a->up = (float)atof(tok[5]); a->vp = (float)atof(tok[6]); wt = atoi(tok[7]);
        if (nt > 8) snprintf(box, sizeof box, "%s", tok[8]);
        a->right = r; a->onlyRevisit = rv; a->waitTrig = wt;
        a->hasBox = sscanf(box, "%f %f %f %f", &a->x0, &a->y0, &a->x1, &a->y1) == 4;
        natc++;
    }
    fclose(f);
}
// AreaTitleController.areaList: (identifier, areaID, subArea, visitedBool)
static const struct { const char *id; int area; bool sub; const char *vb; } AREAS[] = {
    { "ABYSS", 13, 0, "visitedAbyss" }, { "CROSSROADS", 2, 0, "visitedCrossroads" }, { "DEEPNEST", 9, 0, "visitedDeepnest" },
    { "DIRTMOUTH", 1, 0, "visitedDirtmouth" }, { "EGGTEMPLE", 0, 1, "" }, { "FOG_CANYON", 4, 0, "visitedFogCanyon" },
    { "FUNGUS", 5, 0, "visitedFungus" }, { "GREENPATH", 3, 0, "visitedGreenpath" }, { "HIVE", 11, 0, "visitedHive" },
    { "KINGSPASS", 0, 1, "" }, { "MINES", 8, 0, "visitedMines" }, { "RESTING_GROUNDS", 12, 0, "visitedRestingGrounds" },
    { "ROYAL_GARDENS", 10, 0, "visitedRoyalGardens" }, { "RUINS", 6, 0, "visitedRuins" }, { "SHAMANTEMPLE", 0, 1, "" },
    { "WATERWAYS", 7, 0, "visitedWaterways" }, { "MANTIS_VILLAGE", 0, 1, "" }, { "FUNGUS_CORE", 0, 1, "" }, { "MAGE_TOWER", 0, 1, "" },
    { "FUNGUS_SHAMAN", 0, 1, "" }, { "QUEENS_STATION", 0, 1, "" }, { "KINGS_STATION", 0, 1, "" }, { "BLUE_LAKE", 0, 1, "" },
    { "ACID_LAKE", 0, 1, "" }, { "OUTSKIRTS", 14, 0, "visitedOutskirts" }, { "LOVE_TOWER", 0, 1, "" }, { "SPIDER_VILLAGE", 0, 1, "" },
    { "HEGEMOL_NEST", 0, 1, "" }, { "WHITE_PALACE", 15, 0, "visitedWhitePalace" }, { "COLOSSEUM", 0, 1, "seenColosseumTitle" },
    { "ABYSS_DEEP", 16, 0, "visitedAbyssLower" }, { "CLIFFS", 17, 0, "visitedCliffs" }, { "GODHOME", 18, 0, "visitedGodhome" },
    { "GODSEEKER_WASTE", 0, 1, "" },
};
static void atc_schedule(const ATC *a, bool visited, float pause, const char *ev) {
    AC.pendT = pause; AC.pendVisited = visited; AC.pendRight = a->right; snprintf(AC.pendEv, 32, "%s", ev); AC.pendIdx = 1;
}
static void atc_play(int k) {
    ATC *a = &atc[AC.idx[k]];
    if (AC.played[k]) return;
    AC.played[k] = true;
    if (a->door[0] && strcmp(a->door, AC.entry)) return;
    char ev[32]; snprintf(ev, 32, "%s", a->ev);
    int ai = -1;
    for (int i = 0; i < (int)(sizeof AREAS / sizeof AREAS[0]); i++) if (!strcmp(AREAS[i].id, ev)) { ai = i; break; }
    if (ai < 0) { atc_schedule(a, true, a->vp, ev); return; }                       // unknown: default sub area
    if (!strcmp(ev, "CROSSROADS") && pd_bool("crossroadsInfected")) snprintf(ev, 32, "CROSSROADS_INF");
    if (!strcmp(ev, "KINGSPASS") && !pd_bool("visitedCrossroads")) return;
    bool noPause = !strcmp(ev, "HIVE") || !strcmp(ev, "COLOSSEUM");
    if (!strcmp(ev, "COLOSSEUM")) {
        bool v = pd_bool("seenColosseumTitle"); atc_schedule(a, v, v ? a->vp : a->up, ev);
        if (!v) pd_set_bool("seenColosseumTitle", true);
        return;
    }
    if (AREAS[ai].sub) { atc_schedule(a, true, a->vp, ev); return; }
    int cur = pd_int("currentArea");
    bool vis = pd_bool(AREAS[ai].vb);
    if ((!vis && a->onlyRevisit) || AREAS[ai].area == cur) return;
    atc_schedule(a, vis, noPause ? 0 : (vis ? a->vp : a->up), ev);
    pd_set_int("currentArea", AREAS[ai].area);
    if (!vis) pd_set_bool(AREAS[ai].vb, true);
}
void area_titles_enter(const char *room, const char *entryGate) {
    atc_load();
    AC.n = 0; AC.pendIdx = 0; snprintf(AC.entry, 32, "%s", entryGate ? entryGate : "");
    for (int i = 0; i < natc && AC.n < 8; i++) if (!strcmp(atc[i].room, room)) { AC.played[AC.n] = false; AC.idx[AC.n++] = i; }
    for (int k = 0; k < AC.n; k++) if (!atc[AC.idx[k]].waitTrig) atc_play(k);
}
bool area_titles_has(const char *room) {
    atc_load();
    for (int i = 0; i < natc; i++) if (!strcmp(atc[i].room, room)) return true;
    return false;
}
void area_titles_update(float dt, float hx0, float hy0, float hx1, float hy1) {
    for (int k = 0; k < AC.n; k++) {
        ATC *a = &atc[AC.idx[k]];
        if (AC.played[k] || !a->waitTrig || !a->hasBox) continue;
        if (hx1 > a->x0 && hx0 < a->x1 && hy1 > a->y0 && hy0 < a->y1) atc_play(k);
    }
    if (AC.pendIdx) { AC.pendT -= dt; if (AC.pendT <= 0) { AC.pendIdx = 0; title_area(AC.pendEv, AC.pendVisited, AC.pendRight); } }
    title_area_update(dt);
}

/* ---------------- frame ---------------- */
void dlg_reset(void) {
    if (B[0].state != DS_NONE) { set_use_stop(true); B[0].state = DS_NONE; }
    for (int i = 0; i < 2; i++) { B[i].up = false; B[i].alpha = 0; B[i].scale = 0.75f; B[i].state = DS_NONE; B[i].hidden = true; B[i].showArrow = B[i].showStop = false; }
    memset(PR, 0, sizeof PR);
    T.up = false; T.a = 0; T.pending = NULL;
    L.mode = 0; L.a = 0; AC.pendIdx = 0;
    game_hud_out(false);
}

static struct { char text[256]; float t; } DM;      // the Enemy Dream Msg line
// Hall of Gods challenge prompt (BossChallengeUI, simplified to Attuned)
static struct { bool up; Fsm *f; FsmAct *a; char name[64], desc[256], ev[32]; float t; int tier; } GGC;
void gg_challenge_set_tier(int tier) { GGC.tier = tier; }
void gg_challenge_open(Fsm *f, FsmAct *a, const char *ns, const char *nk, const char *ds, const char *dk, const char *ev) {
    GGC.up = true; GGC.f = f; GGC.a = a; GGC.t = 0; GGC.tier = 0;
    const char *n = nk && nk[0] ? lang_get(nk, ns && ns[0] ? ns : "CP3") : NULL, *d = dk && dk[0] ? lang_get(dk, ds && ds[0] ? ds : "CP3") : NULL;
    snprintf(GGC.name, sizeof GGC.name, "%s", n ? n : (nk ? nk : ""));
    snprintf(GGC.desc, sizeof GGC.desc, "%s", d ? d : "");
    for (char *p = GGC.desc; *p; p++) if (*p == '\n') *p = ' ';
    snprintf(GGC.ev, sizeof GGC.ev, "%s", ev ? ev : "");
    sfx_play("ui_open", 0.8f);
}
bool gg_challenge_busy(void) { return GGC.up; }
static void gg_challenge_update(float dt) {
    if (!GGC.up) return;
    GGC.t += dt; if (GGC.t < 0.3f) return;
    if (gin.submitP || gin.jumpP) { GGC.up = false; sfx_play("ui_button_confirm", 1);
        if (GGC.tier) { extern void gg_pantheon_start(int tier); gg_pantheon_start(GGC.tier); }
        fsm_event(GGC.f, GGC.ev); }
    else if (gin.cancelP || gin.attackP) { GGC.up = false; sfx_play("ui_button_cancel", 1); fsm_finish(GGC.f, GGC.a); }
}
static void gg_challenge_draw(void) {
    if (!GGC.up) return;
    C2D_DrawRectSolid(40, 40, 0.5f, 320, 160, C2D_Color32(0, 0, 0, 220));
    font_draw_centered(&A.fontMenu, GGC.name, TOP_W / 2, 52, 1.2f, C2D_Color32(240, 240, 240, 255));
    // description, wrapped to ~280 px
    char buf[256]; snprintf(buf, sizeof buf, "%s", GGC.desc);
    char *w = buf; float y = 86; int lines = 0;
    while (*w && lines < 5) {
        char line[128]; int n = 0, lastSp = -1; while (w[n] && n < 127) { line[n] = w[n]; line[n + 1] = 0; if (w[n] == ' ') lastSp = n; if (font_width(&A.fontText, line) * 0.9f > 280) break; n++; }
        if (w[n] && lastSp > 0) n = lastSp;
        memcpy(line, w, n); line[n] = 0;
        font_draw_centered(&A.fontText, line, TOP_W / 2, y, 0.9f, C2D_Color32(200, 200, 210, 255));
        w += n; while (*w == ' ') w++; y += 15; lines++;
    }
    font_draw_centered(&A.fontText, GGC.tier ? TR("A  Begin the pantheon      B  Leave") : TR("A  Attuned challenge      B  Leave"), TOP_W / 2, 178, 0.9f, C2D_Color32(230, 220, 160, 255));
}

void dlg_update(float dt) {
    update_box(0, dt); update_box(1, dt);
    if (DM.t > 0) DM.t -= dt;
    gg_challenge_update(dt);
    for (int i = 0; i < 8; i++) {
        Prompt *p = &PR[i]; if (!p->o) continue;
        p->t += dt;
        if (p->up && p->owner && !uo_active(p->owner)) p->up = false;     // PromptMarker.Update: owner gone -> Hide
        if (p->up) { p->a += dt / 0.25f; if (p->a > 1) p->a = 1; }
        else { p->a -= dt / 0.25f; if (p->a <= 0) { p->a = 0; } }
    }
    if (T.pending && T.pendFrames-- <= 0) { Fsm *f = T.pending; T.pending = NULL; title_npc_now(f); }
    if (T.up) { T.a += dt / 0.5f; if (T.a > 1) T.a = 1; }
    else { T.a -= dt / 0.25f; if (T.a < 0) T.a = 0; }
}

void dlg_draw_world(float camx, float camy) {
    for (int i = 0; i < 8; i++) {
        Prompt *p = &PR[i]; if (!p->o || p->a <= 0 || p->o->destroyed) continue;
        float sx = (uo_wx(p->o) - camx) * PPU + TOP_W / 2, sy = (camy - uo_wy(p->o)) * PPU + TOP_H / 2;
        float rise = (1 - p->a) * 6;
        u32 c = C2D_Color32f(1, 1, 1, p->a);
        font_draw_centered(&A.fontMenu, p->label, sx, sy - 18 + rise, 1, c);
        int af = spr_anim(&D, "Dlg Arrow Up");
        if (af >= 0) {
            const Anim *an = &D.anims[af];
            int fr = D.animFrames[an->first + (an->count - 1)];
            const Frame *f = &D.frames[fr];
            spr_draw(&D, fr, sx - f->w * 0.35f, sy - 26 + rise - f->h * 0.7f, 0.7f, -0.7f, p->a);   // small up arrow above the label
        }
    }
}

static void draw_clip(const Clip *c, float cx, float y, float sc, float a) {
    int fr = clip_frame(c); if (fr < 0) return;
    spr_draw(&D, fr, cx, y, sc, sc, a);
}

static void draw_box(int yn) {
    Box *b = &B[yn];
    if (b->alpha <= 0.01f && b->hidden) return;
    float a = b->alpha, s = b->scale;
    float cy = yn ? 128 : 62;                 // box centre on the top screen
    float lh = A.fontText.lineh;
    int lines = LINES_N;
    float extra = 0;                          // yes/no: geo cost row + the Yes / No row under the text
    if (yn) {
        lines = 1;
        if (b->npages > 0) { const char *t = b->text + b->pstart[b->page]; for (int k = 0; k < b->plen[b->page]; k++) if (t[k] == '\n') lines++; }
        extra = (b->cost > 0 ? lh : 0) + lh + 6;
    }
    float h = lines * lh + extra;
    // soft backing so the text reads on the small screen
    if (a > 0.01f) for (int k = 0; k < 4; k++) {
        float ix = 20 + k * 6, iy = 4 + k * 3;
        C2D_DrawRectSolid(200 - (180 - ix) * s, cy - (h / 2 + 26 - iy) * s, 0.5f, (360 - 2 * ix) * s, (h + 52 - 2 * iy) * s, C2D_Color32f(0, 0, 0, a * 0.2f));
    }
    draw_clip(&b->fleurTop, 200, cy - (h / 2 + 14) * s, s, a);
    draw_clip(&b->fleurBot, 200, cy + (h / 2 + 14) * s, s, a);
    if (!b->hidden && b->state != DS_NONE && b->npages > 0) {
        char buf[MAX_TEXT]; int st = b->pstart[b->page], n = (int)b->shown;
        if (n > b->plen[b->page]) n = b->plen[b->page];
        memcpy(buf, b->text + st, n); buf[n] = 0;
        if (!yn && !b->up && b->alpha <= 0.01f) {
            // no box (lore tablets, Inspect texts): the game darkens the screen and centres the text (TopCentre)
            C2D_DrawRectSolid(0, 0, 0.45f, TOP_W, TOP_H, C2D_Color32f(0, 0, 0, 0.6f));
            float y = cy - h / 2 - 2; char *ln = buf;
            while (ln && *ln) { char *nl = strchr(ln, '\n'); if (nl) *nl = 0;
                font_draw_centered(&A.fontText, ln, TOP_W / 2, y, 1, C2D_Color32f(1, 1, 1, 1)); y += lh;
                ln = nl ? nl + 1 : NULL; }
        } else
        // TextMeshPro TopLeft alignment
        font_draw(&A.fontText, buf, TEXT_X, cy - h / 2 - 2, 1, C2D_Color32f(1, 1, 1, a));
    }
    if (!yn) {
        if (b->showArrow) draw_clip(&b->arrow, 200, cy + h / 2 + 10, 1, a);
        if (b->showStop) draw_clip(&b->stop, 200, cy + h / 2 + 10, 1, a);
    } else if (b->state == DS_YN_GEO || b->state == DS_YN_INPUT) {
        float y = cy - h / 2 - 2 + lines * lh + 2;
        if (b->cost > 0) {   // Geo Text: the cost with the geo icon
            char cs[16]; snprintf(cs, sizeof cs, "%d", b->cost);
            int ci = spr_anim(&A.hud, "Coin Idle");
            float w = font_width(&A.fontText, cs);
            if (ci >= 0) spr_draw(&A.hud, A.hud.animFrames[A.hud.anims[ci].first], 200 - w / 2 - 10, y + 8, 0.8f, 0.8f, a);
            font_draw(&A.fontText, cs, 200 - w / 2 + 2, y, 1, C2D_Color32f(1, 1, 1, a));
            y += lh;
        }
        if (b->state == DS_YN_INPUT) {
            bool enough = b->cost <= 0 || P.geo >= b->cost;
            const char *yes = enough ? lang_get_or_key("YES", "Prompts") : lang_get_or_key("NOT_ENOUGH_GEO", "Prompts");
            const char *no = lang_get_or_key("NO", "Prompts");
            float yx = 140, nx = 270, oy = y + 2;
            font_draw_centered(&A.fontMenu, yes, yx, oy, 1, enough ? C2D_Color32f(1, 1, 1, a * (b->sel == 0 ? 1 : 0.6f)) : C2D_Color32f(0.6f, 0.6f, 0.6f, a));
            font_draw_centered(&A.fontMenu, no, nx, oy, 1, C2D_Color32f(1, 1, 1, a * (b->sel == 1 ? 1 : 0.6f)));
            float px = b->sel == 0 ? yx : nx; const char *lbl = b->sel == 0 ? yes : no;
            float w = font_width(&A.fontMenu, lbl);
            int pa = spr_anim(&A.ui, "pointer");
            if (pa >= 0) {
                int fr = A.ui.animFrames[A.ui.anims[pa].first + A.ui.anims[pa].count - 1];
                spr_draw(&A.ui, fr, px - w / 2 - 4, oy + 8, 1, 1, a);
                spr_draw(&A.ui, fr, px + w / 2 + 4, oy + 8, -1, 1, a);
            }
        }
    }
}

// "Enemy Dream Msg" (EnemyDreamnailReaction / npc_dream_dialogue): a line of the creature's thoughts, top of the screen
void dlg_dream_msg(const char *title, int amount) {
    if (!title || !title[0]) return;
    char key[96]; snprintf(key, sizeof key, "%s_%d", title, amount > 1 ? 1 + rand() % amount : 1);
    const char *t = lang_get(key, "Enemy Dreams");
    if (!t) { snprintf(key, sizeof key, "%s", title); t = lang_get(key, "Enemy Dreams"); }
    if (!t) return;
    snprintf(DM.text, sizeof DM.text, "%s", t);
    for (char *p = DM.text; *p; p++) if (*p == '\n' || *p == '<') { if (*p == '<') { char *e = strchr(p, '>'); if (e) { memmove(p, e + 1, strlen(e)); p--; continue; } } *p = ' '; }
    DM.t = 3.5f;
}
static void dream_msg_draw(void) {
    if (DM.t <= 0) return;
    float a = DM.t > 3.0f ? (3.5f - DM.t) / 0.5f : DM.t < 0.6f ? DM.t / 0.6f : 1;
    u32 c = C2D_Color32f(1, 1, 1, a);
    // wrap to two lines of about 330 px
    char l1[256], *l2 = NULL; snprintf(l1, sizeof l1, "%s", DM.text);
    if (font_width(&A.fontText, l1) * 0.95f > 330) {
        int n = strlen(l1), best = -1;
        for (int i = n / 2, d = 0; d < n / 2; d++) { if (i + d < n && l1[i + d] == ' ') { best = i + d; break; } if (i - d > 0 && l1[i - d] == ' ') { best = i - d; break; } }
        if (best > 0) { l1[best] = 0; l2 = l1 + best + 1; }
    }
    font_draw_centered(&A.fontText, l1, TOP_W / 2, 52, 0.95f, c);
    if (l2) font_draw_centered(&A.fontText, l2, TOP_W / 2, 68, 0.95f, c);
}
void dlg_draw_hud(void) {
    title_area_draw();
    dream_msg_draw();
    gg_challenge_draw();
    if (T.a > 0.01f) {
        float x = 14, y = 186;
        u32 c = C2D_Color32f(1, 1, 1, T.a);
        if (T.sup[0]) font_draw(&A.fontText, T.sup, x, y - 16, 0.9f, c);
        font_draw(&A.fontMenu, T.main, x, y, 1.35f, c);
        if (T.sub[0]) font_draw(&A.fontText, T.sub, x, y + 22, 0.9f, c);
    }
    draw_box(0);
    draw_box(1);
}
