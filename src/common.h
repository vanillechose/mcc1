#ifndef COMMON_H
#define COMMON_H

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNUSED __attribute__((unused))

#define say(fh, fmt, ...) \
    fprintf(fh, fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#define trace(fmt, ...) \
    say(stderr, "[%s]: " fmt, __func__ __VA_OPT__(,) __VA_ARGS__)
#define die(...) \
    ({ trace("FATAL: " __VA_ARGS__); exit(1); })

#define NELEMS(arr) (sizeof(arr) / sizeof(arr[0]))

#define BIT(n) (1ull << (n))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) > (b) ? (b) : (a))

inline static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if(!p)
        die("out of memory");
    return p;
}

inline static void *xcalloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if(!p)
        die("out of memory");
    return p;
}

inline static void *xreallocarray(void *p, size_t n, size_t size) {
    p = reallocarray(p, n, size);
    if(!p)
        die("out of memory");
    return p;
}

typedef struct vec {
    size_t len;
    size_t cap;
    /* element size */
    size_t ssz;

    void *alloc;
} Vec;

void vec_reserve(Vec *v, size_t additional);

/*
 * Pointers returned by these functions remain
 * valid until the next call to vec_push, pop,
 * reserve or fill, and are obviously invalidated by
 * free(v->alloc).
 *
 * vec_get returns NULL if index is out of bounds.
 */
void *vec_push_uninit(Vec *v);
void *vec_push(Vec *v, void *src);
void *vec_pop(Vec *v);
void *vec_get(Vec *v, size_t index);
void *vec_fill(Vec *v, size_t count);

#define vec_of(T) ((struct vec) { .ssz = sizeof(T) })

#define vec_last(v) \
    vec_get((v), (v)->len ? (v)->len - 1 : (v)->len)

/*
 * requires elem to be an element of v
 */
size_t vec_index_of(Vec *v, void *elem);

/*
 * vec_next (resp. vec_prev) returns:
 *     - if it is NULL, v[0] (resp. v[len - 1])
 *     - if it points to element i and j = i + 1 (resp. i - 1)
 *       is a valid index, v[j]
 *     - otherwise NULL
 */
void *vec_next(Vec *v, void *it);
void *vec_prev(Vec *v, void *it);

#endif /* COMMON_H */
