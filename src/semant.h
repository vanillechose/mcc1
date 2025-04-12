#ifndef SEMANT_H
#define SEMANT_H

#include "common.h"
#include "source.h"
#include "strings.h"

/*
 * whether compilation can continue past semantic analysis
 */
extern int semant_error;

/*
 * external definitions exposed to the code generator
 * element type: struct symbol *
 */
extern struct vec extdefs;

/*
 * compile time integer constant
 */
typedef size_t intval;

typedef enum {
    Kvoid,
    Kint,
    Kptr,
    Kfun,
} typecode;

struct type {
    typecode op;
    unsigned rank;
    unsigned width;
    unsigned align;
    unsigned sign;

    struct type *base;
    struct vec *params;
};

struct arch {
    struct type *inttype;
    struct type *uinttype;
    struct type *longtype;
    struct type *ulongtype;
    struct type *llongtype;
    struct type *ullongtype;
    struct type *sizettype;
    struct type *ptrdiffttype;
    size_t ptrwidth;
};

/*
 * type specifiers
 */
enum {
    SpecNone     = 0,
    SpecVoid     = BIT(0),
    SpecInt      = BIT(3),
    SpecLong     = BIT(4),
    SpecLongLong = BIT(5),
    SpecSigned   = BIT(6),
    SpecUnsigned = BIT(7),

    SpecInvalid  = -1,
};

typedef enum {
    Sdata,
    Sfunc,
} symbolcode;

struct symbol {
    struct loc loc;

    symbolcode op;
    string *name;
    struct type *ty;
    int level; /* scope */

    /* function definitions */
    struct tree *body;
    /* stack variables */
    intptr_t     stackloc;
};

void isemant(struct arch *arch);

/* {{{ Declarations */
void enter_decl(int typename);
void leave_decl(struct loc loc);

struct type *declspec_type(struct loc loc);

void add_specifier(struct loc loc, int atom);

struct symbol *make_idecl(struct loc loc, string *name);
struct symbol *make_pdecl(struct loc loc, struct symbol *base);
struct symbol *make_fdecl(struct loc loc, struct symbol *base, struct vec *params);

void declare_symbol(struct symbol *sym);
struct symbol *define_function(struct loc loc, struct symbol *sym);

void enter_fun(struct symbol *sym);
void leave_fun(void);
/* }}} */

typedef enum {
    Oconst,
    Ovar,
    Ocall,

    /* unary */
    Oaddr,
    Oindir,
    Opos,
    Oneg,
    Ocomp, /* bitwise complement */
    Onot,
    Ocast,
    /* binary */
    Omul,
    Odiv,
    Omod,
    Oadd,
    Osub,
    /* shifts */
    Osl,
    Osr,
    /* equality */
    Olt,
    Ogt,
    Ole,
    Oge,
    Oeq,
    Oneq,
    /* bitwise arithmetic */
    Oand,
    Oxor,
    Oor,
    Oassign,

    Oseq,
    Obranch,
    Owhile,
    Odowhile,
    Oret,
} treecode;

struct tree {
    struct loc loc;
    treecode op;
    struct type *ty;

    int isnullptr;
    int decayed;
    int lvalue;

    union {
        /* integer constants */
        intval ival;
        /* identifiers */
        struct symbol *name;
        /* call */
        struct {
            struct tree *fun;
            struct vec   args; /* vec of symbols */
        };
        /* unary */
        struct tree *base;
        /* sequence of statements */
        struct {
            struct vec decls;
            struct vec stmts;
        };
        struct {
            struct tree *l;
            struct tree *r;
        };
        struct {
            struct tree *cond;
            struct tree *t;
            struct tree *f;
        };
    };
};

/* {{{ Statements */
void enter_block(struct loc loc);

void append_statement(struct tree *t);

struct tree *make_branch(struct loc loc, treecode op, struct tree *cond);
struct tree *make_return(struct loc loc, struct tree *t);

struct tree *make_for(struct loc loc, struct tree *init, struct tree *cond, struct tree *tail);
void add_for_body(struct tree *fr, struct tree *body);

struct tree *leave_block(void);
/* }}} */
/* {{{ Expressions */
struct tree *make_assign(struct loc loc, struct tree *l, struct tree *r);
struct tree *make_binary(struct loc loc, treecode op, struct tree *l, struct tree *r);

struct tree *make_cast(struct loc loc, struct type *to, struct tree *t);

struct tree *make_inc(struct loc loc, int postfix, struct tree *t);
struct tree *make_dec(struct loc loc, int postfix, struct tree *t);

struct tree *make_subscript(struct loc loc, struct tree *l, struct tree *r);
struct tree *make_call(struct loc loc, struct tree *fn, struct vec *args);

struct tree *make_unary(struct loc loc, treecode op, struct tree *t);

struct tree *make_ident(struct loc loc, string *name);
struct tree *make_iconst(struct loc loc);
/* }}} */

#endif /* SEMANT_H */
