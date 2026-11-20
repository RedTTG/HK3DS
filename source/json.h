// Minimal JSON DOM reader (arena allocated) for the converted object/FSM files.
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ } JType;

typedef struct JVal JVal;
struct JVal {
    JType t;
    int n;                 // children count (arrays/objects)
    double num;
    const char *s;         // string value
    const char *key;       // key when inside an object
    JVal *child, *next;    // first child / next sibling
    int cache;             // FSM runtime: resolved variable index + 1 for {"var": name} nodes (0 unknown)
};

typedef struct { char *mem; size_t used, cap; } JArena;

JVal *json_parse(JArena *a, const char *text, size_t len);   // NULL on error
void json_free(JArena *a);
JVal *jget(const JVal *o, const char *key);                 // object member (NULL if missing)
JVal *jat(const JVal *a, int i);                             // array element
double jnum(const JVal *v, double def);
int jint(const JVal *v, int def);
bool jbool(const JVal *v, bool def);
const char *jstr(const JVal *v, const char *def);
static inline bool jis(const JVal *v, JType t) { return v && v->t == t; }
