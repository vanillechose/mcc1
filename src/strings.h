#ifndef STRINGS_H
#define STRINGS_H

#include "common.h"

typedef struct string {
    const char *lo;
    const char *hi;
    unsigned hash;
    struct string *link;
} string;

#define FMT(s) (int)((s)->hi - (s)->lo), (s)->lo

string *getkey0(const char *cstr);
string *getkey(const char *lo, const char *hi);

struct table {
    size_t len;
    size_t cap;
    void **buckets;
};

uintptr_t *table_put(struct table *tb, string *key, uintptr_t val);
uintptr_t *table_find(struct table *tb, string *key);

void table_free(struct table *tb);

#endif /* STRINGS_H */
