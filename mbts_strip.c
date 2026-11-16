/*
 * mbts_strip.c - MochiBerry TypeScript type eraser (lexer-driven, self-contained).
 *
 * Byte-offset-preserving TypeScript -> JavaScript erasure: every erased span is
 * replaced by spaces (newlines inside the span stay newlines) so the output has
 * exactly the same length as the input and runtime line:col diagnostics still
 * point at the original .ts source.
 *
 * Self-contained: its own scanner, its own output buffer, its own context
 * struct.  Only MBTS_OK / MBTS_DIAG / MBTS_OOM and MBTSStatus come from mbts.h.
 * No symbol from mbts.c is referenced.  Every helper is static and mbtsc_
 * prefixed; only mbts_strip() and MBTS_Free() are exported.
 *
 * Clean build:
 *   gcc -O2 -std=c11 -Wall -Wextra -I. -D_GNU_SOURCE -c mbts_strip.c -o build/mbts_strip.o
 */

#include "mbts.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Context / frame structs + forward declarations (landmine #2: every */
/* forward declaration precedes its first use).                        */
/* ------------------------------------------------------------------ */
typedef struct mbtsc_Frame {
    char kind;        /* 'p' paren, 'b' bracket, 'c' brace, 'T' template,
                         'S' template substitution */
    int  is_params;   /* paren is a parameter list                  */
    int  is_control;  /* paren belongs to if/for/while/switch/...   */
    int  is_class;    /* brace is a class body                      */
    int  is_object;   /* brace is an object literal                 */
    int  is_ns;       /* brace is a namespace body                  */
    size_t ns_s, ns_e; /* namespace name span (for is_ns frames)   */
    int  ternary;     /* a '?' is pending at this level             */
    int  expect_var;  /* const/let/var seen at this level           */
    int  is_ctor;     /* paren is a constructor parameter list      */
} mbtsc_Frame;

typedef struct mbtsc_Ctx {
    const char *src;
    size_t      len;
    char       *out;
    mbtsc_Frame stk[128];
    size_t      n;          /* stack height (>= 1)                    */
    char        prev_sig;   /* last significant code char (0 = none)  */
    int         prev_expr;  /* prev token can end an expression       */
    int         at_stmt;    /* at a statement start                   */
    int         pending_class;   /* saw 'class', next '{' is a body   */
    int         class_hdr;       /* in a class header (extends/implements) */
    int         ctor_pending;    /* saw 'constructor' in class body   */
    int         rettype;         /* just closed params ')': next ':'  */
    int         ns_depth;        /* >0 inside a namespace body        */
    int         pending_ns;      /* next '{' is a namespace body      */
    size_t      ns_name_s, ns_name_e;   /* current namespace name span */
    /* ctor parameter-property names: source spans [s,e), in order */
    size_t      cprop_s[32], cprop_e[32];
    int         ncprop;
    const char *msg;        /* refusal message, NULL when none        */
    size_t      at;         /* refusal byte offset                    */
    /* splices: source span [start,end) replaced by generated code (for
       enum/namespace/param-props lowering).  The span is erased in place
       (spaces, newlines kept) and the code is spliced in at rebuild. */
    struct mbtsc_splice {
        size_t start, end;
        char  *code;
        size_t code_len;
    } *splices;
    int         nsplices;
    int         splice_cap;
} mbtsc_Ctx;

static int    mbtsc_is_ws(int c);
static int    mbtsc_is_ident_start(int c);
static int    mbtsc_is_ident_char(int c);
static size_t mbtsc_scan_ident_end(const char *src, size_t len, size_t i);
static size_t mbtsc_skip_ws(const char *src, size_t len, size_t i);
static size_t mbtsc_skip_comment(const char *src, size_t len, size_t i);
static size_t mbtsc_skip_quoted(const char *src, size_t len, size_t i, int q);
static size_t mbtsc_skip_regex(const char *src, size_t len, size_t i);
static size_t mbtsc_skip_template(const char *src, size_t len, size_t i);
static size_t mbtsc_scan_type(const char *src, size_t len, size_t i,
                              int stop_at_arrow);
static size_t mbtsc_match_angle(const char *src, size_t len, size_t i, int *ok);
static size_t mbtsc_match_brace(const char *src, size_t len, size_t i, int *ok);
static size_t mbtsc_stmt_end(const char *src, size_t len, size_t pos);
static void   mbtsc_erase(mbtsc_Ctx *c, size_t a, size_t b);
static int    mbtsc_refuse(mbtsc_Ctx *c, const char *msg, size_t at);
static void   mbtsc_pos(const char *src, size_t len, size_t off,
                        int *line, int *col);
static int    mbtsc_word_at(const char *src, size_t len, size_t i, const char *kw);
static int    mbtsc_is_type_look(const char *src, size_t a, size_t b);
static int    mbtsc_scan(mbtsc_Ctx *c);
static int    mbtsc_scan_import(mbtsc_Ctx *c, size_t start, size_t *out_i);
static int    mbtsc_scan_export(mbtsc_Ctx *c, size_t start, size_t *out_i);
static int    mbtsc_lower_enum(mbtsc_Ctx *c, size_t a, size_t *pi);
static int    mbtsc_lower_ctor_cprops(mbtsc_Ctx *c, const char *s, size_t len,
                                      size_t after_paren);
static int    mbtsc_lower_namespace(mbtsc_Ctx *c, size_t a, size_t *pi);
static int    mbtsc_splice(mbtsc_Ctx *c, size_t start, size_t end,
                           const char *code, size_t code_len);

/* ------------------------------------------------------------------ */
/* Tiny character predicates.  Bytes >= 0x80 are identifier characters  */
/* so UTF-8 sequences are never split.                                  */
/* ------------------------------------------------------------------ */
static int mbtsc_is_ws(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
           c == '\f' || c == '\v';
}

static int mbtsc_is_ident_start(int c)
{
    return c >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '$';
}

static int mbtsc_is_ident_char(int c)
{
    return mbtsc_is_ident_start(c) || (c >= '0' && c <= '9');
}

static size_t mbtsc_scan_ident_end(const char *src, size_t len, size_t i)
{
    while (i < len && mbtsc_is_ident_char((unsigned char)src[i]))
        i++;
    return i;
}

static size_t mbtsc_skip_ws(const char *src, size_t len, size_t i)
{
    while (i < len && mbtsc_is_ws((unsigned char)src[i]))
        i++;
    return i;
}

/* true when kw matches at i and is not glued to a longer word */
static int mbtsc_word_at(const char *src, size_t len, size_t i, const char *kw)
{
    size_t k = 0;
    while (kw[k] != '\0') {
        if (i + k >= len || src[i + k] != kw[k])
            return 0;
        k++;
    }
    if (i + k < len && mbtsc_is_ident_char((unsigned char)src[i + k]))
        return 0;
    return 1;
}

/*
 * Heuristic: does the angle-group content in [a,b) look like a type argument
 * list rather than a comparison?  Requires at least one uppercase letter, a
 * dot, or a nested type-ish token.
 */
static int mbtsc_is_type_look(const char *src, size_t a, size_t b)
{
    size_t i;
    for (i = a; i < b; i++) {
        unsigned char ch = (unsigned char)src[i];
        if ((ch >= 'A' && ch <= 'Z') || ch == '.' || ch == '<' ||
            ch == '[' || ch == '{' || ch == ',' || ch == '?' || ch == '&')
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lexical skipping.  Each returns the offset just past the construct.  */
/* ------------------------------------------------------------------ */
static size_t mbtsc_skip_comment(const char *src, size_t len, size_t i)
{
    if (i + 1 >= len || src[i] != '/')
        return i;
    if (src[i + 1] == '/') {
        i += 2;
        while (i < len && src[i] != '\n')
            i++;
        return i;
    }
    if (src[i + 1] == '*') {
        i += 2;
        while (i < len) {
            if (src[i] == '*' && i + 1 < len && src[i + 1] == '/')
                return i + 2;
            i++;
        }
        return len;
    }
    return i;
}

static size_t mbtsc_skip_quoted(const char *src, size_t len, size_t i, int q)
{
    i++; /* opening quote */
    while (i < len) {
        if (src[i] == '\\') {
            i += 2;
            continue;
        }
        if ((unsigned char)src[i] == (unsigned char)q)
            return i + 1;
        if (src[i] == '\n')   /* unterminated: bail out at the line end */
            return i;
        i++;
    }
    return len;
}

static size_t mbtsc_skip_regex(const char *src, size_t len, size_t i)
{
    int in_cls = 0;
    i++; /* opening slash */
    while (i < len) {
        char ch = src[i];
        if (ch == '\\') {
            i += 2;
            continue;
        }
        if (ch == '\n')
            return i;            /* not a regex after all */
        if (ch == '[') {
            in_cls = 1;
        } else if (ch == ']') {
            in_cls = 0;
        } else if (ch == '/' && !in_cls) {
            i++;
            while (i < len && mbtsc_is_ident_char((unsigned char)src[i]))
                i++;             /* flags */
            return i;
        }
        i++;
    }
    return len;
}

/* consumes a whole template literal starting at the backtick */
static size_t mbtsc_skip_template(const char *src, size_t len, size_t i)
{
    int br = 0;
    i++; /* opening backtick */
    while (i < len) {
        char ch = src[i];
        if (ch == '\\') {
            i += 2;
            continue;
        }
        if (ch == '`' && br == 0)
            return i + 1;
        if (ch == '$' && i + 1 < len && src[i + 1] == '{') {
            br++;
            i += 2;
            continue;
        }
        if (ch == '{' && br > 0) {
            br++;
            i++;
            continue;
        }
        if (ch == '}' && br > 0) {
            br--;
            i++;
            continue;
        }
        i++;
    }
    return len;
}

/* Find the offset just past the balanced '}' that closes the '{' at i. */
static size_t mbtsc_match_brace(const char *src, size_t len, size_t i, int *ok)
{
    int depth = 0;
    *ok = 0;
    if (i >= len || src[i] != '{')
        return i;
    while (i < len) {
        char ch = src[i];
        if (ch == '/' && i + 1 < len && (src[i + 1] == '/' || src[i + 1] == '*')) {
            i = mbtsc_skip_comment(src, len, i);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            i = mbtsc_skip_quoted(src, len, i, ch);
            continue;
        }
        if (ch == '`') {
            i = mbtsc_skip_template(src, len, i);
            continue;
        }
        if (ch == '{') {
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth == 0) {
                *ok = 1;
                return i + 1;
            }
        }
        i++;
    }
    return len;
}

/*
 * Match an angle-bracket type argument / parameter list.  The '<' is at i.
 * Only characters that can legitimately appear inside a type argument list are
 * accepted; anything else (a semicolon, a stray operator, ...) makes the match
 * fail so the caller copies the text through untouched.
 */
static size_t mbtsc_match_angle(const char *src, size_t len, size_t i, int *ok)
{
    int angle = 0;
    int par = 0, brk = 0, brc = 0;
    *ok = 0;
    if (i >= len || src[i] != '<')
        return i;
    while (i < len) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == '"' || ch == '\'') {
            i = mbtsc_skip_quoted(src, len, i, ch);
            continue;
        }
        if (ch == '`') {
            i = mbtsc_skip_template(src, len, i);
            continue;
        }
        if (ch == '/' && i + 1 < len &&
            (src[i + 1] == '/' || src[i + 1] == '*')) {
            i = mbtsc_skip_comment(src, len, i);
            continue;
        }
        if (ch == '<') {
            angle++;
        } else if (ch == '>') {
            angle--;
            if (angle == 0 && par == 0 && brk == 0 && brc == 0) {
                *ok = 1;
                return i + 1;
            }
            if (angle < 0)
                return i;
        } else if (ch == ';') {
            return i;
        } else if (ch == '(') {
            par++;
        } else if (ch == ')') {
            if (par == 0)
                return i;
            par--;
        } else if (ch == '[') {
            brk++;
        } else if (ch == ']') {
            if (brk == 0)
                return i;
            brk--;
        } else if (ch == '{') {
            brc++;
        } else if (ch == '}') {
            if (brc == 0)
                return i;
            brc--;
        } else if (mbtsc_is_ident_char(ch) || mbtsc_is_ws(ch) ||
                   ch == ',' || ch == '|' || ch == '&' || ch == '.' ||
                   ch == '=' || ch == '?' || ch == ':' || ch == '!' ||
                   ch == '@') {
            /* allowed inside a type argument list */
        } else {
            return i;
        }
        i++;
    }
    return len;
}
/* mbts_strip_part2.c - helpers: type scanner, statement end, erase, diag */

/* Scan a type expression starting at pos (just after ':' / 'as' / '=' etc).
   Returns end offset (exclusive), or 0 if nothing type-like was consumed.
   stop_at_arrow: for return-type positions, stop at '=>' at depth 0. */
static size_t mbtsc_scan_type(const char *src, size_t len, size_t pos, int stop_at_arrow)
{
    size_t i = pos;
    int angle = 0, par = 0, brk = 0, brc = 0;
    if (pos >= len)
        return 0;
    while (i < len) {
        unsigned char ch = (unsigned char)src[i];
        if (mbtsc_is_ws(ch)) { i++; continue; }
        if (ch == '/' && i + 1 < len && (src[i + 1] == '/' || src[i + 1] == '*')) {
            i = mbtsc_skip_comment(src, len, i);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            i = mbtsc_skip_quoted(src, len, i, ch);
            continue;
        }
        if (ch == '`') {
            i = mbtsc_skip_template(src, len, i);
            continue;
        }
        if (ch == '<') { angle++; i++; continue; }
        if (ch == '>') {
            if (angle > 0) { angle--; i++; continue; }
            break;
        }
        if (ch == '(') { par++; i++; continue; }
        if (ch == ')') {
            if (par > 0) { par--; i++; continue; }
            break;
        }
        if (ch == '[') { brk++; i++; continue; }
        if (ch == ']') {
            if (brk > 0) { brk--; i++; continue; }
            break;
        }
        if (ch == '{') {
            if (angle == 0 && par == 0 && brk == 0 && brc == 0) {
                int ok = 0;
                size_t end = mbtsc_match_brace(src, len, i, &ok);
                if (!ok)
                    break;
                {
                    size_t j = mbtsc_skip_ws(src, len, end);
                    if (j < len && src[j] == '{') { /* object type then body */
                        i = end;
                        continue;
                    }
                }
                if (stop_at_arrow)
                    break;               /* '{' is the function body */
                i = end;                 /* object type literal */
                continue;
            }
            brc++; i++; continue;
        }
        if (ch == '}') {
            if (brc > 0) { brc--; i++; continue; }
            break;
        }
        if (ch == ';' || ch == ',' || ch == '=') {
            if (angle == 0 && par == 0 && brk == 0 && brc == 0)
                break;
            i++; continue;
        }
        if (ch == '-' && i + 1 < len && src[i + 1] == '>') {
            if (angle == 0 && par == 0 && brk == 0 && brc == 0 && stop_at_arrow)
                break;
            i += 2; continue;
        }
        if (ch == '|' || ch == '&' || ch == '?' || ch == ':' || ch == '.' ||
            ch == '!' || ch == '~' || ch == '*' || ch == '+' || ch == '@') {
            i++; continue;
        }
        if (mbtsc_is_ident_start(ch)) {
            i = mbtsc_scan_ident_end(src, len, i);
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            while (i < len && (mbtsc_is_ident_char((unsigned char)src[i]) || src[i] == '.'))
                i++;
            continue;
        }
        break;
    }
    return (i > pos) ? i : 0;
}

/* Find the offset just past the terminating ';' (or '}' for block-like
   statements) of the statement beginning at pos.  Handles ASI on newline. */
static size_t mbtsc_stmt_end(const char *src, size_t len, size_t pos)
{
    size_t i = pos;
    int brc = 0;
    while (i < len) {
        unsigned char ch = (unsigned char)src[i];
        if (ch == ' ' || ch == '\t' || ch == '\r') { i++; continue; }
        if (ch == '\n') {
            if (brc == 0) {
                size_t j = i;
                while (j > pos && mbtsc_is_ws((unsigned char)src[j - 1]))
                    j--;
                if (j > pos) {
                    char pc = src[j - 1];
                    if (!(pc == ',' || pc == ';' || pc == '=' || pc == ':' ||
                          pc == '+' || pc == '-' || pc == '*' || pc == '%' ||
                          pc == '&' || pc == '|' || pc == '!' || pc == '?' ||
                          pc == '<' || pc == '>' || pc == '(' || pc == '[' ||
                          pc == '{' || pc == '/' || pc == '.' || pc == '~' ||
                          pc == '^' || pc == '@'))
                        return i; /* ASI */
                }
            }
            i++; continue;
        }
        if (ch == '/' && i + 1 < len && (src[i + 1] == '/' || src[i + 1] == '*')) {
            i = mbtsc_skip_comment(src, len, i);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            i = mbtsc_skip_quoted(src, len, i, ch);
            continue;
        }
        if (ch == '`') {
            i = mbtsc_skip_template(src, len, i);
            continue;
        }
        if (ch == '{') {
            if (brc == 0) {
                int ok = 0;
                size_t e = mbtsc_match_brace(src, len, i, &ok);
                if (ok) { i = e; continue; }
            }
            brc++; i++; continue;
        }
        if (ch == '}') {
            if (brc > 0) brc--;
            i++; continue;
        }
        if (ch == ';' && brc == 0)
            return i + 1;
        i++;
    }
    return len;
}

/* Erase [a,b): every byte becomes a space except newlines stay. */
static void mbtsc_erase(mbtsc_Ctx *c, size_t a, size_t b)
{
    size_t k;
    if (b > c->len) b = c->len;
    if (a >= b) return;
    for (k = a; k < b; k++) {
        if (c->out[k] != '\n')
            c->out[k] = ' ';
    }
}

/* Record a refusal (returns 0 = failure sentinel). */
static int mbtsc_refuse(mbtsc_Ctx *c, const char *msg, size_t at)
{
    c->msg = msg;
    c->at = at;
    return 0;
}

/* 1-based line/col for byte offset off. */
static void mbtsc_pos(const char *src, size_t len, size_t off, int *line, int *col)
{
    size_t i;
    int ln = 1, cl = 1;
    if (off > len) off = len;
    for (i = 0; i < off; i++) {
        if (src[i] == '\n') { ln++; cl = 1; }
        else cl++;
    }
    if (line) *line = ln;
    if (col) *col = cl;
}/* mbts_strip_part3.c - main lexer-driven scanner */

/* Find the offset just past the balanced ')' that closes the '(' at i. */
static size_t mbtsc_match_paren(const char *src, size_t len, size_t i, int *ok)
{
    int depth = 0;
    *ok = 0;
    if (i >= len || src[i] != '(')
        return i;
    while (i < len) {
        char ch = src[i];
        if (ch == '/' && i + 1 < len && (src[i + 1] == '/' || src[i + 1] == '*')) {
            i = mbtsc_skip_comment(src, len, i);
            continue;
        }
        if (ch == '"' || ch == '\'') {
            i = mbtsc_skip_quoted(src, len, i, ch);
            continue;
        }
        if (ch == '`') {
            i = mbtsc_skip_template(src, len, i);
            continue;
        }
        if (ch == '(') {
            depth++;
        } else if (ch == ')') {
            depth--;
            if (depth == 0) {
                *ok = 1;
                return i + 1;
            }
        }
        i++;
    }
    return len;
}

/* Erase the type list of a class header starting at an 'extends'/'implements'
   keyword, up to (not including) the class body '{'.  Returns the brace pos. */
static size_t mbtsc_erase_class_types(mbtsc_Ctx *c, size_t start)
{
    const char *s = c->src;
    size_t len = c->len, i = start;
    int angle = 0, par = 0, brk = 0;
    while (i < len) {
        char ch = s[i];
        if (ch == '"' || ch == '\'') { i = mbtsc_skip_quoted(s, len, i, ch); continue; }
        if (ch == '`') { i = mbtsc_skip_template(s, len, i); continue; }
        if (ch == '/' && i + 1 < len && (s[i + 1] == '/' || s[i + 1] == '*')) {
            i = mbtsc_skip_comment(s, len, i);
            continue;
        }
        if (ch == '<') { angle++; i++; continue; }
        if (ch == '>') { if (angle > 0) angle--; i++; continue; }
        if (ch == '(') { par++; i++; continue; }
        if (ch == ')') { if (par > 0) par--; i++; continue; }
        if (ch == '[') { brk++; i++; continue; }
        if (ch == ']') { if (brk > 0) brk--; i++; continue; }
        if (ch == '{' && angle == 0 && par == 0 && brk == 0) {
            mbtsc_erase(c, start, i);
            return i;
        }
        i++;
    }
    return i;
}

/* is this token one of the modifiers we erase? */
static int mbtsc_is_modifier(const char *src, size_t a, size_t b)
{
    static const char *const mods[] = {
        "public", "private", "protected", "readonly", "abstract", "override", 0
    };
    size_t n = b - a, k = 0;
    for (k = 0; mods[k]; k++) {
        size_t m = strlen(mods[k]);
        if (n == m && memcmp(src + a, mods[k], m) == 0)
            return 1;
    }
    return 0;
}

/* Decide whether the '(' at open is a parameter list (function/method/arrow)
   rather than a call or grouping. */
static int mbtsc_paren_is_params(const char *src, size_t len, size_t open,
                                 int prev_ctrl, int prev_was_function, char prev_sig)
{
    int ok = 0;
    size_t close = mbtsc_match_paren(src, len, open, &ok);
    size_t j;
    char n;
    if (!ok)
        return 0;
    j = mbtsc_skip_ws(src, len, close);
    if (j >= len)
        return 0;
    n = src[j];
    if (n == ':')
        return 1;
    if (n == '-' && j + 1 < len && src[j + 1] == '>')
        return 1;
    /* arrow function body: `(params) => body`.  prev_sig '=' means we are in a
       const/let/var or assignment init; a paren directly followed by `=>` is
       always an arrow parameter list (body is a block or an expression). */
    if (n == '=' && j + 1 < len && src[j + 1] == '>' &&
        (prev_sig == '=' || prev_sig == '(' || prev_sig == ',' ||
         prev_sig == '>' || mbtsc_is_ident_char((unsigned char)prev_sig)))
        return 1;
    if (n == '{') {
        if (prev_ctrl)
            return 0;
        if (prev_was_function)
            return 1;
        if (prev_sig == ')' || mbtsc_is_ident_char((unsigned char)prev_sig) ||
            prev_sig == '>')
            return 1;
        return 0;
    }
    return 0;
}

/* Erase keyword plus trailing spaces, but never the newline. */
static void mbtsc_erase_kw(mbtsc_Ctx *c, size_t a, size_t b)
{
    size_t e = b;
    while (e < c->len && (c->src[e] == ' ' || c->src[e] == '\t'))
        e++;
    mbtsc_erase(c, a, e);
}

/* Handle a `import` statement at start.  Returns 1 on success (i updated). */
static int mbtsc_scan_import(mbtsc_Ctx *c, size_t start, size_t *out_i)
{
    const char *s = c->src;
    size_t len = c->len;
    size_t end = mbtsc_stmt_end(s, len, start);
    size_t p = mbtsc_skip_ws(s, len, start + 6);
    size_t k;

    /* import type ... -> erase whole statement */
    if (p < len && mbtsc_word_at(s, len, p, "type")) {
        mbtsc_erase(c, start, end);
        *out_i = end;
        return 1;
    }
    /* import X = require(...)  /  import X = foo.bar -> erase whole */
    if (p < len && mbtsc_is_ident_start((unsigned char)s[p])) {
        size_t e = mbtsc_scan_ident_end(s, len, p);
        size_t r = mbtsc_skip_ws(s, len, e);
        if (r < len && s[r] == '=') {
            mbtsc_erase(c, start, end);
            *out_i = end;
            return 1;
        }
    }
    /* value import: copy through, but erase inline `type` keywords */
    k = start;
    while (k < end) {
        char ch = s[k];
        if (ch == '"' || ch == '\'') { k = mbtsc_skip_quoted(s, len, k, ch); continue; }
        if (ch == '`') { k = mbtsc_skip_template(s, len, k); continue; }
        if (ch == '/' && k + 1 < len && (s[k + 1] == '/' || s[k + 1] == '*')) {
            k = mbtsc_skip_comment(s, len, k);
            continue;
        }
        if (mbtsc_is_ident_start((unsigned char)ch)) {
            size_t e = mbtsc_scan_ident_end(s, len, k);
            if (e - k == 4 && memcmp(s + k, "type", 4) == 0) {
                size_t r = mbtsc_skip_ws(s, len, e);
                if (r < len && mbtsc_is_ident_start((unsigned char)s[r])) {
                    /* type-only specifier: erase `type` AND the following name,
                       preserving whitespace/newlines. */
                    mbtsc_erase(c, k, e);
                    mbtsc_erase(c, r, mbtsc_scan_ident_end(s, len, r));
                }
            }
            k = e;
            continue;
        }
        k++;
    }
    *out_i = end;
    return 1;
}

/* Find the innermost enclosing namespace name span. */
static int mbtsc_ns_name(const mbtsc_Ctx *c, size_t *s_out, size_t *e_out)
{
    size_t k;
    for (k = c->n; k > 0; k--) {
        if (c->stk[k - 1].is_ns) {
            *s_out = c->stk[k - 1].ns_s;
            *e_out = c->stk[k - 1].ns_e;
            return 1;
        }
    }
    return 0;
}

/* Find a class/function member's body '{', starting after the member name.
   Returns the offset of '{' or 0 when the member has no block body. */
static size_t mbtsc_member_body(const char *s, size_t len, size_t p)
{
    int par = 0, brk = 0;
    while (p < len) {
        char ch = s[p];
        if (ch == '"' || ch == '\'') { p = mbtsc_skip_quoted(s, len, p, ch); continue; }
        if (ch == '`') { p = mbtsc_skip_template(s, len, p); continue; }
        if (ch == '/' && p + 1 < len && (s[p + 1] == '/' || s[p + 1] == '*')) {
            p = mbtsc_skip_comment(s, len, p); continue;
        }
        if (ch == '(') { par++; p++; continue; }
        if (ch == ')') { if (par) par--; p++; continue; }
        if (ch == '[') { brk++; p++; continue; }
        if (ch == ']') { if (brk) brk--; p++; continue; }
        if (ch == '{' && par == 0 && brk == 0)
            return p;
        if (ch == ';' || ch == '=')
            return 0;               /* no block body */
        p++;
    }
    return 0;
}

/* Namespace member export: `export const K = v;` / `export function f(){}`
   / `export class C {}`.  The `export` keyword is erased and a
   `NS.NAME = NAME;` assignment is appended after the member. */
static int mbtsc_ns_export(mbtsc_Ctx *c, size_t start, size_t *out_i)
{
    const char *s = c->src;
    size_t len = c->len;
    size_t p = mbtsc_skip_ws(s, len, start + 6);
    size_t e, n, name_s, name_e, uns, une;
    size_t stmt_end, assign_at, nm, uns_n, cl, q;
    char *code;

    if (!mbtsc_ns_name(c, &uns, &une))
        return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
    if (p >= len || !mbtsc_is_ident_start((unsigned char)s[p]))
        return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
    e = mbtsc_scan_ident_end(s, len, p);
    n = e - p;
    if (n == 5 && memcmp(s + p, "const", 5) == 0) {
        size_t q2 = mbtsc_skip_ws(s, len, e);
        if (q2 >= len || !mbtsc_is_ident_start((unsigned char)s[q2]))
            return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
        name_s = q2;
        name_e = mbtsc_scan_ident_end(s, len, q2);
        stmt_end = mbtsc_stmt_end(s, len, start);
        assign_at = stmt_end;
    } else if (n == 3 && (memcmp(s + p, "let", 3) == 0 || memcmp(s + p, "var", 3) == 0)) {
        size_t q2 = mbtsc_skip_ws(s, len, e);
        if (q2 >= len || !mbtsc_is_ident_start((unsigned char)s[q2]))
            return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
        name_s = q2;
        name_e = mbtsc_scan_ident_end(s, len, q2);
        stmt_end = mbtsc_stmt_end(s, len, start);
        assign_at = stmt_end;
    } else if (n == 8 && memcmp(s + p, "function", 8) == 0) {
        size_t q2 = mbtsc_skip_ws(s, len, e);
        if (q2 >= len || !mbtsc_is_ident_start((unsigned char)s[q2]))
            return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
        name_s = q2;
        name_e = mbtsc_scan_ident_end(s, len, q2);
        {
            size_t body = mbtsc_member_body(s, len, name_e);
            int    ok = 0;
            size_t cl2;
            if (body == 0)
                return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
            cl2 = mbtsc_match_brace(s, len, body, &ok);
            if (!ok)
                return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
            assign_at = cl2;      /* just past the closing '}' */
        }
    } else if (n == 5 && memcmp(s + p, "class", 5) == 0) {
        size_t q2 = mbtsc_skip_ws(s, len, e);
        if (q2 >= len || !mbtsc_is_ident_start((unsigned char)s[q2]))
            return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
        name_s = q2;
        name_e = mbtsc_scan_ident_end(s, len, q2);
        {
            size_t body = mbtsc_member_body(s, len, name_e);
            int    ok = 0;
            size_t cl2;
            if (body == 0)
                return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
            cl2 = mbtsc_match_brace(s, len, body, &ok);
            if (!ok)
                return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
            assign_at = cl2;
        }
    } else {
        return mbtsc_refuse(c, "namespace export needs a transpiler, use tsc", start);
    }

    nm = name_e - name_s;
    uns_n = une - uns;
    cl = uns_n + 2 * nm + 5;
    code = (char *)malloc(cl + 1);
    if (!code)
        return mbtsc_refuse(c, "out of memory", start);
    q = 0;
    memcpy(code + q, s + uns, uns_n);  q += uns_n;
    memcpy(code + q, ".", 1);           q += 1;
    memcpy(code + q, s + name_s, nm);   q += nm;
    memcpy(code + q, " = ", 3);         q += 3;
    memcpy(code + q, s + name_s, nm);   q += nm;
    memcpy(code + q, ";", 1);           q += 1;
    code[q] = '\0';

    mbtsc_erase(c, start, start + 6);   /* drop the `export` keyword */
    if (mbtsc_splice(c, assign_at, assign_at, code, q) != 0) {
        free(code);
        return mbtsc_refuse(c, "out of memory", start);
    }
    free(code);
    *out_i = start + 6;
    return 1;
}

/* Handle a `export` statement at start.  Returns 1 on success (i updated) or 0
   on refusal. */
static int mbtsc_scan_export(mbtsc_Ctx *c, size_t start, size_t *out_i)
{
    const char *s = c->src;
    size_t len = c->len;
    size_t end = mbtsc_stmt_end(s, len, start);
    size_t p = mbtsc_skip_ws(s, len, start + 6);
    size_t k;

    /* inside a namespace: `export` marks a member as exported */
    if (c->ns_depth > 0)
        return mbtsc_ns_export(c, start, out_i);

    if (p < len && mbtsc_is_ident_start((unsigned char)s[p])) {
        size_t e = mbtsc_scan_ident_end(s, len, p);
        size_t n = e - p;
        if (n == 5 && memcmp(s + p, "type", 5) == 0) {
            mbtsc_erase(c, start, end);
            *out_i = end;
            return 1;
        }
        if (n == 9 && memcmp(s + p, "interface", 9) == 0) {
            mbtsc_erase(c, start, end);
            *out_i = end;
            return 1;
        }
        if (n == 7 && memcmp(s + p, "declare", 7) == 0) {
            mbtsc_erase(c, start, end);
            *out_i = end;
            return 1;
        }
        if (n == 4 && memcmp(s + p, "enum", 4) == 0)
            return mbtsc_refuse(c, "enum needs a transpiler, use tsc", p);
        if (n == 9 && memcmp(s + p, "namespace", 9) == 0)
            return mbtsc_refuse(c, "namespace needs a transpiler, use tsc", p);
        if (n == 6 && memcmp(s + p, "module", 6) == 0) {
            size_t r = mbtsc_skip_ws(s, len, e);
            if (r < len && (s[r] == '"' || s[r] == '\'')) {
                mbtsc_erase(c, start, end);
                *out_i = end;
                return 1;
            }
            return mbtsc_refuse(c, "namespace needs a transpiler, use tsc", p);
        }
    }
    if (p < len && s[p] == '=')
        return mbtsc_refuse(c, "export = needs a transpiler, use tsc", p);

    /* export { ... }  -> erase inline `type` keywords */
    if (p < len && s[p] == '{') {
        k = start;
        while (k < end) {
            char ch = s[k];
            if (ch == '"' || ch == '\'') { k = mbtsc_skip_quoted(s, len, k, ch); continue; }
            if (ch == '`') { k = mbtsc_skip_template(s, len, k); continue; }
            if (ch == '/' && k + 1 < len && (s[k + 1] == '/' || s[k + 1] == '*')) {
                k = mbtsc_skip_comment(s, len, k);
                continue;
            }
            if (mbtsc_is_ident_start((unsigned char)ch)) {
                size_t e = mbtsc_scan_ident_end(s, len, k);
                if (e - k == 4 && memcmp(s + k, "type", 4) == 0) {
                    size_t r = mbtsc_skip_ws(s, len, e);
                    if (r < len && mbtsc_is_ident_start((unsigned char)s[r]))
                        mbtsc_erase(c, k, e);
                }
                k = e;
                continue;
            }
            k++;
        }
        *out_i = end;
        return 1;
    }

    /* export then const/let/var/function/class/default/etc: let the main
       loop process the rest of the statement (types inside get erased). */
    *out_i = start + 6;
    return 1;
}

/* Erase the type after an `as` / `satisfies` operator at kw_start..kw_end. */
static size_t mbtsc_erase_as(mbtsc_Ctx *c, size_t kw_start, size_t kw_end)
{
    size_t end = mbtsc_scan_type(c->src, c->len, kw_end, 0);
    if (end == 0)
        return kw_start;   /* not a type after all: copy through */
    mbtsc_erase(c, kw_start, end);
    return end;
}
/* mbts_strip_part4.c - main scan loop and public entry points */

static mbtsc_Frame *mbtsc_top(mbtsc_Ctx *c)
{
    return &c->stk[c->n - 1];
}

static void mbtsc_push(mbtsc_Ctx *c, char kind)
{
    mbtsc_Frame *f;
    if (c->n >= sizeof(c->stk) / sizeof(c->stk[0]))
        return;
    f = &c->stk[c->n];
    memset(f, 0, sizeof(*f));
    f->kind = kind;
    c->n++;
}

static void mbtsc_pop(mbtsc_Ctx *c)
{
    if (c->n > 1)
        c->n--;
}

/* Is the current token at a member position inside a class body? */
static int mbtsc_at_class_member(mbtsc_Ctx *c, int prev_was_mod)
{
    mbtsc_Frame *t = mbtsc_top(c);
    if (t->kind != 'c' || !t->is_class)
        return 0;
    if (prev_was_mod)
        return 1;
    return c->prev_sig == '{' || c->prev_sig == ';' || c->prev_sig == '}' ||
           c->prev_sig == 0;
}

/*
 * Decide whether the ':' at pos is a type annotation we may erase.
 * Returns 0 = copy through, 1 = annotation (caller erases the type).
 */
static int mbtsc_is_type_pos(mbtsc_Ctx *c, size_t pos)
{
    mbtsc_Frame *t = mbtsc_top(c);
    (void)pos;
    if (c->rettype)
        return 1;
    if (t->ternary)
        return 0;                          /* ternary colon */
    if (t->kind == 'p' && t->is_params)
        return 1;                          /* parameter type */
    if (t->kind == 'c' && t->is_class &&
        (mbtsc_is_ident_char((unsigned char)c->prev_sig) || c->prev_sig == '?' ||
         c->prev_sig == '!' || c->prev_sig == ')' || c->prev_sig == ']' ||
         c->prev_sig == '}'))
        return 1;                          /* class field / method return */
    if (t->kind == 'c' && !t->is_object && t->expect_var &&
        (mbtsc_is_ident_char((unsigned char)c->prev_sig) || c->prev_sig == ']' ||
         c->prev_sig == '}' || c->prev_sig == '!'))
        return 1;                          /* const/let/var type */
    return 0;
}

/* Consume a run of digits / number literal. */
static size_t mbtsc_skip_number(const char *src, size_t len, size_t i)
{
    size_t start = i;
    while (i < len) {
        char ch = src[i];
        if ((ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == 'e' ||
            ch == 'E' || ch == 'x' || ch == 'X' || ch == 'b' || ch == 'B' ||
            ch == 'o' || ch == 'O' || ch == '+' || ch == '-')
            i++;
        else
            break;
    }
    return (i > start) ? i : start + 1;
}

static int mbtsc_scan(mbtsc_Ctx *c)
{
    const char *s = c->src;
    size_t len = c->len, i = 0;
    int prev_was_mod = 0;

    c->n = 1;
    memset(&c->stk[0], 0, sizeof(c->stk[0]));
    c->stk[0].kind = 'c';        /* root brace (statement level) */
    c->stk[0].is_object = 0;
    c->prev_sig = 0;
    c->prev_expr = 0;
    c->at_stmt = 1;

    while (i < len) {
        mbtsc_Frame *t = mbtsc_top(c);
        unsigned char ch = (unsigned char)s[i];

        /* ----- template text mode ----- */
        if (t->kind == 'T') {
            if (ch == '\\') { i += 2; continue; }
            if (ch == '`') { mbtsc_pop(c); i++; continue; }
            if (ch == '$' && i + 1 < len && s[i + 1] == '{') {
                mbtsc_push(c, 'S');
                i += 2;
                continue;
            }
            i++;
            continue;
        }
        /* ----- template substitution mode: code, closed by '}' ----- */
        if (t->kind == 'S') {
            if (ch == '}') {
                mbtsc_pop(c);
                i++;
                continue;
            }
            /* fall through to code handling */
        }

        /* ----- whitespace ----- */
        if (mbtsc_is_ws(ch)) {
            if (ch == '\n')
                c->at_stmt = 1;
            i++;
            continue;
        }
        /* ----- comments (copy) ----- */
        if (ch == '/' && i + 1 < len && (s[i + 1] == '/' || s[i + 1] == '*')) {
            i = mbtsc_skip_comment(s, len, i);
            continue;
        }
        /* ----- string literal (copy) ----- */
        if (ch == '"' || ch == '\'') {
            i = mbtsc_skip_quoted(s, len, i, ch);
            c->prev_sig = '"';
            c->prev_expr = 1;
            c->at_stmt = 0;
            prev_was_mod = 0;
            continue;
        }
        /* ----- template literal ----- */
        if (ch == '`') {
            mbtsc_push(c, 'T');
            i++;
            continue;
        }
        /* ----- regex vs division ----- */
        if (ch == '/' && !c->prev_expr) {
            size_t e = mbtsc_skip_regex(s, len, i);
            if (e > i) {
                i = e;
                c->prev_sig = '/';
                c->prev_expr = 1;
                c->at_stmt = 0;
                prev_was_mod = 0;
                continue;
            }
        }
        /* ----- number ----- */
        if (ch >= '0' && ch <= '9') {
            i = mbtsc_skip_number(s, len, i);
            c->prev_sig = '0';
            c->prev_expr = 1;
            c->at_stmt = 0;
            prev_was_mod = 0;
            continue;
        }
        /* ----- identifier / keyword ----- */
        if (mbtsc_is_ident_start(ch)) {
            size_t a = i, b = mbtsc_scan_ident_end(s, len, i);
            size_t n = b - a;
            int is_kw = 0;

            /* `as` / `satisfies` type assertions */
            if (((n == 2 && memcmp(s + a, "as", 2) == 0) ||
                 (n == 9 && memcmp(s + a, "satisfies", 9) == 0)) &&
                c->prev_expr) {
                size_t ae = mbtsc_erase_as(c, a, b);
                i = (ae > a) ? ae : b;   /* nothing type-like: just skip kw */
                c->prev_sig = 'a';
                c->prev_expr = 1;
                c->at_stmt = 0;
                prev_was_mod = 0;
                continue;
            }

            /* declaration keywords */
            if (c->at_stmt &&
                ((n == 3 && memcmp(s + a, "let", 3) == 0) ||
                 (n == 3 && memcmp(s + a, "var", 3) == 0) ||
                 (n == 5 && memcmp(s + a, "const", 5) == 0))) {
                /* const enum -> lower it (drop the `const`) */
                if (n == 5) {
                    size_t r = mbtsc_skip_ws(s, len, b);
                    if (r + 4 <= len && memcmp(s + r, "enum", 4) == 0 &&
                        (r + 4 >= len || !mbtsc_is_ident_char((unsigned char)s[r + 4]))) {
                        size_t ei = 0;
                        if (mbtsc_lower_enum(c, r, &ei)) {
                            mbtsc_erase(c, a, r);   /* swallow the `const` */
                            i = ei;
                            prev_was_mod = 0;
                            continue;
                        }
                        return mbtsc_refuse(c, "enum needs a transpiler, use tsc", r);
                    }
                }
                mbtsc_top(c)->expect_var = 1;
                c->prev_sig = 'v';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                continue;
            }

            if (c->at_stmt && n == 6 && memcmp(s + a, "export", 6) == 0) {
                size_t oi = 0;
                if (!mbtsc_scan_export(c, a, &oi))
                    return 0;
                i = oi;
                c->prev_sig = 'e';
                c->prev_expr = 0;
                c->at_stmt = 1;
                prev_was_mod = 0;
                continue;
            }
            if (c->at_stmt && n == 6 && memcmp(s + a, "import", 6) == 0) {
                size_t oi = 0;
                if (!mbtsc_scan_import(c, a, &oi))
                    return 0;
                i = oi;
                c->prev_sig = 'i';
                c->prev_expr = 0;
                c->at_stmt = 1;
                prev_was_mod = 0;
                continue;
            }

            /* whole-statement type declarations */
            if (c->at_stmt && n == 9 && memcmp(s + a, "interface", 9) == 0) {
                size_t e = mbtsc_stmt_end(s, len, a);
                mbtsc_erase(c, a, e);
                i = e;
                c->prev_sig = ';';
                c->prev_expr = 0;
                c->at_stmt = 1;
                prev_was_mod = 0;
                continue;
            }
            if (c->at_stmt && n == 4 && memcmp(s + a, "type", 4) == 0) {
                size_t r = mbtsc_skip_ws(s, len, b);
                if (r < len && mbtsc_is_ident_start((unsigned char)s[r])) {
                    size_t e = mbtsc_stmt_end(s, len, a);
                    mbtsc_erase(c, a, e);
                    i = e;
                    c->prev_sig = ';';
                    c->prev_expr = 0;
                    c->at_stmt = 1;
                    prev_was_mod = 0;
                    continue;
                }
            }
            if (c->at_stmt && n == 7 && memcmp(s + a, "declare", 7) == 0) {
                size_t e = mbtsc_stmt_end(s, len, a);
                mbtsc_erase(c, a, e);
                i = e;
                c->prev_sig = ';';
                c->prev_expr = 0;
                c->at_stmt = 1;
                prev_was_mod = 0;
                continue;
            }
            if (c->at_stmt && n == 4 && memcmp(s + a, "enum", 4) == 0) {
                size_t ei = 0;
                if (mbtsc_lower_enum(c, a, &ei)) {
                    i = ei;
                    prev_was_mod = 0;
                    continue;
                }
                return mbtsc_refuse(c, "enum needs a transpiler, use tsc", a);
            }
            if (c->at_stmt && n == 9 && memcmp(s + a, "namespace", 9) == 0)
                ; /* handled below by mbtsc_lower_namespace */
            if (c->at_stmt && n == 6 && memcmp(s + a, "module", 6) == 0) {
                size_t r = mbtsc_skip_ws(s, len, b);
                if (r < len && (s[r] == '"' || s[r] == '\'')) {
                    size_t e = mbtsc_stmt_end(s, len, a);
                    mbtsc_erase(c, a, e);
                    i = e;
                    c->prev_sig = ';';
                    c->prev_expr = 0;
                    c->at_stmt = 1;
                    prev_was_mod = 0;
                    continue;
                }
                return mbtsc_refuse(c, "namespace needs a transpiler, use tsc", a);
            }

            /* decorators are refused */
            if (ch == '@')
                return mbtsc_refuse(c, "decorators need a transpiler, use tsc", i);

            /* access modifiers */
            if (mbtsc_is_modifier(s, a, b)) {
                mbtsc_Frame *tp = mbtsc_top(c);
                if (tp->kind == 'p' && tp->is_ctor) {
                    /* constructor parameter property: MOD NAME [: type] [= dflt] */
                    size_t ps = mbtsc_skip_ws(s, len, b);
                    size_t pe = mbtsc_scan_ident_end(s, len, ps);
                    size_t af = mbtsc_skip_ws(s, len, pe);
                    int is_prop =
                        (n == 6 && memcmp(s + a, "public", 6) == 0) ||
                        (n == 7 && memcmp(s + a, "private", 7) == 0) ||
                        (n == 9 && memcmp(s + a, "protected", 9) == 0) ||
                        (n == 8 && memcmp(s + a, "readonly", 8) == 0);
                    if (is_prop && pe > ps && af < len &&
                        (s[af] == ':' || s[af] == '?' || s[af] == ',' ||
                         s[af] == ')' || s[af] == '=')) {
                        if (c->ncprop < 32) {
                            c->cprop_s[c->ncprop] = ps;
                            c->cprop_e[c->ncprop] = pe;
                            c->ncprop++;
                        }
                        mbtsc_erase_kw(c, a, b);
                        c->prev_sig = ';';
                        c->prev_expr = 0;
                        c->at_stmt = 0;
                        prev_was_mod = 0;
                        i = b;
                        continue;
                    }
                    return mbtsc_refuse(c,
                        "constructor parameter properties need a transpiler, use tsc", a);
                }
                if (mbtsc_at_class_member(c, prev_was_mod) ||
                    (c->at_stmt && n == 8 && memcmp(s + a, "abstract", 8) == 0)) {
                    size_t r = mbtsc_skip_ws(s, len, b);
                    if (r < len && s[r] == 'c') { /* abstract class */
                        mbtsc_erase_kw(c, a, b);
                        c->prev_sig = ';';
                        c->prev_expr = 0;
                        c->at_stmt = 1;
                        prev_was_mod = 1;
                        i = b;
                        continue;
                    }
                    mbtsc_erase_kw(c, a, b);
                    c->prev_sig = ';';
                    c->prev_expr = 0;
                    c->at_stmt = 1;
                    prev_was_mod = 1;
                    i = b;
                    continue;
                }
            }

            /* namespace / module -> lower */
            if (c->at_stmt && n == 9 && memcmp(s + a, "namespace", 9) == 0) {
                size_t oi = 0;
                if (!mbtsc_lower_namespace(c, a, &oi))
                    return mbtsc_refuse(c, "namespace needs a transpiler, use tsc", a);
                c->ns_depth++;
                c->prev_sig = ';';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i = oi;              /* continue at the namespace body '{' */
                continue;
            }

            /* class / function / constructor */
            if (n == 5 && memcmp(s + a, "class", 5) == 0) {
                c->pending_class = 1;
                c->class_hdr = 1;
                c->prev_sig = 'c';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i = b;
                continue;
            }
            if (n == 8 && memcmp(s + a, "function", 8) == 0) {
                c->prev_sig = 'f';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i = b;
                continue;
            }
            if (n == 11 && memcmp(s + a, "constructor", 11) == 0) {
                if (mbtsc_at_class_member(c, prev_was_mod)) {
                    c->ctor_pending = 1;
                    c->prev_sig = 'n';
                    c->prev_expr = 0;
                    c->at_stmt = 0;
                    prev_was_mod = 0;
                } else {
                    c->prev_sig = 'n';
                    c->prev_expr = 0;
                    c->at_stmt = 0;
                    prev_was_mod = 0;
                }
                i = b;
                continue;
            }

            /* extends / implements in a class header: erase the type list */
            if (c->class_hdr &&
                ((n == 7 && memcmp(s + a, "extends", 7) == 0) ||
                 (n == 10 && memcmp(s + a, "implements", 10) == 0))) {
                size_t brace = mbtsc_erase_class_types(c, a);
                i = brace;
                c->prev_sig = '{';
                c->prev_expr = 0;
                c->at_stmt = 1;
                prev_was_mod = 0;
                continue;
            }

            /* value keywords */
            if ((n == 4 && memcmp(s + a, "this", 4) == 0) ||
                (n == 4 && memcmp(s + a, "null", 4) == 0) ||
                (n == 4 && memcmp(s + a, "true", 4) == 0) ||
                (n == 5 && memcmp(s + a, "false", 5) == 0) ||
                (n == 5 && memcmp(s + a, "super", 5) == 0) ||
                (n == 7 && memcmp(s + a, "typeof", 7) == 0) ||
                (n == 3 && memcmp(s + a, "new", 3) == 0)) {
                is_kw = 1;
            }

            c->prev_sig = (char)s[b - 1];
            c->prev_expr = is_kw ? 0 : 1;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i = b;
            continue;
        }

        /* ----- punctuation ----- */
        if (ch == '(') {
            mbtsc_Frame *f;
            int is_params = mbtsc_paren_is_params(s, len, i, c->at_stmt,
                                                  0, c->prev_sig);
            mbtsc_push(c, 'p');
            f = mbtsc_top(c);
            f->is_params = is_params;
            f->is_ctor = (c->ctor_pending != 0);
            c->ctor_pending = 0;
            c->prev_sig = '(';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == ')') {
            mbtsc_Frame *popped;
            mbtsc_pop(c);
            popped = &c->stk[c->n];
            if (popped->kind == 'p' && popped->is_ctor && c->ncprop > 0) {
                /* constructor param props collected: inject this.X=X; */
                if (!mbtsc_lower_ctor_cprops(c, s, len, i + 1))
                    return 0;
            }
            if (popped->kind == 'p' && popped->is_params) {
                size_t j = mbtsc_skip_ws(s, len, i + 1);
                if (j < len && s[j] == ':')
                    c->rettype = 1;
            }
            c->prev_sig = ')';
            c->prev_expr = 1;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '[') {
            mbtsc_push(c, 'b');
            c->prev_sig = '[';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == ']') {
            mbtsc_pop(c);
            c->prev_sig = ']';
            c->prev_expr = 1;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '{') {
            int is_class = c->pending_class != 0;
            int is_object = 0;
            mbtsc_Frame *f;
            if (!is_class) {
                if (c->prev_sig == '=' || c->prev_sig == '(' || c->prev_sig == ',' ||
                    c->prev_sig == ':' || c->prev_sig == '?' || c->prev_sig == '!' ||
                    c->prev_sig == '&' || c->prev_sig == '|' || c->prev_sig == '+' ||
                    c->prev_sig == '-' || c->prev_sig == '*' || c->prev_sig == '/' ||
                    c->prev_sig == '%' || c->prev_sig == '<' || c->prev_sig == '>' ||
                    c->prev_sig == '[' || c->prev_sig == '^' || c->prev_sig == '~' ||
                    c->prev_sig == '=')
                    is_object = 1;
                else if (c->prev_sig == 'v' && c->prev_expr == 0)
                    is_object = 0;   /* destructuring pattern */
                else if (c->prev_sig == 'a' || c->prev_sig == 'n' ||
                         c->prev_sig == 'c' || c->prev_sig == 'f')
                    is_object = 0;   /* after as/class/function keyword */
            }
            mbtsc_push(c, 'c');
            f = mbtsc_top(c);
            f->is_class = is_class;
            f->is_object = is_object;
            if (c->pending_ns) {
                f->is_ns = 1;
                f->ns_s = c->ns_name_s;
                f->ns_e = c->ns_name_e;
                c->pending_ns = 0;
            }
            c->pending_class = 0;
            c->class_hdr = 0;
            c->prev_sig = '{';
            c->prev_expr = 0;
            c->at_stmt = is_class ? 1 : (is_object ? 0 : 1);
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '}') {
            mbtsc_Frame *popped;
            mbtsc_pop(c);
            popped = &c->stk[c->n];
            if (popped->is_ns) {
                /* namespace body close -> `})(NAME || (NAME = {}));` */
                size_t nm = popped->ns_e - popped->ns_s;
                size_t cl = 3 + nm + (sizeof(" || (") - 1) + nm + (sizeof(" = {}));") - 1);
                char  *h = (char *)malloc(cl + 1);
                if (!h)
                    return mbtsc_refuse(c, "out of memory", i);
                {
                    size_t q = 0;
                    memcpy(h + q, "})(", sizeof("})(") - 1);   q += sizeof("})(") - 1;
                    memcpy(h + q, s + popped->ns_s, nm); q += nm;
                    memcpy(h + q, " || (", sizeof(" || (") - 1); q += sizeof(" || (") - 1;
                    memcpy(h + q, s + popped->ns_s, nm); q += nm;
                    memcpy(h + q, " = {}));", sizeof(" = {}));") - 1); q += sizeof(" = {}));") - 1;
                    h[q] = '\0';
                    if (mbtsc_splice(c, i, i + 1, h, q) != 0) {
                        free(h);
                        return mbtsc_refuse(c, "out of memory", i);
                    }
                }
                free(h);
                c->ns_depth--;
            }
            c->prev_sig = '}';
            c->prev_expr = 1;
            c->at_stmt = 1;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == ':') {
            if (mbtsc_is_type_pos(c, i)) {
                size_t tend = mbtsc_scan_type(s, len, i + 1, c->rettype ? 1 : 0);
                c->rettype = 0;
                if (tend != 0) {
                    mbtsc_erase(c, i, tend);
                    c->prev_sig = ';';
                    c->prev_expr = 0;
                    c->at_stmt = 0;
                    prev_was_mod = 0;
                    i = tend;
                    continue;
                }
                /* not actually a type: copy the ':' through */
                c->prev_sig = ':';
                c->prev_expr = 0;
                c->at_stmt = 1;
                prev_was_mod = 0;
                i++;
                continue;
            }
            c->prev_sig = ':';
            c->prev_expr = 0;
            c->at_stmt = 1;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '?') {
            if (i + 1 < len && s[i + 1] == ':') {
                /* optional marker x?: T - erase '?' then let the ':' handler
                   scan+erase the following type (is_type_pos sees prev_sig='?') */
                mbtsc_erase(c, i, i + 1);
                c->prev_sig = '?';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i += 1;
                continue;
            }
            if (i + 1 < len && s[i + 1] == '.') {
                c->prev_sig = '.';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i += 2;
                continue;
            }
            if (i + 1 < len && s[i + 1] == '?') {
                c->prev_sig = '?';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i += 2;
                continue;
            }
            /* ternary ? */
            mbtsc_top(c)->ternary = 1;
            c->prev_sig = '?';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '!') {
            if (i + 1 < len && s[i + 1] == '=') {
                c->prev_sig = '!';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i += 2;
                continue;
            }
            if (i + 1 < len &&
                (s[i + 1] == '.' || s[i + 1] == ';' || s[i + 1] == ',' ||
                 s[i + 1] == ')' || s[i + 1] == ']' || s[i + 1] == '}' ||
                 s[i + 1] == ':')) {
                /* definite assignment / non-null assertion: erase '!'; when the
                   next char is ':' leave the ':' for its own handler so the
                   following type still gets scanned (x!: number). */
                mbtsc_erase(c, i, i + 1);
                c->prev_sig = '!';
                c->prev_expr = 0;
                c->at_stmt = 0;
                prev_was_mod = 0;
                i += 1;
                continue;
            }
            c->prev_sig = '!';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '<') {
                        /* call/expression generics: f<T>(...) / f<T>; / f<T>, / f<T>) */
                        if (mbtsc_is_ident_char((unsigned char)c->prev_sig)) {
                            int ok = 0;
                            size_t e = mbtsc_match_angle(s, len, i, &ok);
                            if (ok && e < len) {
                                size_t w = mbtsc_skip_ws(s, len, e);
                                char nc = w < len ? s[w] : 0;
                                size_t content = i + 1;
                                if ((nc == '(' || nc == ';' || nc == ',' || nc == ')' ||
                                     (nc == '{' && c->class_hdr)) &&
                                    mbtsc_is_type_look(s, content, e - 1)) {
                                    mbtsc_erase(c, i, e);
                                    i = e;
                                    c->prev_sig = '>';
                                    c->prev_expr = 1;
                                    c->at_stmt = 0;
                                    prev_was_mod = 0;
                                    continue;
                                }
                            }
                        }
            /* arrow generic <T,>(x: T) => x at expression start */
            if (c->prev_expr == 0) {
                int ok = 0;
                size_t e = mbtsc_match_angle(s, len, i, &ok);
                if (ok && e < len && s[e] == '(' &&
                    mbtsc_is_type_look(s, i + 1, e - 1)) {
                    mbtsc_erase(c, i, e);
                    i = e;
                    c->prev_sig = '>';
                    c->prev_expr = 1;
                    c->at_stmt = 0;
                    prev_was_mod = 0;
                    continue;
                }
            }
            c->prev_sig = '<';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '>') {
            c->prev_sig = '>';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '=' && i + 1 < len && s[i + 1] == '>') {
            c->prev_sig = '>';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i += 2;
            continue;
        }
        if (ch == ';') {
            mbtsc_top(c)->expect_var = 0;
            mbtsc_top(c)->ternary = 0;
            c->prev_sig = ';';
            c->prev_expr = 0;
            c->at_stmt = 1;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == ',') {
            mbtsc_Frame *f = mbtsc_top(c);
            if (f->kind == 'c' && !f->is_object && f->expect_var)
                f->expect_var = 1;   /* continue a var declaration */
            else if (f->kind == 'c' && !f->is_object)
                f->expect_var = 0;
            f->ternary = 0;
            c->prev_sig = ',';
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '.' || ch == '=' ) {
            c->prev_sig = (char)ch;
            c->prev_expr = 0;
            c->at_stmt = 0;
            prev_was_mod = 0;
            i++;
            continue;
        }
        if (ch == '@') {
            return mbtsc_refuse(c, "decorators need a transpiler, use tsc", i);
        }
        /* other operators: copy */
        c->prev_sig = (char)ch;
        c->prev_expr = 0;
        c->at_stmt = 0;
        prev_was_mod = 0;
        i++;
    }
    return 1;
}
/* Order splices by source position so the rebuild can walk left-to-right even
   when a splice was recorded eagerly (e.g. a namespace export computed before
   the scanner reaches the member body). */
static int mbtsc_splice_cmp(const void *pa, const void *pb)
{
    const struct mbtsc_splice *a = (const struct mbtsc_splice *)pa;
    const struct mbtsc_splice *b = (const struct mbtsc_splice *)pb;
    if (a->start != b->start)
        return a->start < b->start ? -1 : 1;
    if (a->end != b->end)
        return a->end < b->end ? -1 : 1;
    return 0;
}

/* Record a splice: source span [start,end) is replaced by `code`. */
static int mbtsc_splice(mbtsc_Ctx *c, size_t start, size_t end,
                        const char *code, size_t code_len)
{
    struct mbtsc_splice *ns;
    char *cp;
    if (c->nsplices == c->splice_cap) {
        int nc = c->splice_cap ? c->splice_cap * 2 : 4;
        ns = (struct mbtsc_splice *)realloc(c->splices, (size_t)nc * sizeof(*ns));
        if (!ns)
            return -1;
        c->splices = ns;
        c->splice_cap = nc;
    }
    cp = (char *)malloc(code_len + 1);
    if (!cp)
        return -1;
    if (code_len)
        memcpy(cp, code, code_len);
    cp[code_len] = '\0';
    c->splices[c->nsplices].start = start;
    c->splices[c->nsplices].end   = end;
    c->splices[c->nsplices].code  = cp;
    c->splices[c->nsplices].code_len = code_len;
    c->nsplices++;
    return 0;
}

/* Growable code builder for lowering. */
typedef struct mbtsc_Cb {
    char  *p;
    size_t len, cap;
} mbtsc_Cb;

static int mbtsc_cb_putn(mbtsc_Cb *cb, const char *s, size_t n)
{
    if (cb->len + n + 1 > cb->cap) {
        size_t nc = cb->cap ? cb->cap : 64;
        char  *np;
        while (nc < cb->len + n + 1)
            nc *= 2;
        np = (char *)realloc(cb->p, nc);
        if (!np)
            return -1;
        cb->p = np;
        cb->cap = nc;
    }
    if (n)
        memcpy(cb->p + cb->len, s, n);
    cb->len += n;
    cb->p[cb->len] = '\0';
    return 0;
}

static int mbtsc_cb_puts(mbtsc_Cb *cb, const char *s)
{
    return mbtsc_cb_putn(cb, s, strlen(s));
}

static int mbtsc_cb_putc(mbtsc_Cb *cb, char ch)
{
    return mbtsc_cb_putn(cb, &ch, 1);
}

/* Skip a comma-separated enum member list between [start,end), emitting the
   tsc-style IIFE body into cb.  Returns 0 on success, -1 on refusal. */
static int mbtsc_enum_body(mbtsc_Ctx *c, const char *s, size_t len,
                           size_t start, size_t end, const char *name,
                           mbtsc_Cb *cb)
{
    size_t i = start;
    size_t nl = strlen(name);    /* enum name length */
    double prev = -1.0;          /* previous numeric value (for auto-inc)  */
    int    prev_known = 1;       /* prev is a statically-known number      */
    int    saw_string = 0;       /* enum has a string member               */
    int    first = 1;

    while (i < end) {
        size_t name_s, name_e, val_s, val_e;
        while (i < end && mbtsc_is_ws((unsigned char)s[i])) i++;
        if (i >= end) break;
        /* member name: identifier or string literal */
        name_s = i;
        if (s[i] == '\'' || s[i] == '"') {
            i = mbtsc_skip_quoted(s, len, i, s[i]);
            name_e = i;
        } else if (mbtsc_is_ident_start((unsigned char)s[i])) {
            i = mbtsc_scan_ident_end(s, len, i);
            name_e = i;
        } else {
            return -1;
        }
        while (i < end && mbtsc_is_ws((unsigned char)s[i])) i++;
        /* optional `= value` */
        val_s = val_e = 0;
        if (i < end && s[i] == '=') {
            i++;
            while (i < end && mbtsc_is_ws((unsigned char)s[i])) i++;
            val_s = i;
            /* scan to ',' or end at depth 0 */
            {
                int par = 0, brk = 0, brc = 0;
                while (i < end) {
                    char ch = s[i];
                    if (ch == '(') par++;
                    else if (ch == ')') { if (par) par--; }
                    else if (ch == '[') brk++;
                    else if (ch == ']') { if (brk) brk--; }
                    else if (ch == '{') brc++;
                    else if (ch == '}') { if (brc) brc--; }
                    else if (ch == ',' && !par && !brk && !brc) break;
                    else if (ch == ';' && !par && !brk && !brc) break;
                    i++;
                }
            }
            val_e = i;
            /* trim trailing whitespace from the value */
            while (val_e > val_s && mbtsc_is_ws((unsigned char)s[val_e - 1])) val_e--;
        }
        while (i < end && mbtsc_is_ws((unsigned char)s[i])) i++;
        if (i < end && s[i] == ',') i++;

        /* determine member style */
        if (!first)
            mbtsc_cb_puts(cb, "; ");
        first = 0;

        if (val_s && val_e > val_s) {
            /* explicit value */
            char v0 = s[val_s];
            if ((v0 == '\'' || v0 == '"') &&
                val_e == mbtsc_skip_quoted(s, len, val_s, v0)) {
                /* string literal member -> string enum */
                mbtsc_cb_putn(cb, name, nl);
                mbtsc_cb_puts(cb, "[\"");
                mbtsc_cb_putn(cb, s + name_s, name_e - name_s);
                mbtsc_cb_puts(cb, "\"] = ");
                mbtsc_cb_putn(cb, s + val_s, val_e - val_s);
                saw_string = 1;
                prev_known = 0;
            } else {
                /* numeric / computed member */
                mbtsc_cb_putn(cb, name, nl);
                mbtsc_cb_puts(cb, "[");
                mbtsc_cb_putn(cb, name, nl);
                mbtsc_cb_puts(cb, "[\"");
                mbtsc_cb_putn(cb, s + name_s, name_e - name_s);
                mbtsc_cb_puts(cb, "\"] = ");
                mbtsc_cb_putn(cb, s + val_s, val_e - val_s);
                mbtsc_cb_puts(cb, "] = \"");
                mbtsc_cb_putn(cb, s + name_s, name_e - name_s);
                mbtsc_cb_puts(cb, "\"");
                /* try to statically track the value for auto-inc */
                {
                    double dv = 0;
                    int    is_num = 1;
                    size_t k = val_s;
                    while (k < val_e && mbtsc_is_ws((unsigned char)s[k])) k++;
                    if (k < val_e && (s[k] == '-' || s[k] == '+' || (s[k] >= '0' && s[k] <= '9'))) {
                        char tmp[64]; size_t m = 0;
                        while (k < val_e && m + 1 < sizeof(tmp) &&
                               (mbtsc_is_ident_char((unsigned char)s[k]) || s[k] == '.' || s[k] == '-' || s[k] == '+' || s[k] == 'e' || s[k] == 'E'))
                            tmp[m++] = s[k++];
                        tmp[m] = '\0';
                        dv = strtod(tmp, NULL);
                        is_num = (m > 0);
                    } else {
                        is_num = 0;
                    }
                    if (is_num) { prev = dv; prev_known = 1; }
                    else { prev_known = 0; }
                }
            }
        } else {
            /* auto-increment */
            if (!prev_known) {
                /* computed previous value: cannot statically auto-inc */
                return -1;
            }
            prev = prev + 1;
            mbtsc_cb_putn(cb, name, nl);
            mbtsc_cb_puts(cb, "[");
            mbtsc_cb_putn(cb, name, nl);
            mbtsc_cb_puts(cb, "[\"");
            mbtsc_cb_putn(cb, s + name_s, name_e - name_s);
            mbtsc_cb_puts(cb, "\"] = ");
            {
                char num[32];
                int n = snprintf(num, sizeof(num), "%.15g", prev);
                mbtsc_cb_putn(cb, num, (size_t)n);
            }
            mbtsc_cb_puts(cb, "] = \"");
            mbtsc_cb_putn(cb, s + name_s, name_e - name_s);
            mbtsc_cb_puts(cb, "\"");
        }
    }
    return 0;
}

/* Lower an `enum`/`const enum` statement starting at `a`.  On success sets
   *pi past the statement.  Returns 1 on success, 0 on refusal. */
static int mbtsc_lower_enum(mbtsc_Ctx *c, size_t a, size_t *pi)
{
    const char *s = c->src;
    size_t len = c->len, i, name_s, name_e, body, close, stmt_end;
    int ok = 0;
    char *nm = NULL;
    size_t nm_len = 0;
    mbtsc_Cb cb;
    int nlines = 0;

    i = mbtsc_skip_ws(s, len, a + 4);   /* past "enum" */
    if (i >= len || !mbtsc_is_ident_start((unsigned char)s[i]))
        return 0;
    name_s = i;
    i = mbtsc_scan_ident_end(s, len, i);
    name_e = i;
    i = mbtsc_skip_ws(s, len, i);
    if (i >= len || s[i] != '{')
        return 0;
    body = i;
    close = mbtsc_match_brace(s, len, body, &ok);
    if (!ok)
        return 0;

    /* count newlines in the statement span for line preservation */
    {
        size_t k = a;
        for (; k <= close; k++)
            if (k < len && s[k] == '\n') nlines++;
    }

    nm_len = name_e - name_s;
    nm = (char *)malloc(nm_len + 1);
    if (!nm)
        return 0;
    memcpy(nm, s + name_s, nm_len);
    nm[nm_len] = '\0';

    memset(&cb, 0, sizeof(cb));
    mbtsc_cb_puts(&cb, "var ");
    mbtsc_cb_putn(&cb, nm, nm_len);
    mbtsc_cb_puts(&cb, "; (function (");
    mbtsc_cb_putn(&cb, nm, nm_len);
    mbtsc_cb_puts(&cb, ") { ");
    if (mbtsc_enum_body(c, s, len, body + 1, close - 1, nm, &cb) != 0) {
        free(nm); free(cb.p); free(c->splices);
        c->splices = NULL; c->nsplices = 0; c->splice_cap = 0;
        return 0;
    }
    mbtsc_cb_puts(&cb, "; })(");
    mbtsc_cb_putn(&cb, nm, nm_len);
    mbtsc_cb_puts(&cb, " || (");
    mbtsc_cb_putn(&cb, nm, nm_len);
    mbtsc_cb_puts(&cb, " = {}));");

    /* statement end: past the closing '}', then optional ';' and ws */
    stmt_end = close + 1;
    while (stmt_end < len && mbtsc_is_ws((unsigned char)s[stmt_end]) &&
           s[stmt_end] != '\n')
        stmt_end++;
    if (stmt_end < len && s[stmt_end] == ';')
        stmt_end++;

    /* pad to the original newline count (line numbers stay valid) */
    {
        int cnew = 0;
        size_t k;
        for (k = 0; k < cb.len; k++)
            if (cb.p[k] == '\n') cnew++;
        while (cnew < nlines) {
            mbtsc_cb_putc(&cb, '\n');
            cnew++;
        }
    }

    if (mbtsc_splice(c, a, stmt_end, cb.p, cb.len) != 0) {
        free(nm); free(cb.p);
        return 0;
    }
    /* erase the source span in place (preserve newlines) */
    mbtsc_erase(c, a, stmt_end);

    free(nm);
    free(cb.p);
    *pi = stmt_end;
    c->prev_sig = ';';
    c->prev_expr = 0;
    c->at_stmt = 1;
    return 1;
}

/* Lower constructor parameter properties: inject `this.X = X;` at the top of
   the constructor body.  `after_paren` = offset just past the ')'.
   Returns 1 on success, 0 if it had to refuse. */
static int mbtsc_lower_ctor_cprops(mbtsc_Ctx *c, const char *s, size_t len,
                                   size_t after_paren)
{
    size_t j = mbtsc_skip_ws(s, len, after_paren);
    size_t brace, need = 0, pos = 0;
    int    k;
    char  *code;

    if (c->ncprop <= 0)
        return 1;
    /* optional return type annotation between ')' and the body */
    if (j < len && s[j] == ':') {
        size_t tend = mbtsc_scan_type(s, len, j + 1, 1);
        if (tend)
            j = tend;
    }
    j = mbtsc_skip_ws(s, len, j);
    if (j >= len || s[j] != '{') {
        mbtsc_refuse(c, "constructor parameter properties need a transpiler, use tsc",
                     after_paren);
        c->ncprop = 0;
        return 0;
    }
    brace = j;
    for (k = 0; k < c->ncprop; k++)
        need += 10 + (c->cprop_e[k] - c->cprop_s[k]) * 2;
    code = (char *)malloc(need + 1);
    if (!code) { c->ncprop = 0; return 0; }
    for (k = 0; k < c->ncprop; k++) {
        size_t nl = c->cprop_e[k] - c->cprop_s[k];
        memcpy(code + pos, "this.", 5);      pos += 5;
        memcpy(code + pos, s + c->cprop_s[k], nl); pos += nl;
        memcpy(code + pos, " = ", 3);        pos += 3;
        memcpy(code + pos, s + c->cprop_s[k], nl); pos += nl;
        memcpy(code + pos, "; ", 2);         pos += 2;
    }
    code[pos] = '\0';
    mbtsc_splice(c, brace + 1, brace + 1, code, pos);
    free(code);
    c->ncprop = 0;
    return 1;
}

/* Validate that a namespace statement is plain (bare name + brace body with
   member statements), then set up `pending_ns` so the main loop erases the
   `namespace NAME` keyword and strips types inside the body.  Returns 1. */
/* Lower `namespace NAME {` into a TS-style IIFE header:
       namespace NAME {   ->   var NAME; (function (NAME) {
   The matching '}' is replaced later (when the body frame is popped) with
   `})(NAME || (NAME = {}));`.  Returns 1 on success, 0 on refusal. */
static int mbtsc_lower_namespace(mbtsc_Ctx *c, size_t a, size_t *pi)
{
    const char *s = c->src;
    size_t len = c->len;
    size_t p = mbtsc_skip_ws(s, len, a + 9);   /* past "namespace" */
    size_t ns_s, ns_e, brace;
    size_t nm, hl, p2;
    char  *h;

    if (p >= len || !mbtsc_is_ident_start((unsigned char)s[p]))
        return 0;
    ns_s = p;
    ns_e = mbtsc_scan_ident_end(s, len, p);
    p = mbtsc_skip_ws(s, len, ns_e);
    if (p >= len || s[p] != '{')
        return 0;
    brace = p;
    nm = ns_e - ns_s;
    /* "var NAME; (function (NAME) {" */
    hl = 4 + nm + (sizeof("; (function (") - 1) + nm + (sizeof(") {") - 1);
    h = (char *)malloc(hl + 1);
    if (!h)
        return 0;
    p2 = 0;
    memcpy(h + p2, "var ", sizeof("var ") - 1);            p2 += sizeof("var ") - 1;
    memcpy(h + p2, s + ns_s, nm);                          p2 += nm;
    memcpy(h + p2, "; (function (", sizeof("; (function (") - 1); p2 += sizeof("; (function (") - 1;
    memcpy(h + p2, s + ns_s, nm);                          p2 += nm;
    memcpy(h + p2, ") {", sizeof(") {") - 1);              p2 += sizeof(") {") - 1;
    h[p2] = '\0';
    if (mbtsc_splice(c, a, brace + 1, h, p2) != 0) {
        free(h);
        return 0;
    }
    free(h);
    c->ns_name_s = ns_s;
    c->ns_name_e = ns_e;
    c->pending_ns = 1;
    *pi = brace;              /* scanner continues at the body '{' */
    return 1;
}

MBTSStatus mbts_strip(const char *src, size_t len, int is_tsx,
                      const char **diag_msg, int *diag_line, int *diag_col,
                      char **out, size_t *out_len)
{
    mbtsc_Ctx c;
    char *buf;
    int ok;

    (void)is_tsx;
    if (out) *out = NULL;
    if (out_len) *out_len = 0;

    buf = (char *)malloc(len ? len + 1 : 1);
    if (!buf) {
        if (diag_msg) *diag_msg = "out of memory";
        if (diag_line) *diag_line = 0;
        if (diag_col) *diag_col = 0;
        return MBTS_OOM;
    }
    if (len)
        memcpy(buf, src, len);
    buf[len] = '\0';

    memset(&c, 0, sizeof(c));
    c.src = src;
    c.len = len;
    c.out = buf;
    c.n = 1;

    ok = mbtsc_scan(&c);
    if (!ok) {
        int sp;
        for (sp = 0; sp < c.nsplices; sp++) free(c.splices[sp].code);
        free(c.splices);
        free(buf);
        if (diag_msg) *diag_msg = c.msg ? c.msg : "unsupported TypeScript";
        if (diag_line) *diag_line = 0;
        if (diag_col) *diag_col = 0;
        if (c.at < len || c.at) mbtsc_pos(src, len, c.at, diag_line, diag_col);
        return MBTS_DIAG;
    }

    if (c.nsplices > 0) {
        /* rebuild output, splicing in lowered constructs (enum/...) */
        size_t extra = 0, k;
        size_t out_n;
        int sp;
        char *nb, *dst;
        /* splices may have been recorded out of source order (a namespace
           export is computed eagerly, before the scanner reaches the member
           body) -> sort so the walk below is strictly left-to-right */
        if (c.nsplices > 1)
            qsort(c.splices, (size_t)c.nsplices, sizeof(*c.splices),
                  mbtsc_splice_cmp);
        for (sp = 0; sp < c.nsplices; sp++)
            extra += c.splices[sp].code_len;
        nb = (char *)malloc(len + extra + 1);
        if (!nb) {
            for (sp = 0; sp < c.nsplices; sp++) free(c.splices[sp].code);
            free(c.splices);
            free(buf);
            if (diag_msg) *diag_msg = "out of memory";
            if (diag_line) *diag_line = 0;
            if (diag_col) *diag_col = 0;
            return MBTS_OOM;
        }
        dst = nb;
        k = 0;
        for (sp = 0; sp < c.nsplices; sp++) {
            size_t st = c.splices[sp].start, en = c.splices[sp].end;
            if (en <= k)
                continue;             /* inside a prior span: already emitted */
            if (k < st) { memcpy(dst, buf + k, st - k); dst += st - k; }
            memcpy(dst, c.splices[sp].code, c.splices[sp].code_len);
            dst += c.splices[sp].code_len;
            k = en;
        }
        if (k < len) { memcpy(dst, buf + k, len - k); dst += len - k; }
        *dst = '\0';
        out_n = (size_t)(dst - nb);
        for (sp = 0; sp < c.nsplices; sp++) free(c.splices[sp].code);
        free(c.splices);
        c.splices = NULL; c.nsplices = 0; c.splice_cap = 0;
        free(buf);
        buf = nb;
        if (out) *out = buf;
        if (out_len) *out_len = out_n;
        return MBTS_OK;
    }

    if (out) *out = buf;
    if (out_len) *out_len = len;
    return MBTS_OK;
}
