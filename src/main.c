#include "common.h"
#include "parser.h"
#include "semant.h"
#include "source.h"

#define basety(name, w, s) \
    static struct type name = { \
        .op = Kint, \
        .rank = w * 8, \
        .align = w, \
        .width = w, \
        .sign = s, \
    };

basety(inttype, 4, 1);
basety(uinttype, 4, 0);
basety(longtype, 8, 1);
basety(ulongtype, 8, 1);
basety(llongtype, 8, 1);
basety(ullongtype, 8, 1);

struct arch arch_amd64_sysv = {
    .inttype      = &inttype,
    .uinttype     = &uinttype,
    .longtype     = &longtype,
    .ulongtype    = &ulongtype,
    .llongtype    = &llongtype,
    .ullongtype   = &ullongtype,
    .sizettype    = &ulongtype,
    .ptrdiffttype = &longtype,
    .ptrwidth     = 8,
};

static char *slurp(FILE *in) {
    Vec buf = vec_of(char);
    char c;


    while((c = fgetc(in)) != EOF)
        vec_push(&buf, &c);

    c = '\0';
    vec_push(&buf, &c);

    return buf.alloc;
}

extern void emit_amd64_sysv(void);

int main(int argc, char **argv) {
    int gen_asm = 1;
    const char *filename = NULL;

    if(argc > 1) {
        for(int i = 1; i < argc; i += 1) {
            const char *arg = argv[i];
            if(arg[0] == '-' && arg[1]) {
                if(!strcmp(arg + 1, "fsyntax-only"))
                    gen_asm = 0;
                else
                    die("invalid argument `%s`", arg);
            } else if(arg[0] != '-') { /* not stdin */
                if(filename)
                    die("only a single input file is supported");
                filename = arg;
            }
        }
    }

    if(filename)
        if(!freopen(filename, "r", stdin))
            die("could not open `%s`: %s", filename, strerror(errno));

    /* compiler initialization */
    isource(slurp(stdin), filename ?: "<stdin>");
    ilex(source);
    isemant(&arch_amd64_sysv);

    if(yyparse() || semant_error)
        return 1;

    if(gen_asm) {
        emit_amd64_sysv();
    }

    return 0;
}
