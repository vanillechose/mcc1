#include "semant.h"

static void data(FILE *fh, struct symbol *sym) {
    struct type *ty = sym->ty;

    switch(ty->op) {
        case Kint:
            break;
        case Kptr:
            break;
        default:
            die("tried to emit data with type code %d", ty->op);
    }

    say(fh, "\t.align\t%u", ty->align);
    say(fh, "\t.global\t%.*s", FMT(sym->name));
    say(fh, "\t.type\t%.*s, @object", FMT(sym->name));
    say(fh, "\t.size\t%.*s, %u", FMT(sym->name), ty->width);
    say(fh, "%.*s:", FMT(sym->name));
    say(fh, "\t.zero\t%u", ty->width);
}

#define STACKALIGN  16
#define STACKTOP     8 /* space needed for callee saved registers */

#define NPARAM_REG 6

static const char *preg[][NPARAM_REG] = {
    [4] = { "edi", "esi", "edx", "ecx", "r8d", "r9d" },
    [8] = { "rdi", "rsi", "rdx", "rcx", "r8",  "r9"  },
};

inline static unsigned align(unsigned sp, unsigned to) {
    return (sp + to - 1) & -to;
}

static struct symbol *curfn;

static void prologue(FILE *fh, unsigned stacksz) {
    /*
     * +-----------+ <- stack argument n ( = 8n+16(%rbp) )
     * |           |
     * |           | <- stack argument 0 ( = 16(%rbp) )
     * +-----------+ <- current frame
     * |  return   |
     * |  address  |
     * +-----------+ 
     * |  old rbp  | 0(%rbp) = 0(%rsp) (stack is aligned)
     * +-----------+
     *
     *            (1) Initial stack frame
     */
    say(fh, "\tpushq\t%%rbp");
    say(fh, "\tmovq\t%%rsp, %%rbp");
    
    stacksz = align(stacksz, STACKALIGN);

    /*
     * +-----------+ <- stack argument n ( = 8n+16(%rbp) )
     * |           |
     * |           | <- stack argument 0 ( = 16(%rbp) )
     * +-----------+ <- current frame
     * |  return   |
     * |  address  |
     * +-----------+ 
     * |  old rbp  | 0(%rbp)
     * +-----------+ <- callee saved registers
     * |           |
     * |           | -STACKTOP(%rbp)
     * +-----------+ <- local variables (including passed by register arguments)
     * |           |
     * |           |
     * |           |
     * |           | 
     * |           | -stacksz(%rbp)
     * +-----------+
     *
     *            (2) Complete stack frame
     *
     * local variables are addressed using %rbp
     * and a negative displacement
     */
    say(fh, "\tsubq\t$%u, %%rsp", stacksz);
    say(fh, "\tmovq\t%%rbx, -8(%%rbp)");

    int n = 0;
    for(struct symbol **it = NULL; (it = vec_next(curfn->ty->params, it)); ) {
        if(n >= NPARAM_REG)
            break;

        struct symbol *sym = *it;
        assert(sym->stackloc > 0);

        const char **regs = preg[sym->ty->width];
        char suffix = sym->ty->width == 8 ? 'q' : 'l';

        say(fh, "\tmov%c\t%%%s, -%ld(%%rbp)", suffix, regs[n], sym->stackloc);

        n += 1;
    }
}

static void epilogue(FILE *fh, unsigned stacksz) {
    stacksz = align(stacksz, STACKALIGN);

    if(stacksz)
        say(fh, "\taddq\t$%u, %%rsp", stacksz);
    say(fh, "\tmovq\t-8(%%rbp), %%rbx");

    say(fh, "\tleave");
    say(fh, "\tret");
}

static size_t npush, npop;

inline static void push(FILE *fh, const char *reg) {
    say(fh, "\tpushq\t%%%s", reg);
    npush += 1;
}

inline static void pop(FILE *fh, const char *reg) {
    say(fh, "\tpopq\t%%%s", reg);
    npop += 1;
}

static void tree(FILE *fh, struct tree *t);

static void lvalue(FILE *fh, struct tree *t) {
    switch(t->op) {
        case Ovar:
            if(t->name->stackloc > 0) /* local variable */
                say(fh, "\tleaq\t-%lu(%%rbp), %%rbx", t->name->stackloc);
            else if(t->name->stackloc < 0) /* argument variable */
                say(fh, "\tleaq\t%lu(%%rbp), %%rbx", -t->name->stackloc);
            else
                say(fh, "\tleaq\t%.*s(%%rip), %%rbx", FMT(t->name->name));
            break;
        case Oindir:
            tree(fh, t->base);
            say(fh, "\tmovq\t%%rax, %%rbx");
            break;
        default:
            die("not an lvalue");
    }
}

static void call(FILE *fh, struct tree *t) {
    /* address of function is in rax */
    unsigned i = t->args.len ? t->args.len - 1 : 0;
    size_t stackadjust = 0;

    while(i >= NPARAM_REG) {
        struct symbol *arg = *(struct symbol **)vec_get(&t->args, i--);
        struct type *ty = arg->ty;

        stackadjust += ty->width;
        stackadjust = align(stackadjust, MAX(ty->width, 8));
    }

    stackadjust = align(stackadjust, 16);

    if(stackadjust)
        say(fh, "\tsubq\t$%ld, %%rsp", stackadjust);

    i = 0;
    unsigned eightbyte = 0;
    for(i = NPARAM_REG; i < t->args.len; i += 1) {
        struct symbol *arg = *(struct symbol **)vec_get(&t->args, i);
        struct type *ty = arg->ty;

        char suffix = ty->width == 8 ? 'q' : 'l';
        const char *bx = ty->width == 8 ? "rbx" : "ebx";

        assert(arg->stackloc > 0);

        say(fh, "\tmov%c\t-%ld(%%rbp), %%%s", suffix, arg->stackloc, bx);
        say(fh, "\tmov%c\t%%%s, %u(%%rsp)", suffix, bx, eightbyte);

        eightbyte += ty->width;
        eightbyte = align(eightbyte, MAX(ty->width, 8));
    }

    for(i = 0; i < NPARAM_REG; i += 1) {
        struct symbol **it = vec_get(&t->args, i);
        if(!it)
            break;
        struct symbol *arg = *it;
        struct type *ty = arg->ty;

        char suffix = ty->width == 8 ? 'q' : 'l';

        say(fh, "\tmov%c\t-%lu(%%rbp), %%%s", suffix, arg->stackloc, preg[ty->width][i]);
    }

    say(fh, "\tcall\t*%%rax");

    if(stackadjust)
        say(fh, "\taddq\t$%ld, %%rsp", stackadjust);
}

static unsigned label;

static void tree(FILE *fh, struct tree *t) {
    struct type *ty;

    switch(t->op) {
        case Obranch:
        case Owhile:
        case Odowhile:
            ty = t->cond->ty;
            break;
        default:
            ty = t->ty;
            break;
    }

    char suffix;
    const char *ax, *bx, *dx;
    switch(ty->op) {
        case Kint:
            if(ty->width != 8) {
                ax = "eax";
                bx = "ebx";
                dx = "edx";
                suffix = 'l';
                break;
            }
            /* fallthrough */
        default:
            ax = "rax";
            bx = "rbx";
            dx = "rdx";
            suffix = 'q';
            break;
    }

    /* emit operands */
    switch(t->op) {
        case Oconst:
        case Ovar:
        case Owhile:
        case Odowhile:
            break;
        case Ocall:
            tree(fh, t->fun);
            break;
        case Oindir:
        case Opos:
        case Oneg:
        case Ocomp:
        case Onot:
        case Ocast:
        case Obranch:
            tree(fh, t->base);
            break;
        case Oaddr:
            lvalue(fh, t->base);
            break;
        case Omul:
        case Odiv:
        case Omod:
        case Oadd:
        case Osub:
        case Osl:
        case Osr:
        case Olt:
        case Ogt:
        case Ole:
        case Oge:
        case Oeq:
        case Oneq:
        case Oand:
        case Oxor:
        case Oor:
            /*
             * Binary operations: emit rhs, push rhs, emit lhs,
             * pop rhs to the appropriate register
             */
            tree(fh, t->r);
            push(fh, "rax");
            tree(fh, t->l);
            if(t->op == Osl || t->op == Osr)
                pop(fh, "rcx");
            else
                pop(fh, "rbx");
            break;
        case Oassign:
            lvalue(fh, t->l);
            push(fh, "rbx");
            tree(fh, t->r);
            pop(fh, "rbx");
            break;
        case Oseq:
            for(struct tree **it = NULL; (it = vec_next(&t->stmts, it)); ) {
                tree(fh, *it);
                if((*it)->op == Oret)
                    break;
            }
            break;
        case Oret:
            if(t->base)
                tree(fh, t->base);
            break;
    }

    /* emit operator code */
    switch(t->op) {
        case Oconst:
            say(fh, "\tmov%c\t$%lu, %%%s", suffix, t->ival, ax);
            break;
        case Ovar:
            if(t->name->stackloc > 0) /* local variable */
                say(fh, "\tmov%c\t-%lu(%%rbp), %%%s", suffix, t->name->stackloc, ax);
            else if(t->name->stackloc < 0) /* argument variable */
                say(fh, "\tmov%c\t%lu(%%rbp), %%%s", suffix, -t->name->stackloc, ax);
            else
                say(fh, "\tmov%c\t%.*s(%%rip), %%%s", suffix, FMT(t->name->name), ax);
            break;
        case Ocall:
            call(fh, t);
            break;
        case Oindir:
            say(fh, "\tmov%c\t(%%rax), %%%s", suffix, ax);
            break;
        case Oaddr:
            say(fh, "\tmovq\t%%rbx, %%rax");
            break;
        case Opos:
            break;
        case Oneg:
        case Ocomp:
            say(fh, "\t%s%c\t%%%s", t->op == Oneg ? "neg" : "not", suffix, ax);
            break;
        case Onot:
            say(fh, "\tcmp%c\t$0, %%%s", suffix, ax);
            say(fh, "\tsete\t%%al");
            say(fh, "\tmovzbl\t%%al, %%eax");
            break;
        case Ocast:
            /* (k : long) <- (n : int) ~> movslq n, k */
            if(ty->width == 8 && t->base->ty->width == 4)
                say(fh, "\tmov%sq\t%%%s, %%%s",
                        ty->sign ? "sl" : "", "eax", "rax");
            break;
        case Omul:
            say(fh, "\timul%c\t%%%s, %%%s", suffix, bx, ax);
            break;
        case Odiv:
        case Omod:
            if(ty->sign) {
                say(fh, "\t%s", suffix == 'q' ? "cqto" : "cltd");
                say(fh, "\tidiv%c\t%%%s", suffix, bx);
            } else {
                say(fh, "\txor%c\t%%%s, %%%s", suffix, dx, dx);
                say(fh, "\tdiv%c\t%%%s", suffix, bx);
            }
            if(t->op == Omod)
                say(fh, "\tmov%c\t%%%s, %%%s", suffix, dx, ax);
            break;
        case Oadd:
            say(fh, "\tadd%c\t%%%s, %%%s", suffix, bx, ax);
            break;
        case Osub:
            say(fh, "\tsub%c\t%%%s, %%%s", suffix, bx, ax);
            break;
        /* shift: rhs is already in cx */
        case Osl:
            say(fh, "\tshl%c\t%%cl, %%%s", suffix, ax);
            break;
        case Osr:
            /* implementation defined, gcc generates sar when lhs
             * is signed */
            if(ty->sign)
                say(fh, "\tsar%c\t%%cl, %%%s", suffix, ax);
            else
                say(fh, "\tshr%c\t%%cl, %%%s", suffix, ax);
            break;
        case Olt:
        case Ogt:
        case Ole:
        case Oge:
        case Oeq:
        case Oneq:
            say(fh, "\tcmp%c\t%%%s, %%%s", suffix, bx, ax);
            goto emitcmp;
        case Oand:
            say(fh, "\tand%c\t%%%s, %%%s", suffix, bx, ax);
            break;
        case Oxor:
            say(fh, "\txor%c\t%%%s, %%%s", suffix, bx, ax);
            break;
        case Oor:
            say(fh, "\tor%c\t%%%s, %%%s", suffix, bx, ax);
            break;
        case Oassign:
            say(fh, "\tmov%c\t%%%s, (%%rbx)", suffix, ax);
            break;
        case Oseq:
            break;
        case Oret:
            say(fh, "\tjmp\t.L%.*s.ret", FMT(curfn->name));
            break;
        unsigned lbl;
        case Obranch:
            say(fh, "\tcmp%c\t$0, %%%s", suffix, ax);
            lbl = label++;

            say(fh, "\tje\t.L%.*s.branch.else.%u", FMT(curfn->name), lbl);
            tree(fh, t->t);
            if(t->f)
                say(fh, "\tjmp\t.L%.*s.branch.end.%u", FMT(curfn->name), lbl);

            say(fh, ".L%.*s.branch.else.%u:", FMT(curfn->name), lbl);
            if(t->f)
                tree(fh, t->f);

            say(fh, ".L%.*s.branch.end.%u:", FMT(curfn->name), lbl);
            break;
        case Odowhile:
        case Owhile:
            lbl = label++;
            if(t->op == Odowhile)
                say(fh, "\tjmp\t.L%.*s.while.body.%u:", FMT(curfn->name), lbl);

            say(fh, ".L%.*s.while.cond.%u:", FMT(curfn->name), lbl);
            tree(fh, t->cond);
            say(fh, "\tcmp%c\t$0, %%%s", suffix, ax);
            say(fh, "\tje\t.L%.*s.while.after.%u", FMT(curfn->name), lbl);

            if(t->op == Odowhile)
                say(fh, ".L%.*s.while.body.%u:", FMT(curfn->name), lbl);

            tree(fh, t->t);
            say(fh, "\tjmp\t.L%.*s.while.cond.%u", FMT(curfn->name), lbl);
            say(fh, ".L%.*s.while.after.%u:", FMT(curfn->name), lbl);
            break;
    }
    
    return;
    static const char *cmpsuffix[][2] = {
        /* unsigned   signed */
        { "b",        "l" },
        { "a",        "g" },
        { "be",       "le" },
        { "ae",       "ge" },
        { "e",        "e" },
        { "ne",       "ne" },
    };
emitcmp:
    int s = t->l->ty->op == Kint && t->l->ty->sign;
    assert(Olt <= t->op && t->op <= Oneq);
    say(fh, "\tset%s\t%%al", cmpsuffix[t->op - Olt][s]);
    say(fh, "\tmovzbl\t%%al, %%eax");
}

/*
 * Recursively assigns stack locations to local
 * variables declared in t
 * Returned value may need further alignment
 */
static unsigned assign_memloc(unsigned sz, struct tree *t) {
    switch(t->op) {
        case Oconst:
        case Ovar:
        case Ocall:
            return sz;
        case Oret:
            if(!t->base)
                return sz;
            /* fallthrough */
        case Oaddr:
        case Oindir:
        case Opos:
        case Oneg:
        case Ocomp:
        case Onot:
        case Ocast:
            return assign_memloc(sz, t->base);
        case Omul:
        case Odiv:
        case Omod:
        case Oadd:
        case Osub:
        case Osl:
        case Osr:
        case Olt:
        case Ogt:
        case Ole:
        case Oge:
        case Oeq:
        case Oneq:
        case Oand:
        case Oxor:
        case Oor:
        case Oassign:
            unsigned l = assign_memloc(sz, t->l);
            unsigned r = assign_memloc(sz, t->r);
            return MAX(l, r);
        case Oseq:
            break;
        case Obranch:
        case Owhile:
        case Odowhile:
            unsigned c = assign_memloc(sz, t->cond);
            unsigned i = assign_memloc(sz, t->t);
            unsigned e;
            if(t->f)
                e = assign_memloc(sz, t->f);
            else
                e = 0;
            sz = MAX(c, MAX(i, e));
            return sz;
    }

    for(struct symbol **it = NULL; (it = vec_next(&t->decls, it)); ) {
        struct symbol *sym = *it;
        struct type   *ty  = sym->ty;

        /* TODO: expose a way to check whether a type is complete in semant.h */
        assert(ty->op == Kint || ty->op == Kptr);
        assert(ty->width && ty->align);

        sz += ty->width;
        sz = align(sz, ty->align);
        sym->stackloc = sz;

        trace("stack frame[%lu] = %.*s", sym->stackloc, FMT(sym->name));
    }

    unsigned maxsz = sz;
    for(struct tree **it = NULL; (it = vec_next(&t->stmts, it)); ) {
        unsigned s = assign_memloc(sz, *it);
        maxsz = MAX(maxsz, s);
    }
    return maxsz;
}

static unsigned assign_paramloc(unsigned sz, struct symbol *fn) {
    struct type *ty = fn->ty;
    assert(ty->op == Kfun && ty->params);
    int n = 0, top = 16;

    for(struct symbol **it = NULL; (it = vec_next(ty->params, it)); ) {
        struct symbol *param = *it;
        struct type   *ty    = param->ty;

        if(n < NPARAM_REG) {
            sz += ty->width;
            sz = align(sz, ty->align);
            param->stackloc = sz;

            trace("stack frame[%ld] = %.*s", param->stackloc, FMT(param->name));
        } else {
            top = align(top, 8);
            param->stackloc = -top;
            top += ty->width;

            trace("stack frame[-%ld] = %.*s", -param->stackloc, FMT(param->name));
        }

        n += 1;
    }

    return sz;
}

static void func(FILE *fh, struct symbol *sym) {
    struct type *ty = sym->ty;

    switch(ty->op) {
        case Kfun:
            break;
        default:
            die("tried to emit function with a non function type");
    }

    assert(sym->body && sym->body->op == Oseq);

    curfn = sym;
    npush = npop = label = 0;

    unsigned stacksz = STACKTOP;
    stacksz = assign_paramloc(stacksz, curfn);
    stacksz = assign_memloc(stacksz, curfn->body);

    say(fh, "\t.global\t%.*s", FMT(sym->name));
    say(fh, "\t.type\t%.*s, @function", FMT(sym->name));
    say(fh, "%.*s:", FMT(sym->name));

    say(fh, "\t/* PROLOGUE */");
    prologue(fh, stacksz);

    if(npush != npop)
        die("corrupted stack (npush != npop)");

    say(fh, "\t/* BODY */");
    tree(fh, sym->body);

    string *main = getkey0("main");
    if(sym->name == main)
        say(fh, "\txorl\t%%eax, %%eax");

    say(fh, ".L%.*s.ret:", FMT(sym->name));
    say(fh, "\t/* EPILOGUE */");
    epilogue(fh, stacksz);

    say(fh, "\t.size\t%.*s, .-%.*s", FMT(sym->name), FMT(sym->name));
}

void emit_amd64_sysv(void) {
    enum {
        SEC_NONE,
        SEC_DATA,
        SEC_TEXT,
    } lastsec = SEC_NONE;

    for(struct symbol **it = NULL; (it = vec_next(&extdefs, it)); ) {
        struct symbol *sym = *it;

        switch(sym->op) {
            case Sdata:
                if(lastsec != SEC_DATA)
                    say(stdout, "\t.bss");
                data(stdout, sym);
                lastsec = SEC_DATA;
                break;
            case Sfunc:
                if(lastsec != SEC_TEXT)
                    say(stdout, "\t.text");
                func(stdout, sym);
                lastsec = SEC_TEXT;
                break;
        }
    }
}
