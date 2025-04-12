#include "common.h"
#include "source.h"

struct line {
    unsigned lo;
    unsigned hi;
    unsigned number;
};

struct vec line_map = vec_of(struct line);

static void make_line_map(void) {
    struct line line = { 0, 0, 1 };
    struct line *prev = vec_push(&line_map, &line);

    const char *p = source;

    while((p = strchr(p, '\n'))) {
        p += 1;
        unsigned off = p - source;

        prev->hi = off;
        line.lo = off;
        line.number += 1;
        prev = vec_push(&line_map, &line);
    }

    prev->hi = strlen(source) + 1; /* include terminating '\0' */
}

const char *filename;

static struct line *get_source_line(unsigned off) {
    struct line *it = NULL;
    while((it = vec_next(&line_map, it))) {
        if(it->lo <= off && off < it->hi)
            return it;
    }
    return NULL;
}

unsigned get_line_number(struct loc loc) {
    struct line *line = get_source_line(loc.lo);
    assert(line);
    return line->number;
}

#define RED     "31"
#define GREEN   "32"
#define MAGENTA "35"
#define CYAN    "36"

#ifndef NO_COLORS
#   define COLOR(code, s) "\x1B[" code "m" s "\x1B[0m"
#else
#   define COLOR(_, s) s
#endif

static void emitdiag(struct loc at, const char *label, const char *fmt, va_list ap) {
    struct line *line = get_source_line(at.lo);
    assert(line);

    unsigned col = at.lo - line->lo;
    unsigned span = MIN(line->hi, at.hi) - at.lo;

    fprintf(stderr, "[%s in %s]: ", label, filename);

    vfprintf(stderr, fmt, ap);

    fprintf(stderr, "\n");

    /* line  | source */
    unsigned numwidth = fprintf(stderr, " %u ", line->number);
    fprintf(stderr, "| %.*s\n",
            line->hi - line->lo - 1, &source[line->lo]);
    /* blank | cursor */
    fprintf(stderr, "%*s| %*s^", numwidth, "", col, "");

    for(unsigned i = 1; i < (span?:1); i += 1)
        fputc('-', stderr);
    fprintf(stderr, "\n");
}

void emitwarn(struct loc at, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitdiag(at, COLOR(MAGENTA, "warning"), fmt, ap);
    va_end(ap);
}

void emiterror(struct loc at, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitdiag(at, COLOR(RED, "error"), fmt, ap);
    va_end(ap);
}

void emitinfo(struct loc at, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emitdiag(at, COLOR(CYAN, "info"), fmt, ap);
    va_end(ap);
}

const char *source;

static void normalize_source(char *source) {
    unsigned len = strlen(source);
    for(unsigned k = 0; k < len; k += 1) {
        if(source[k] == '\t' || source[k] == '\v' || source[k] == '\f')
            ((char *)source)[k] = ' ';
    }
}

void isource(char *src, const char *file) {
    normalize_source(src);

    source = src;
    filename = file;

    make_line_map();
}
