#include "common.h"
#include "parser.h"
#include "strings.h"

static struct table strs;

#include "tokens.def"

inline static void putstr(const char *name, int code) {
    table_put(&strs, getkey0(name), code);
}

static const char *sp;

void ilex(const char *source) {
    sp = source;

#   define s(str, t) \
    putstr(str, T ## t);
    FOREACH_KEYWORD(s);
#   undef s
}

int yylex();

static int comment() {
    while(*sp) {
        if(sp[0] == '*' && sp[1] == '/') {
            sp += 2;
            return yylex();
        }
        sp += 1;
    }

    yylloc.hi = sp - source;
    emiterror(yylloc, "unterminated '/*' comment");
    semant_error = 1;
    return -1;
}

static int commentl() {
    while(*sp) {
        if(*sp++ == '\n')
            return yylex();
    }
    yylloc.hi = sp - source;
    return -1;
}

int yylex(void) {
redo:
    yylloc.lo = sp - source;
    yylloc.hi = yylloc.lo;
    switch(*sp++) {
        case '\0':
            yylloc.hi += 1;
            return -1;
        case ' ':
        case '\t':
        case '\v':
        case '\f':
        case '\n':
            goto redo;

        case '_':
        case 'a' ... 'z':
        case 'A' ... 'Z':
            while(isalpha(*sp) || *sp == '_')
                sp += 1;
            yylloc.hi = sp - source;
            string *id = getkey(&source[yylloc.lo], &source[yylloc.hi]);
            uintptr_t *kw = table_find(&strs, id);
            if(kw)
                return *kw;
            else {
                yylval.id = id;
                return TIDENT;
            }

        case '0':
            switch(*sp) {
                case 'x':
                case 'X':
                    do sp += 1; while(isxdigit(*sp));
                    break;
                case 'b':
                case 'B':
                    do sp += 1; while(*sp == '1' || *sp == '0');
                    break;
            }
            /* fallthrough, invalid digits are handled
             * during semantic analysis */
        case '1' ... '9':
            while(isdigit(*sp)) /* integer suffixes */
                sp += 1;

            yylloc.hi = sp - source;
            return TINTEGER;

        /*
         * R: set yylloc and return the token
         * E: if(consume '=') R
         * D: if(consume 'previous character') R
         */
#       define R(t) { yylloc.hi = sp - source; return t; }
#       define E(t) if(*sp == '=') { sp++; R(t) } else
#       define D(t) if(*sp == sp[-1]) { sp++; R(t) } else
        case '!': E(TNEQ) R(TNOT)
        case '%': E(TMODASSIGN) R(TMOD)
        case '&': D(TAND) E(TANDASSIGN) R(TAMPERSAND)
        case '*': E(TMULASSIGN) R(TMUL)
        case '+': E(TPLUSASSIGN) D(TINC) R(TPLUS)
        case '-': E(TMINUSASSIGN) D(TDEC) R(TMINUS)
        case '/':
            E(TDIVASSIGN) if(*sp == '*') {
                sp += 1;
                return comment();
            } else if(*sp == '/') {
                sp += 1;
                return commentl();
            } else R(TDIV);
        case '=': D(TEQ) R(TASSIGN)
        case '^': E(TXORASSIGN) R(TXOR)
        case '|': D(TOR) E(TORASSIGN) R(TBAR)

        case '<':
            if(*sp == sp[-1]) {
                sp++;
                E(TLSHIFTASSIGN) { R(TLSHIFT) }
            } else E(TLE) R(TLESS)
        case '>':
            if(*sp == sp[-1]) {
                sp++;
                E(TRSHIFTASSIGN) { R(TRSHIFT) }
            } else E(TGE) R(TGREATER)

        case '.':
            if(*sp++ == '.') {
                D(TCDOTS) { sp--; }
            }
            R(TDOT)

        case ':': D(TCOLONS) R(TCOLON)
        case '(': R(TLPAREN)
        case ')': R(TRPAREN)
        case ',': R(TCOMMA)
        case ';': R(TSEMICOLON)
        case '?': R(TTEST)
        case '[': R(TLBRACKET)
        case ']': R(TRBRACKET)
        case '{': R(TLCURLY)
        case '~': R(TTILDE)
        case '}': R(TRCURLY)
        
        default:
            yylloc.hi += 1;
            emiterror(yylloc, "unexpected character `%c`", sp[-1]);
            return TYYerror;
    }
}
