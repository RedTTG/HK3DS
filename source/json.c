#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void *aalloc(JArena *a, size_t n) {
    n = (n + 7) & ~(size_t)7;
    if (a->used + n > a->cap) {
        // chain a new block (we never free individual nodes; keep it simple: grow by reallocating is unsafe for
        // pointers, so allocate blocks and link them through the first word)
        size_t bs = n > 256 * 1024 ? n + 16 : 256 * 1024;
        char *blk = malloc(bs + 16);
        if (!blk) return NULL;
        *(char **)blk = a->mem;     // previous block
        a->mem = blk; a->used = 16; a->cap = bs + 16;
    }
    void *p = a->mem + a->used; a->used += n;
    return p;
}

void json_free(JArena *a) {
    char *b = a->mem;
    while (b) { char *prev = *(char **)b; free(b); b = prev; }
    a->mem = NULL; a->used = a->cap = 0;
    free(a->itab); a->itab = NULL; a->icap = a->icount = 0;
}

// string interning: keys ("type", "var", "enabled"...) and values repeat thousands of times in a room's file
static unsigned shash(const char *t, size_t n) { unsigned h = 2166136261u; while (n--) h = (h ^ (unsigned char)*t++) * 16777619u; return h; }
static bool itab_grow(JArena *a) {
    unsigned nc = a->icap ? a->icap * 2 : 4096;
    const char **nt = calloc(nc, sizeof *nt); if (!nt) return false;
    for (unsigned i = 0; i < a->icap; i++) if (a->itab[i]) {
        unsigned h = shash(a->itab[i], strlen(a->itab[i])) & (nc - 1);
        while (nt[h]) h = (h + 1) & (nc - 1);
        nt[h] = a->itab[i];
    }
    free(a->itab); a->itab = nt; a->icap = nc;
    return true;
}
static const char *intern(JArena *a, const char *t, size_t n) {
    if ((a->icount + 1) * 4 > a->icap * 3 && !itab_grow(a)) {     // no memory for the table: just store it
        char *o = aalloc(a, n + 1); if (!o) return NULL; memcpy(o, t, n); o[n] = 0; return o;
    }
    unsigned h = shash(t, n) & (a->icap - 1);
    for (const char *e; (e = a->itab[h]); h = (h + 1) & (a->icap - 1))
        if (!strncmp(e, t, n) && !e[n]) return e;
    char *o = aalloc(a, n + 1); if (!o) return NULL;
    memcpy(o, t, n); o[n] = 0;
    a->itab[h] = o; a->icount++;
    return o;
}

typedef struct { const char *p, *e; JArena *a; } P;

static void ws(P *s) { while (s->p < s->e && (*s->p == ' ' || *s->p == '\n' || *s->p == '\r' || *s->p == '\t')) s->p++; }

static const char *pstring(P *s) {
    // s->p at opening quote
    s->p++;
    const char *st = s->p;
    size_t extra = 0;
    while (s->p < s->e && *s->p != '"') { if (*s->p == '\\') { s->p++; extra++; } s->p++; }
    size_t n = s->p - st;
    char tmp[256];
    bool shared = n < sizeof tmp;                                           // short strings are shared (intern)
    char *own = shared ? NULL : aalloc(s->a, n + 1);                         // long ones get their own copy
    char *out = shared ? tmp : own, *o = out;
    if (!out) return "";
    for (const char *q = st; q < s->p; q++) {
        if (*q == '\\' && q + 1 < s->p) {
            q++;
            switch (*q) {
                case 'n': *o++ = '\n'; break; case 't': *o++ = '\t'; break; case 'r': *o++ = '\r'; break;
                case 'b': *o++ = '\b'; break; case 'f': *o++ = '\f'; break;
                case 'u': {   // basic \uXXXX -> UTF-8
                    unsigned c = 0; for (int k = 0; k < 4 && q + 1 < s->p; k++) { q++; c = c * 16 + (*q >= 'a' ? *q - 'a' + 10 : *q >= 'A' ? *q - 'A' + 10 : *q - '0'); }
                    if (c < 0x80) *o++ = (char)c; else if (c < 0x800) { *o++ = 0xC0 | (c >> 6); *o++ = 0x80 | (c & 63); }
                    else { *o++ = 0xE0 | (c >> 12); *o++ = 0x80 | ((c >> 6) & 63); *o++ = 0x80 | (c & 63); }
                    break; }
                default: *o++ = *q;
            }
        } else *o++ = *q;
    }
    *o = 0; (void)extra;
    if (s->p < s->e) s->p++;
    if (shared) { const char *r = intern(s->a, tmp, (size_t)(o - tmp)); return r ? r : ""; }
    return own;
}

static JVal *pvalue(P *s);

static JVal *newv(P *s, JType t) { JVal *v = aalloc(s->a, sizeof(JVal)); if (v) { memset(v, 0, sizeof *v); v->t = t; } return v; }

static JVal *pvalue(P *s) {
    ws(s);
    if (s->p >= s->e) return NULL;
    char c = *s->p;
    if (c == '{' || c == '[') {
        bool obj = c == '{';
        JVal *v = newv(s, obj ? J_OBJ : J_ARR); if (!v) return NULL;
        s->p++;
        JVal **tail = &v->child;
        ws(s);
        if (s->p < s->e && *s->p == (obj ? '}' : ']')) { s->p++; return v; }
        while (s->p < s->e) {
            const char *key = NULL;
            if (obj) { ws(s); if (*s->p != '"') return NULL; key = pstring(s); ws(s); if (*s->p != ':') return NULL; s->p++; }
            JVal *ch = pvalue(s); if (!ch) return NULL;
            ch->key = key; *tail = ch; tail = &ch->next; v->n++;
            ws(s);
            if (s->p < s->e && *s->p == ',') { s->p++; continue; }
            if (s->p < s->e && *s->p == (obj ? '}' : ']')) { s->p++; return v; }
            return NULL;
        }
        return NULL;
    }
    if (c == '"') { JVal *v = newv(s, J_STR); if (v) v->s = pstring(s); return v; }
    if (!strncmp(s->p, "true", 4)) { s->p += 4; return newv(s, J_TRUE); }
    if (!strncmp(s->p, "false", 5)) { s->p += 5; return newv(s, J_FALSE); }
    if (!strncmp(s->p, "null", 4)) { s->p += 4; return newv(s, J_NULL); }
    if (!strncmp(s->p, "NaN", 3)) { s->p += 3; JVal *v = newv(s, J_NUM); return v; }
    if (!strncmp(s->p, "Infinity", 8)) { s->p += 8; JVal *v = newv(s, J_NUM); if (v) v->num = 1e30; return v; }
    if (!strncmp(s->p, "-Infinity", 9)) { s->p += 9; JVal *v = newv(s, J_NUM); if (v) v->num = -1e30; return v; }
    JVal *v = newv(s, J_NUM); if (!v) return NULL;
    char *end; v->num = strtod(s->p, &end);
    if (end == s->p) return NULL;
    s->p = end;
    return v;
}

JVal *json_parse(JArena *a, const char *text, size_t len) {
    P s = { text, text + len, a };
    JVal *r = pvalue(&s);
    free(a->itab); a->itab = NULL; a->icap = a->icount = 0;       // the strings stay; only the lookup table goes
    return r;
}

JVal *jget(const JVal *o, const char *key) {
    if (!o || o->t != J_OBJ) return NULL;
    for (JVal *c = o->child; c; c = c->next) if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}
JVal *jat(const JVal *a, int i) {
    if (!a || a->t != J_ARR) return NULL;
    JVal *c = a->child; while (c && i-- > 0) c = c->next;
    return c;
}
double jnum(const JVal *v, double def) { if (!v) return def; if (v->t == J_NUM) return v->num; if (v->t == J_TRUE) return 1; if (v->t == J_FALSE) return 0; return def; }
int jint(const JVal *v, int def) { return (int)jnum(v, def); }
bool jbool(const JVal *v, bool def) { if (!v) return def; if (v->t == J_TRUE) return true; if (v->t == J_FALSE) return false; if (v->t == J_NUM) return v->num != 0; return def; }
const char *jstr(const JVal *v, const char *def) { return v && v->t == J_STR ? v->s : def; }
