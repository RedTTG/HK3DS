// Minimal JSON DOM reader (arena allocated) for the converted object/FSM files.
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
// 32 bytes on the 3DS (it was 40): a room's objects are a few hundred thousand of these, and on an Old 3DS the
// biggest rooms (False Knight's arena) ran out of memory before their boss scene could load
struct JVal {
    unsigned t : 4;        // JType
    unsigned n : 28;       // children count (arrays/objects)
    int cache;             // FSM runtime: resolved variable index + 1 for {"var": name} nodes (0 unknown)
    double num;
    const char *s;         // string value (read-only: equal strings of one file share their storage)
    const char *key;       // key when inside an object (shared the same way)
    JVal *child, *next;    // first child / next sibling
};

// itab: while a file is parsed, its strings so far (each distinct key or value is stored once)
typedef struct { char *mem; size_t used, cap; const char **itab; unsigned icap, icount; } JArena;

JVal *json_parse(JArena *a, const char *text, size_t len);   // NULL on error
void json_free(JArena *a);
JVal *jget(const JVal *o, const char *key);                 // object member (NULL if missing)
JVal *jat(const JVal *a, int i);                             // array element
double jnum(const JVal *v, double def);
int jint(const JVal *v, int def);
bool jbool(const JVal *v, bool def);
const char *jstr(const JVal *v, const char *def);
static inline bool jis(const JVal *v, JType t) { return v && v->t == t; }
