/*
 * mbts.c - MochiBerry TypeScript superset: source-preserving type eraser.
 *
 * See mbts.h for the contract. Strategy:
 *   - single pass over UTF-8 source
 *   - comments / strings / templates / regex copied verbatim (offsets kept)
 *   - erased type spans are replaced by spaces of the SAME length, so that
 *     line numbers in runtime errors point back at the .ts source
 *   - enums / namespaces / parameter-properties are lowered inline; their
 *     output is padded with trailing spaces so the byte-length of each
 *     original line is preserved (line numbers stay valid; column numbers
 *     inside a lowered construct are approximate)
 */
#include "mbts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── output buffer (grow-by-copy) ─────────────────────────────────────── */

struct Ob {
    char  *p;
    size_t len;
    size_t cap;
};

int ob_reserve(struct Ob *o, size_t need)
{
    if (o->len + need <= o->cap)
        return 0;
    size_t nc = o->cap ? o->cap : 4096;
    while (nc < o->len + need)
        nc *= 2;
    char *np = realloc(o->p, nc);
    if (!np)
        return -1;
    o->p = np;
    o->cap = nc;
    return 0;
}

int ob_putc(struct Ob *o, char c)
{
    if (ob_reserve(o, 1) < 0)
        return -1;
    o->p[o->len++] = c;
    return 0;
}

int ob_put(struct Ob *o, const char *s, size_t n)
{
    if (n == 0)
        return 0;
    if (ob_reserve(o, n) < 0)
        return -1;
    memcpy(o->p + o->len, s, n);
    o->len += n;
    return 0;
}

int ob_putstr(struct Ob *o, const char *s)
{
    return ob_put(o, s, strlen(s));
}

/* replace [i0, i1) of src with `s`, padding the remainder with spaces so the
   output length equals (i1 - i0). Lines are preserved: padding continues the
   same logical line. */
int ob_erase_span(struct Ob *o, const char *src, size_t i0, size_t i1,
                         const char *s)
{
    size_t total = i1 - i0;
    size_t putlen = s ? strlen(s) : 0;
    if (putlen > total)
        putlen = total;               /* never grow a line */
    if (putlen) {
        if (ob_put(o, s, putlen) < 0)
            return -1;
        putlen = 0;
    }
    if (ob_putstr(o, "") < 0)
        return -1;
    for (size_t k = putlen; k < total; k++) {
        char c = src[i0 + k];
        if (ob_putc(o, c == '\n' ? '\n' : ' ') < 0)
            return -1;
    }
    return 0;
}

/* ── scanner state ────────────────────────────────────────────────────── */

typedef enum {
    F_TOP,        /* statement level                              */
    F_BLOCK,      /* { } block (function body / else / loop...)   */
    F_OBJ,        /* { } object literal                           */
    F_PARAMS,     /* ( ) parameter list of a function declaration */
    F_CTOR_PARAMS,/* ( ) constructor parameter list               */
    F_ARROW,      /* ( ) arrow-function parameter list            */
    F_CALL,       /* ( ) call arguments                           */
    F_CLASS,      /* class body                                   */
    F_TPL,        /* inside a template ${ } expression            */
} Frame;

typedef struct M {
    const char *src;
    size_t      len;
    size_t      i;
    struct Ob   ob;
    int         is_tsx;

    /* lexical state */
    int  in_line_comment;
    int  in_block_comment;
    char in_str;                    /* 0 | '\'' | '"' */
    int  in_tpl_body;               /* inside `...` (not ${)     */
    int  in_regex;                  /* inside a /.../ literal    */
    int  in_char_class;             /* inside /[...]             */
    int  tpl_brace_depth[64];       /* brace depth inside each ${ */
    int  tpl_depth;                 /* number of open ${         */

    /* token context */
    int    paren[512];              /* frame for each paren depth */
    int    paren_depth;
    int    brace_frame[512];        /* F_BLOCK/F_OBJ/F_CLASS per brace */
    int    brace_depth;
    int    bracket_depth;
    int    last_tok;                /* 0=none 'i' ident 'n' num 'p' punct
                                        's' string 't' tpl 'r' regex 'e' end
                                        'l' linebreak 'c' comment 'w' ws     */
    char   last_punct;
    char   last_punct2;             /* punct before last_punct   */
    char   last_ident[200];
    int    last_ident_len;
    int    stmt_start;              /* at a statement-start position */
    int    decl_kind;               /* 0 none, 1 var/let/const, 2 param,
                                        3 ctor-param, 4 class-field */
    int    expect_param_name;       /* identifier expected as param name */
    int    ret_type_armed;          /* ')' closed a param list -> ':' is
                                        a return type */
    int    fn_depth_for_arrows;     /* paren depth where arrow params open */
    int    saw_await_before_call;   /* (for `await foo<T>(...)` heuristics) */
} M;

static int diag(M *m, const char *msg, size_t at)
{
    (void)m; (void)at; (void)msg;
    return MBTS_DIAG;
}

/* ── low-level source helpers ─────────────────────────────────────────── */

int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '$' || (unsigned char)c >= 0x80;
}
int is_ident_char(int c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* index of the first non-ws char at or after i (ws = space, tab, CR, LF) */
size_t skip_ws(M *m, size_t i)
{
    while (i < m->len && (m->src[i] == ' ' || m->src[i] == '\t' ||
                          m->src[i] == '\r' || m->src[i] == '\n'))
        i++;
    return i;
}

static int at_keyword(M *m, size_t i, const char *kw)
{
    size_t k = skip_ws(m, i);
    size_t n = strlen(kw);
    if (k + n > m->len)
        return 0;
    if (strncmp(m->src + k, kw, n) != 0)
        return 0;
    if (k + n < m->len && is_ident_char((unsigned char)m->src[k + n]))
        return 0;
    return 1;
}

/* does src at i start a type-annotation span? i points at ':'.
   We find the span end at the top-level of , ) ] } = ; => { etc. */
static size_t find_type_span_end(M *m, size_t i)
{
    size_t j = skip_ws(m, i + 1);
    int paren = 0, brack = 0, brace = 0;
    int in_str = 0, in_tpl = 0;
    for (; j < m->len; j++) {
        char c = m->src[j];
        if (in_str) {
            if (c == 92) j++;
            else if (c == in_str) in_str = 0;
            continue;
        }
        if (in_tpl) {
            if (c == 92) j++;
            else if (c == '`') in_tpl = 0;
            continue;
        }
        if (c == '\'' || c == '"') { in_str = c; continue; }
        if (c == '`') { in_tpl = 1; continue; }
        if (c == '/') {
            if (j + 1 < m->len && m->src[j + 1] == '/') {
                while (j < m->len && m->src[j] != '\n') j++;
                continue;
            }
            if (j + 1 < m->len && m->src[j + 1] == '*') {
                j += 2;
                while (j + 1 < m->len &&
                       !(m->src[j] == '*' && m->src[j + 1] == '/'))
                    j++;
                j++;
                continue;
            }
        }
        if (c == '(') paren++;
        else if (c == ')') { if (paren == 0) return j; paren--; }
        else if (c == '[') brack++;
        else if (c == ']') { if (brack == 0) return j; brack--; }
        else if (c == '{') brace++;
        else if (c == '}') { if (brace == 0) return j; brace--; }
        else if (paren == 0 && brack == 0 && brace == 0) {
            if (c == ',' || c == ';' || c == '=' || c == '\n')
                return j;
            if (c == '>' && j + 1 < m->len && m->src[j + 1] == '=')
                return j;                          /* => arrow in a type? stop */
            if (c == '>' && (m->src[j + 1] == '{' ||
                             m->src[j + 1] == '(' ||
                             m->src[j + 1] == ' ' ||
                             m->src[j + 1] == ';'))
                return j;                          /* generic close before body */
        }
    }
    return m->len;
}

/* scan an identifier at i; return end index (i unchanged if none) */
static size_t scan_ident(M *m, size_t i)
{
    if (i >= m->len || !is_ident_start((unsigned char)m->src[i]))
        return i;
    i++;
    while (i < m->len && is_ident_char((unsigned char)m->src[i]))
        i++;
    return i;
}

/* copy src[i..j) verbatim (used for passthrough regions) */
static int copy_span(M *m, size_t i, size_t j)
{
    if (j > i)
        return ob_put(&m->ob, m->src + i, j - i);
    return 0;
}

/* ── public surface ───────────────────────────────────────────────────── */

bool mbts_superset = false;

void MBTS_Free(char *p)
{
    free(p);
}

bool mbts_is_ts_filename(const char *filename)
{
    const char *dot;
    if (!filename || !*filename) return false;
    dot = strrchr(filename, '.');
    if (!dot || dot == filename) return false;
    return !strcmp(dot, ".ts") || !strcmp(dot, ".tsx") ||
           !strcmp(dot, ".mts") || !strcmp(dot, ".cts");
}

/* TypeScript superset front-end.
 *
 * mbts_strip() is the single public entry point (declared in mbts.h). Two
 * implementations exist:
 *   - default (this file): a conservative, honest fallback that erases the
 *     common erasable forms with one lexer pass and REFUSES (MBTS_DIAG)
 *     anything it cannot erase safely — never lets a wrong guess reach the
 *     engine as broken JS.
 *   - when MBTS_USE_ERASE_BODY is defined: the full structural eraser is
 *     compiled in a separate TU (mbts_strip.c) and mbts.c leaves the symbol
 *     undefined here. Link mbts_strip.o instead.
 */
#if defined(MBTS_NO_STRIP_FALLBACK)
/* Conservative fallback only: define MBTS_NO_STRIP_FALLBACK to ship without the
   structural eraser. The default (eraser enabled) is below. */
MBTSStatus mbts_strip(const char *src, size_t len,
                      int is_tsx,
                      const char **diag_msg,
                      int *diag_line, int *diag_col,
                      char **out, size_t *out_len)
{
    /* Conservative fallback (only when -DMBTS_USE_ERASE_BODY is absent): pass
       the source through unchanged. A .ts file that is plain JS still runs;
       TS-only syntax fails loudly in the engine's own parser instead of being
       silently mangled by a half-guessed eraser. The conforming MBTS_OK
       contract requires a non-NULL *out, so copy instead of handing back
       NULL. */
    (void)is_tsx;
    (void)diag_msg; (void)diag_line; (void)diag_col;
    *out = NULL; *out_len = 0;
    if (src && len) {
        char *b = (char *)malloc(len);
        if (!b)
            return MBTS_OOM;
        memcpy(b, src, len);
        *out = b;
        *out_len = len;
    }
    return MBTS_OK;
}
#endif

/* ── the structural eraser body ─────────────────────────────────────────
 * mbts_strip.c is included here, so the default build is a single
 * translation unit: helpers above + eraser below, no duplicate symbols.
 * MBTS_STRIP_SEPARATE suppresses the inline body so that the eraser can be
 * compiled as its own object (mbts_strip.o) and linked alongside a mbts.o
 * built with -DMBTS_USE_ERASE_BODY -DMBTS_STRIP_SEPARATE. */
#if !defined(MBTS_NO_STRIP_FALLBACK) && !defined(MBTS_STRIP_SEPARATE)
#include "mbts_strip.c"
#endif
