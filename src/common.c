#include "common.h"

/* {{{ Vec */

inline static void vec_grow(Vec *v, size_t new_cap) {
    assert(v->len <= new_cap);
    v->alloc = xreallocarray(v->alloc, new_cap, v->ssz);
    v->cap = new_cap;
}

void vec_reserve(Vec *v, size_t additional) {
    size_t cap = v->cap;

    while(v->len + additional > cap)
        cap = cap ? 2 * cap : additional;

    vec_grow(v, cap);
}

inline static void *vec_get_unchecked(Vec *v, size_t i) {
    return (void *)((uintptr_t)v->alloc + i * v->ssz);
}

void *vec_push_uninit(Vec *v) {
    vec_reserve(v, 1);
    return vec_get_unchecked(v, v->len++);
}

void *vec_push(Vec *v, void *src) {
    void *dst = vec_push_uninit(v);
    return memcpy(dst, src, v->ssz);
}

void *vec_pop(Vec *v) {
    return v->len ? vec_get_unchecked(v, --v->len) : NULL;
}

void *vec_get(Vec *v, size_t index) {
    return v->len > index ? vec_get_unchecked(v, index) : NULL;
}

void *vec_fill(Vec *v, size_t count) {
    vec_reserve(v, count);
    void *s = vec_get_unchecked(v, v->len);
    void *e = vec_get_unchecked(v, v->len + count);
    v->len += count;
    memset(s, 0, e - s);
    return s;
}

size_t vec_index_of(Vec *v, void *elem) {
    ptrdiff_t offset = elem - v->alloc;
    assert(offset >= 0
            && (size_t)offset / v->ssz < v->len
            && (size_t)offset % v->ssz == 0);
    return offset / v->ssz;
}

void *vec_next(Vec *v, void *it) {
    if(!it)
        return vec_get(v, 0);

    return vec_get(v, vec_index_of(v, it) + 1);
}

void *vec_prev(Vec *v, void *it) {
    if(!it)
        return vec_last(v);

    size_t index = vec_index_of(v, it);
    if(index > 0)
        return vec_get_unchecked(v, index - 1);
    return NULL;
}

/* }}} */
