#ifndef SOURCE_H
#define SOURCE_H

extern const char *source;
extern const char *filename;

void isource(char *source, const char *filename);

struct loc {
    unsigned lo;
    unsigned hi;
};

#define LOCRANGE(llo, lhi) (struct loc) { \
    .lo = MIN((llo).lo, (lhi).lo), \
    .hi = MAX((llo).hi, (lhi).hi), \
}

[[gnu::format(printf, 2, 3)]]
void emitwarn(struct loc at, const char *fmt, ...);

[[gnu::format(printf, 2, 3)]]
void emiterror(struct loc at, const char *fmt, ...);

[[gnu::format(printf, 2, 3)]]
void emitinfo(struct loc at, const char *fmt, ...);

unsigned get_line_number(struct loc loc);

#endif /* SOURCE_H */
