#include "strings.h"

#define FNV_PRIME  0x01000193
#define FNV_OFFSET 0x811C9DC5

static unsigned hash(const char *lo, size_t len) {
    unsigned ret = FNV_OFFSET;

    for(const char *s = lo; s != lo + len; s += 1) {
        ret *= FNV_PRIME;
        ret ^= *s;
    }

    return ret;
}

static struct table allkeys;

static void itable(struct table *tb) {
#   define BASESIZE 32
    *tb = (struct table) {
        .len = 0,
        .cap = BASESIZE,
        .buckets = xcalloc(BASESIZE, sizeof(void *)),
    };
    memset(tb->buckets, 0, BASESIZE * sizeof(void *));
#   undef BASESIZE
}

string *getkey0(const char *cstr) {
    return getkey(cstr, cstr + strlen(cstr));
}

string *getkey(const char *lo, const char *hi) {
    if(!allkeys.buckets)
        itable(&allkeys);

    unsigned h = hash(lo, hi - lo);

    void **bkt = &allkeys.buckets[h & (allkeys.cap - 1)];
    string *it = *bkt;
    for(; it; it = it->link) {
        if(h == it->hash && hi - lo == it->hi - it->lo
                && !strncmp(lo, it->lo, hi - lo))
            break;
    }

    if(!it) {
        it = xmalloc(sizeof(string));
        *it = (string) { lo, hi, h, *bkt };
        *bkt = it;
    }

    if(allkeys.len >= allkeys.cap / 2) {
        size_t newcap = allkeys.cap * 2;
        void **to = xcalloc(newcap, sizeof to[0]);
        for(size_t i = 0; i < allkeys.cap; i += 1) {
            string *it = allkeys.buckets[i], *next = NULL;

            for(; it; it = next) {
                next = it->link;
                it->link = to[it->hash & (newcap - 1)];
                to[it->hash & (newcap - 1)] = it;
            }
        }
        free(allkeys.buckets);
        allkeys.cap = newcap;
        allkeys.buckets = to;
    }

    return it;
}

struct entry {
    string *key;
    uintptr_t val;
    struct entry *link;
};

uintptr_t *table_put(struct table *tb, string *key, uintptr_t val) {
    if(!tb->buckets)
        itable(tb);

    unsigned h = key->hash;

    void **bkt = &tb->buckets[h & (tb->cap - 1)];
    struct entry *it = *bkt;
    for(; it; it = it->link) {
        if(key == it->key)
            break;
    }

    if(!it) {
        it = xmalloc(sizeof *it);
        *it = (struct entry) { key, val, *bkt };
        *bkt = it;
    }

    if(tb->len >= tb->cap / 2) {
        size_t newcap = tb->cap * 2;
        void **to = xcalloc(newcap, sizeof(void *));
        memset(to, 0, newcap * sizeof to[0]);
        for(size_t i = 0; i < tb->cap; i += 1) {
            string *it = tb->buckets[i], *next = NULL;

            for(; it; it = next) {
                next = it->link;
                it->link = to[it->hash & (newcap - 1)];
                to[it->hash & (newcap - 1)] = it;
            }
        }
        free(tb->buckets);
        tb->cap = newcap;
        tb->buckets = to;
    }

    it->val = val;
    return &it->val;
}

uintptr_t *table_find(struct table *tb, string *key) {
    if(!tb->buckets)
        itable(tb);

    unsigned h = key->hash;

    struct entry *it = tb->buckets[h & (tb->cap - 1)];
    for(; it; it = it->link) {
        if(key == it->key)
            break;
    }

    return it ? &it->val : NULL;
}

void table_free(struct table *tb) {
    for(size_t i = 0; i < tb->cap; i += 1) {
        struct entry *it = tb->buckets[i], *next;
        while(it) {
            next = it->link;
            free(it);
            it = next;
        }
    }
}
