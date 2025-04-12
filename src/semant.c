#include "semant.h"
#include "strings.h"

struct vec extdefs = vec_of(struct symbol *);

static struct type *voidtype;
static struct type *inttype;
static struct type *uinttype;
static struct type *longtype;
static struct type *ulongtype;
static struct type *llongtype;
static struct type *ullongtype;
static struct type *sizettype;
static struct type *ptrdiffttype;

static size_t ptrwidth;

int semant_error = 0;

#define emiterror(loc, ...) { \
    emiterror(loc, __VA_ARGS__); \
    semant_error = 1; \
}

struct scope {
    struct scope *parent;
    struct table names;
    int level;
};

static struct scope *filescope, *curscope;

void isemant(struct arch *arch) {
    static struct type v = { .op = Kvoid };

    voidtype     = &v;
    inttype      = arch->inttype;
    uinttype     = arch->uinttype;
    longtype     = arch->longtype;
    ulongtype    = arch->ulongtype;
    llongtype    = arch->llongtype;
    ullongtype   = arch->ullongtype;
    ptrwidth     = arch->ptrwidth;
    sizettype    = arch->sizettype;
    ptrdiffttype = arch->ptrdiffttype;

    filescope = curscope = xmalloc(sizeof *filescope);
    memset(curscope, 0, sizeof *curscope);
}

static struct symbol *resolve(string *name) {
    struct scope *it = curscope;
    while(it) {
        uintptr_t *p = table_find(&it->names, name);
        if(p) {
            struct symbol *sym = (void *)*p;
            assert(sym->level <= curscope->level);
            return sym;
        }
        it = it->parent;
    }
    return NULL;
}

static struct symbol *install(struct symbol *sym) {
    table_put(&curscope->names, sym->name, (uintptr_t)sym);
    return sym;
}

inline static struct type *make_type(struct type *ty) {
    return memcpy(xmalloc(sizeof *ty), ty, sizeof *ty);
}

inline static struct tree *make_tree(struct tree *t) {
    return memcpy(xmalloc(sizeof *t), t, sizeof *t);
}

static struct type *ptrty(struct type *pointee) {
    struct type ty = {
        .op = Kptr,
        .align = ptrwidth,
        .width = ptrwidth,
        .base = pointee,
    };
    return make_type(&ty);
}

static struct type *adjust(struct type *ty) {
    switch(ty->op) {
        case Kfun:
            return ptrty(ty);
        default:
            return ty;
    }
}

static struct type *functy(struct type *ret, struct vec *params) {
    assert(ret && ret->op != Kfun);

    for(struct symbol **sym = NULL; (sym = vec_next(params, sym)); )
        (*sym)->ty = adjust((*sym)->ty);

    struct type ty = {
        .op = Kfun,
        .base = ret,
        .params = params,
    };
    return make_type(&ty);
}

static int complete(struct type *t) {
    return t->op == Kint || t->op == Kptr;
}

static int compatible(struct type *s, struct type *t) {
    if(s == t)
        return true;
    if(s->op != t->op)
        return false; /* TODO: enum types here */

    switch(s->op) {
        case Kfun:
            if(s->params->len != t->params->len)
                return false;
            for(unsigned i = 0; i < s->params->len; i += 1) {
                if(!compatible(vec_get(s->params, i), vec_get(t->params, i)))
                    return false;
            }
            /* fallthrough */
        case Kptr:
            return compatible(s->base, t->base);
        default:
            return false;
    }
}

static int isscalar(struct type *ty) {
    return ty->op == Kint || ty->op == Kptr;
}
static int isarith(struct type *ty) {
    return ty->op == Kint;
}
static int isint(struct type *ty) {
    return ty->op == Kint;
}

static struct tree *convert(struct type *to, struct tree *t) {
    assert(to && t->ty);
    assert(isscalar(to) || to == voidtype);

    if(compatible(to, t->ty)) {
        t->ty = to;
        return t;
    }

    struct tree ret = {
        .loc = t->loc,
        .op = Ocast,
        .ty = to,
        .isnullptr = t->isnullptr && to->base == voidtype,
        .base = t,
    };
    return make_tree(&ret);
}

/*
 * Checks that conversion of r as if by assignment to lty
 * is legal and returns the type r should be converted to
 */
static struct type *assign(struct loc loc, struct type *lty, struct tree *r) {
    struct type *rty = r->ty;
    switch(lty->op) {
        case Kint:
            if(!isarith(rty)) {
                emiterror(loc, "assignment to arithmetic type requires"
                        " the right operand to have arithmetic type");
            }
            break;
        case Kptr:
            if(rty->op != Kptr && !r->isnullptr) {
                emiterror(loc, "only pointers or null pointer constants can be assigned to pointer types");
                break;
            }

            if(!(compatible(lty, rty)
                || lty->base == voidtype
                || rty->base == voidtype)) {
                emiterror(loc, "assignment between incompatible pointer types");
            }
            break;
        /* 
         * assign should never be called with
         * lty = void or function as function
         * designators always undergo conversion
         * to pointer to functions and (void)
         * expressions are not lvalues.
         */
        case Kvoid:
        case Kfun:
        default:
            die("assignment to void or function type");
    }
    return lty;
}

/*
 * Performs integer promotion in place
 */
static void promote(struct tree **t) {
    struct type *ty = (*t)->ty;
    assert(ty->op == Kint);

    if(ty->rank <= inttype->rank) {
        struct type *nty;
        if(ty->width * 8 - ty->sign <= inttype->width * 8 - inttype->sign)
            nty = inttype;
        else
            nty = uinttype;

        *t = convert(nty, *t);
    }
}

/*
 * Performs usual arithmetic conversions in place
 */
static struct type *commonrealtype(struct tree **l, struct tree **r) {
    struct type *tl = (*l)->ty;
    struct type *tr = (*r)->ty;
    assert(isarith(tl) && isarith(tr));

    if(tl == tr)
        return tl;

    struct type *result;

    /* both types have the same sign, resulting
     * type is the one with the greater rank */
    if(tl->sign == tr->sign) {
        result = tl->rank > tr->rank ? tl : tr;
        goto ret;
    }
    /* make sure the the unsigned type is the first one */
    if(tl->sign) {
        struct type *tmp = tl;
        tl = tr;
        tr = tmp;
    }
    /* the unsigned type has a rank >= than the signed
     * type, resulting type is the unsigned one */
    if(tl->rank >= tr->rank) {
        result = tl;
        goto ret;
    }
    /* the signed type can represent all the values of the
     * unsigned one, resulting type is the signed one */
    if(tl->width < tr->width) {
        result = tr;
        goto ret;
    }
    assert(tr != inttype);
    if(tr == longtype) {
        result = ulongtype;
        goto ret;
    }
    if (tr == llongtype) {
        result = ullongtype;
        goto ret;
    }
    die("no common real type");

ret:
    *l = convert(result, *l);
    *r = convert(result, *r);
    return result;
}

/*
 * Returns the first type capable of representing k
 */
static struct type *iconsttype(intval k, int decimal) {
    struct type *inttypes[] = {
        inttype,
        uinttype,
        longtype,
        ulongtype,
        llongtype,
        ullongtype,
    };

    unsigned step = decimal ? 2 : 1;
    for(unsigned i = 0; i < NELEMS(inttypes); i += step) {
        struct type *ty = inttypes[i];
        if(k <= ~0ul >> ((8 - ty->width) * 8 + ty->sign))
            return ty;
    }
    return ullongtype;
}



struct declspec {
    struct loc loc;
    int ts; /* type specifiers */
    int fs; /* filescope ? */
    int empty;
};

static struct vec dsstack = { .ssz = sizeof(struct declspec) };

void enter_decl(int typename) {
    struct declspec ds = {
        .loc   = { 0, 0 },
        .ts    = SpecNone,
        .fs    = curscope == filescope,
        .empty = !typename,
    };
    vec_push(&dsstack, &ds);
}

void leave_decl(struct loc loc) {
    struct declspec *top = vec_pop(&dsstack);
    assert(top);
    if(top->ts != SpecInvalid && top->empty)
        emiterror(loc, "C forbids empty declarations");
}

static const char *string_of_atomic_ts(int atom) {
    switch(atom) {
        case SpecNone:     return "<none>";
        case SpecVoid:     return "void";
        case SpecSigned:   return "signed";
        case SpecUnsigned: return "unsigned";
        case SpecInt:      return "int";
        case SpecLong:     return "long";
        case SpecLongLong: return "long long";
        default:
            die("invalid atomic type specifier: %u", atom);
    }
}

void add_specifier(struct loc loc, int atom) {
    struct declspec *top = vec_last(&dsstack);
    assert(top);

    /* fix location */
    if(top->loc.lo == 0 && top->loc.lo == top->loc.hi)
        top->loc = loc;
    else
        top->loc = LOCRANGE(top->loc, loc);

    int s = top->ts;

    if(s == SpecInvalid) /* propagate errors */
        return;

    const char *satom, *sprev;
    if(s & SpecVoid) { /* can't combine anything with void */
        satom = string_of_atomic_ts(atom);
        sprev = "void";
        goto err;
    }
    if(atom == SpecVoid && s != SpecNone) {
        emiterror(loc, "cannot combine 'void' with previous type specifier");
        top->ts = SpecInvalid;
        return;
    }

    switch(atom) {
        case SpecVoid:
            top->ts = SpecVoid;
            return;
        case SpecSigned:
        case SpecUnsigned:
            if(s & SpecUnsigned || s & SpecSigned) {
                satom = atom == SpecSigned ? "signed" : "unsigned";
                sprev = s & SpecSigned ? "signed" : "unsigned";
                goto err;
            }
            s = s | atom;
            break;
        case SpecInt:
            if(s & SpecInt) {
                satom = sprev = "int";
                goto err;
            }
            s = s | SpecInt;
            break;
        case SpecLong:
            if(s & SpecLongLong) {
                satom = "long";
                sprev = "long long";
                goto err;
            }
            if(s & SpecLong) {
                s &= ~SpecLong;
                s |= SpecLongLong;
            } else
                s |= SpecLong;
            break;
        default:
            die("invalid atomic type specifier: %u", s);
    }
    top->ts = s;
    return;

err:
    emiterror(loc, "cannot combine '%s' with previous '%s' type specifier",
            satom, sprev);
    top->ts = SpecInvalid;
}

struct type *declspec_type(struct loc loc) {
    struct declspec *specs = vec_last(&dsstack);
    assert(specs);

    switch(specs->ts) {
        case SpecNone:
            emiterror(loc, "missing type specifier");
            return NULL;
        case SpecInvalid:
            return NULL;
        case SpecVoid:
            return voidtype;
        case SpecInt:
        case SpecSigned:
        case SpecSigned | SpecInt:
            return inttype;
        case SpecUnsigned:
        case SpecUnsigned | SpecInt:
            return uinttype;
        case SpecLong:
        case SpecInt | SpecLong:
        case SpecSigned | SpecLong:
        case SpecSigned | SpecInt | SpecLong:
            return longtype;
        case SpecUnsigned | SpecLong:
        case SpecUnsigned | SpecInt | SpecLong:
            return ulongtype;
        case SpecLongLong:
        case SpecInt | SpecLongLong:
        case SpecSigned | SpecLongLong:
        case SpecSigned | SpecInt | SpecLongLong:
            return llongtype;
        case SpecUnsigned | SpecLongLong:
        case SpecUnsigned | SpecInt | SpecLongLong:
            return ullongtype;
        default:
            die("invalid type specifier: %x", specs->ts);
    }
}

/* {{{ Declarations */
struct symbol *make_idecl(struct loc loc, string *name) {
    struct declspec *top = vec_last(&dsstack);
    assert(top);
    top->empty = 0;

    struct type *ty = declspec_type(loc);

    struct symbol *sym = xmalloc(sizeof *sym);
    *sym = (struct symbol) {
        .loc   = loc,
        .name  = name,
        .op    = Sdata,
        .ty    = ty,
        .level = curscope->level,
    };
    return sym;
}

struct symbol *make_pdecl(struct loc loc, struct symbol *base) {
    struct type *ty = base->ty;

    if(!ty) /* propagate errors */
        return base;

    base->loc = loc; /* fix locations */

    if(ty->op != Kfun && ty->op != Kptr) {
        base->ty = ptrty(ty);
    } else {
        /* FIXME: there ought to be a better way to do that */
        base->ty = ty->base;
        make_pdecl(base->loc, base);
        ty->base = base->ty;
        base->ty = ty;
    }

    return base;
}

struct symbol *make_fdecl(struct loc loc, struct symbol *base, struct vec *params) {
    assert(params->ssz == sizeof(struct symbol *));
    struct type *ty = base->ty;

    if(ty->op == Kfun) {
        emiterror(loc, "function cannot return another function");
        base->ty = NULL;
        return base;
    }

    /* note: a function should return a complete object type (or void)
     *       but this can't be checked here as the declarator list
     *       is not fully parsed
     */

    base->loc = LOCRANGE(base->loc, loc);
    base->op = Sfunc;
    base->ty = functy(ty, params);

    return base;
}

static struct vec bstack = vec_of(struct tree);

static void valid_functy(struct symbol *sym, int indef) {
    assert(sym->ty->op == Kfun);

    if(sym->ty->base->op != Kvoid && !complete(sym->ty->base)) {
        emiterror(sym->loc, "function must return either void or a complete object type");
    }

    struct symbol **x, **y;
    for(x = NULL; (x = vec_next(sym->ty->params, x)); ) {
        for(y = NULL; (y = vec_next(sym->ty->params, y)); ) {
            if((*x)->name == (*y)->name && x != y) {
                emiterror((*x)->loc, "parameter name '%.*s' appears twice in function declarator", FMT((*x)->name));
                return;
            }
        }

        if(indef && !complete((*x)->ty)) {
            emiterror((*x)->loc, "parameter should not have incomplete type in function definition");
        }
    }
}

void declare_symbol(struct symbol *sym) {
    assert(curscope);

    if(!sym->ty)
        return;

    struct symbol *existing = resolve(sym->name);
    if(existing && existing->level == curscope->level) {
        emiterror(sym->loc, "redeclaration of '%.*s' in the same scope", FMT(sym->name));
        return;
    }

    if(sym->ty->op == Kfun)
        valid_functy(sym, false);

    if(curscope != filescope && sym->ty->op == Kfun)
        emiterror(sym->loc, "function declaration at block scope");

    if(sym->ty->op != Kfun && !complete(sym->ty))
        emiterror(sym->loc, "object declared with incomplete type");

    install(sym);

    if(curscope == filescope) {
        if(sym->ty->op != Kfun)
        vec_push(&extdefs, &sym);
    } else {
        struct tree *blk = vec_last(&bstack);
        assert(blk && blk->op == Oseq);
        vec_push(&blk->decls, &sym);
    }
}

struct symbol *define_function(struct loc loc, struct symbol *sym) {
    assert(curscope == filescope);

    if(!sym->ty)
        return NULL;

    if(sym->ty->op != Kfun) {
        emiterror(loc, "'%.*s' defined as a function but without function type", FMT(sym->name));
        return NULL;
    }

    struct symbol *previous = resolve(sym->name);
    if(previous && previous->body) {
        emiterror(loc, "redefinition of function '%.*s'", FMT(sym->name));
        emitinfo(previous->loc, "previously defined here");
        return NULL;
    }

    valid_functy(sym, true);

    if(previous && !compatible(previous->ty, sym->ty)) {
        emiterror(loc, "definition of function '%.*s' with incompatible type", FMT(sym->name));
        emitinfo(previous->loc, "previously defined here");
        return NULL;
    }

    if(previous) {
        /* TODO: previous->ty = composite(previous->ty, sym->ty); */
        sym = previous;
    } else {
        install(sym);
    }
    vec_push(&extdefs, &sym);

    return sym;
}

static void enter_scope(void) {
    struct scope *sc = xmalloc(sizeof *sc);
    *sc = (struct scope) {
        .parent = curscope,
        .level = curscope->level + 1,
    };
    curscope = sc;
}

static void leave_scope() {
    assert(curscope != filescope);

    struct scope *sc = curscope;
    curscope = sc->parent;
    table_free(&sc->names);
    free(sc);
}

static struct symbol *curfn;

void enter_fun(struct symbol *sym) {
    enter_scope();
    assert(sym->ty->op == Kfun);
    for(struct symbol **it = NULL; (it = vec_next(sym->ty->params, it)); ) {
        install(*it);
    }
    curfn = sym;
}

void leave_fun(void) {
    leave_scope();
    curfn = NULL;
}
/* }}} */
/* {{{ Statements */
void enter_block(struct loc loc) {
    enter_scope();

    struct tree blk = {
        .loc   = loc,
        .op    = Oseq,
        .ty    = voidtype,
        .decls = vec_of(struct symbol *),
        .stmts = vec_of(struct tree *),
    };
    vec_push(&bstack, &blk);
}

void append_statement(struct tree *t) {
    struct tree *blk = vec_last(&bstack);
    assert(blk && blk->op == Oseq);
    vec_push(&blk->stmts, &t);
}

struct tree *make_branch(struct loc loc, treecode op, struct tree *cond) {
    assert(op == Obranch || op == Owhile);

    if(!cond->ty)
        return cond;

    struct type *ty = voidtype;
    if(!isscalar(cond->ty)) {
        emiterror(loc, "controlling expression must have scalar type");
        ty = NULL;
    }

    struct tree t = {
        .loc  = loc,
        .op   = op,
        .ty   = ty,
        .cond = cond,
    };
    return make_tree(&t);
}

void add_for_body(struct tree *fr, struct tree *body) {
    assert(fr->op == Oseq && fr->stmts.len == 3);

    struct tree *tail = *(struct tree **)vec_pop(&fr->stmts);
    struct tree *loop = *(struct tree **)vec_last(&fr->stmts);

    if(body->op == Oseq) {
        vec_push(&body->stmts, &tail);
    } else {
        enter_block(fr->loc);
        append_statement(body);
        append_statement(tail);
        body = leave_block();
    }

    assert(loop->op == Owhile && loop->t == NULL);
    loop->t = body;
}

struct tree *iconst_tree(struct loc loc, intval k) {
    struct tree ret = {
        .loc  = loc,
        .op   = Oconst,
        .ty   = iconsttype(k, 0),
        .ival = k,
    };
    return make_tree(&ret);
}

struct tree *make_for(struct loc loc, struct tree *init, struct tree *cond, struct tree *tail) {
    enter_block(loc);

    if(init)
        append_statement(init);

    if(!cond)
        cond = iconst_tree(loc, 0);
    struct tree *loop = make_branch(loc, Owhile, cond);
    append_statement(loop);

    if(tail)
        append_statement(tail);

    return leave_block();
}

struct tree *make_return(struct loc loc, struct tree *t) {
    assert(curfn);

    if(curfn->ty->base == voidtype && t) {
        emiterror(loc, "cannot return a value from a void-returning function");
    } else if(curfn->ty->base != voidtype && !t) {
        emiterror(loc, "expected expression after 'return' in non void-returning function");
    } else {
        if(t) {
            struct type *ty = assign(loc, curfn->ty->base, t);
            t = !ty ? t : convert(ty, t);
        }
    }

    struct tree ret = {
        .loc = loc,
        .op  = Oret,
        .ty  = voidtype,
        .base = t,
    };
    return make_tree(&ret);
}

struct tree *leave_block(void) {
    struct tree *blk = vec_pop(&bstack);
    assert(blk && blk->op == Oseq);

    leave_scope();

    return make_tree(blk);
}
/* }}} */
/* {{{ Expressions */
static struct symbol *make_temp(struct loc loc, struct type *ty) {
    assert(curscope != filescope);

    struct tree *blk = vec_last(&bstack);
    assert(blk && blk->op == Oseq);

    struct symbol *sym = xmalloc(sizeof *sym);
    *sym = (struct symbol) {
        .loc   = loc,
        .op    = Sdata,
        .name  = getkey0("__tmp"),
        .ty    = ty,
        .level = curscope->level,
    };
    vec_push(&blk->decls, &sym);
    return sym;
}

struct tree *make_iconst(struct loc loc) {
    const char *s = &source[loc.lo];
    const char *e = &source[loc.hi];

    int base = 10;
    if(s[0] == '0') {
        switch(s[1]) {
            case 'x': case 'X': base = 16; break;
            case 'b': case 'B': base =  2; break;
            default:            base =  8; break;
        }
    }

    char *end;
    if(base == 2 || base == 16) s += 2;

    int old_errno = errno;
    errno = 0;
    intval k = strtoul(s, &end, base);

    struct tree t = { .loc = loc };

    assert(e - end >= 0);

    if(s == end && end == e) { /* k = 0b or 0x */
        emiterror(loc, "empty integer constant");
        /* set the type anyway, it may help report errors elsewhere */
        t.ty = inttype;
    } else if(end != e) {
        struct loc offending = { end - source, loc.hi };
        emiterror(offending, "invalid digit '%c' in %s integer",
                *end, base == 8 ? "octal" : "binary");
    } else if(errno == ERANGE) {
        emitwarn(loc, "integer overflow");
    }

    errno = old_errno;

    t.op   = Oconst;
    t.ty   = iconsttype(k, base == 10);
    t.ival = k;
    t.isnullptr = k == 0;
    return make_tree(&t);
}

static struct tree *decay(struct tree *t) {
    if(!t->ty)
        return t;

    if(t->ty->op == Kfun) {
        t = make_unary(t->loc, Oaddr, t);
        t->decayed = 1;
    }
    return t;
}

struct tree *make_ident(struct loc loc, string *name) {
    assert(curscope);
    struct symbol *sym = resolve(name);

    struct type *ty;
    int lvalue  = 1;
    if(!sym) {
        emiterror(loc, "use of undeclared identifier '%.*s'", FMT(name));
        ty = NULL;
    } else if(sym->ty->op == Kfun) {
        ty = sym->ty;
        lvalue = 0;
    } else {
        ty = sym->ty;
    }

    struct tree ret = {
        .loc  = loc,
        .op   = Ovar,
        .ty   = ty,
        .lvalue  = lvalue,
        .name = sym,
    };
    return decay(make_tree(&ret));
}

struct tree *make_cast(struct loc loc, struct type *typename, struct tree *t) {
    if(typename != voidtype && !isscalar(typename)) {
        emiterror(loc, "typename should specify either void or scalar type");
        goto err;
    }
    if(!isscalar(t->ty) && typename != voidtype) {
        if(typename)
            emiterror(loc, "operand with non-scalar type can only be cast to void");
        goto err;
    }
    return convert(typename, t);

err:
    t->ty = NULL;
    return t;
}

struct tree *make_subscript(struct loc loc, struct tree *l, struct tree *r) {
    if(!r->ty)
        return r;
    if(!l->ty)
        return l; /* propagate errors */

    if(!(isarith(l->ty) && r->ty->op == Kptr)
            && !(isarith(r->ty) && l->ty->op == Kptr)) {
        emiterror(loc, "array subscripting requires a pointer and an integer");
        l->ty = NULL;
        return l;
    }

    struct tree *p = make_binary(loc, Oadd, l, r);
    return make_unary(loc, Oindir, p);
}

struct tree *make_call(struct loc loc, struct tree *fn, struct vec *args) {
    if(!fn->ty)
        return fn; /* propagate errors */

    struct type *pty = fn->ty;
    if(!(pty->op == Kptr && pty->base->op == Kfun)) {
        emiterror(fn->loc, "expected function pointer");
        pty = NULL;
    }

    struct type *fnty = fn->ty->base;

    if(args->len != fnty->params->len) {
        emiterror(loc, "wrong number of arguments"); 
        pty = NULL;
    }

    enter_block(loc);

    struct vec symargs = vec_of(struct symbol *);
    for(size_t i = 0; i < args->len; i += 1) {
        struct tree *argt = *(struct tree **)vec_get(args, i);
        struct type *argty   = (*(struct symbol **)vec_get(fnty->params, i))->ty;

        struct symbol *argsym = make_temp(argt->loc, argty);

        argty = assign(argt->loc, argty, argt);
        if(!argty) {
            emitinfo(loc, "in function call");
            pty = NULL;
            break;
        }

        argt  = convert(argty, argt);

        struct tree *argtemp = make_tree(&(struct tree) {
            .loc  = argt->loc,
            .op   = Ovar,
            .ty   = argty,
            .lvalue = 1,
            .name = argsym,
        });

        struct tree *assigntemp = make_tree(&(struct tree) {
            .loc = argt->loc,
            .op  = Oassign,
            .ty  = argty,
            .l   = argtemp,
            .r   = argt,
        });

        append_statement(assigntemp);

        vec_push(&symargs, &argsym);
    }

    struct tree t = {
        .loc  = loc,
        .op   = Ocall,
        .ty   = fnty->base,
        .fun  = fn,
        .args = symargs,
    };
    append_statement(make_tree(&t));

    struct tree *call = leave_block();
    call->ty = fnty->base;
    return call;
}

struct tree *make_unary(struct loc loc, treecode op, struct tree *t) {
    struct type *ty = t->ty;
    if(!ty)
        goto err;

    switch(op) {
        case Oaddr:
            if(t->decayed) {
                struct tree *tmp = t;
                assert(tmp->ty->op == Kptr);
                t = tmp->base;
                free(tmp->ty);
                free(tmp);
            }

            if(!t->lvalue && t->ty->op != Kfun) {
                emiterror(loc, "unary '&' requires a function designator or an lvalue");
                ty = NULL;
                goto err;
            }

            if(t->op == Oindir) {
                assert(t->base->ty->op == Kptr);
                struct tree *tmp = t;
                ty = t->base->ty;
                t  = t->base;
                free(tmp);

                return t;
            }

            ty = ptrty(t->ty);
            break;
        case Oindir:
            if(t->ty->op != Kptr) {
                emiterror(loc, "unary '*' requires pointer operand");
                ty = NULL;
                goto err;
            }

            if(t->op == Oaddr) {
                struct tree *tmp = t;
                ty = t->base->ty;
                t  = t->base;
                free(tmp->ty);
                free(tmp);

                return t;
            }

            ty = t->ty->base;
            break;
        case Oneg:
        case Opos:
            if(!isarith(t->ty))
                goto err;
            goto promote;
        case Ocomp:
            if(!isint(t->ty))
                goto err;
        promote:
            promote(&t);
            ty = t->ty;
            break;
        case Onot:
            if(!isscalar(t->ty))
                goto err;
            ty = inttype;
            break;
        default:
            die("not an unary operator: %u", op);
        err:
            if(!t->ty)
                emiterror(loc, "wrong operand type");
            ty = NULL;
    }
    struct tree ret = {
        .loc  = loc,
        .op   = op,
        .ty   = ty,
        .lvalue = op == Oindir && (!ty || ty->op != Kfun),
        .base = t,
    };
    return make_tree(&ret);
}

struct tree *make_binary(struct loc loc, treecode op, struct tree *l, struct tree *r) {
    struct type *tl = l->ty;
    struct type *tr = r->ty;

    if(!tl || !tr)
        goto err;
    
    struct type *ty;

    switch(op) {
        case Omul:
        case Odiv:
            if(!isarith(tl) || !isarith(tr))
                goto err;
            goto conv;
        case Omod:
        case Osr:
        case Osl:
        case Oand:
        case Oxor:
        case Oor:
            if(!isint(tl) || !isint(tr))
                goto err;
            if(op == Osl || op == Osr) {
                /* shift */
                promote(&l);
                promote(&r);
                ty = l->ty;
                goto end;
            }
            goto conv;
        case Osub:
            if(tl->op == Kptr && tr->op == Kptr) {
                if(!compatible(tl, tr)) {
                    emiterror(loc, "subtraction between two incompatible"
                            " pointer types");
                    goto err_reported;
                }
                /* XXX: check that this is correct */
                if(!complete(tl->base) || !complete(tr->base)) {
                    emiterror(loc, "subtraction between pointers to incomplete"
                            " object type");
                    goto err_reported;
                }
                assert(tl->base->width == tr->base->width);
                struct tree sub = {
                    .loc = loc,
                    .op = op,
                    .ty = ptrdiffttype,
                    .l = l,
                    .r = r,
                };
                l = make_tree(&sub);
                r = iconst_tree(loc, tl->base->width);
                return make_binary(loc, Odiv, l, r);
            }
            goto additive;
        case Oadd:
            /* special case: rewrite int + pointer as pointer + int */
            if(isint(tl) && tr->op == Kptr)
                return make_binary(loc, op, r, l);
        additive:
            if(isarith(tl) && isarith(tr))
                goto conv;
            if(isint(tr) && tl->op == Kptr) {
                if(!complete(tl->base)) {
                    emiterror(loc, "pointer operand should"
                            " point to a complete object type");
                    goto err_reported;
                }

                r = make_binary(r->loc, Omul, r, iconst_tree(r->loc, tl->base->width));
                r = convert(sizettype, r);
                ty = tl;
                break;
            }
            goto err;
        case Olt:
        case Ogt:
        case Ole:
        case Oge:
        case Oeq:
        case Oneq:
            if(isarith(tl) && isarith(tr)) {
                promote(&l);
                promote(&r);
                commonrealtype(&l, &r);
                ty = inttype;
                break;
            }
            if(tl->op == Kptr && tr->op == Kptr) {
                if(compatible(tl, tr)) {
                    ty = inttype;
                    break;
                }
                if(op == Oeq || op == Oneq) {
                    if(tl->base == voidtype
                            || tr->base == voidtype
                            || l->isnullptr
                            || r->isnullptr) {
                        ty = inttype;
                        break;
                    }
                }
                emiterror(loc, "comparison between two incompatible"
                        " pointer types");
                goto err_reported;
            }
            goto err;
        default:
            die("not a binary operator: %u", op);
        conv:
            /* usual arithmetic conversions */
            promote(&l);
            promote(&r);
            ty = commonrealtype(&l, &r);
            goto end;
        err:
            /* TODO: better error messages */
            if(tr && tl)
                emiterror(loc, "wrong operand types");
        err_reported:
            ty = NULL;
    }
end:
    struct tree ret = {
        .loc = loc,
        .op = op,
        .ty = ty,
        .l = l,
        .r = r,
    };
    return make_tree(&ret);
}

struct tree *make_assign(struct loc loc, struct tree *l, struct tree *r) {
    struct type *ty;

    if(!l->lvalue) {
        emiterror(loc, "left-hand side of assignment must be a lvalue");
        ty = NULL;
    } else {
        ty = assign(loc, l->ty, r);
        r  = convert(ty, r);
    }
    
    struct tree ret = {
        .loc = loc,
        .op = Oassign,
        .ty = ty,
        .l = l,
        .r = r,
    };
    return make_tree(&ret);
}
/* }}} */
