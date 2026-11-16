/*
 * mbjs command line compiler
 *
 * Copyright (c) 2018-2026 Fabrice Bellard
 * Copyright (c) 2023-2026 Ben Noordhuis
 * Copyright (c) 2023-2026 Saúl Ibarra Corretgé
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>

#include "cutils.h"
#include "mbjs-libc.h"
#include "mbjs-llvm.h"
#include "mbjs-opt.h"
#include "mbjs-superset.h"

#include "mbjsc.h"

/* ---- built-in pseudo-C backend -----------------------------------------
 *
 * The ordinary `-o` output reconstructs readable, C-flavoured pseudo-code
 * from the engine's own compiled representation (the bytecode produced from
 * the parser -> AST -> tree nodes). It deliberately reads like the output of
 * a decompiler / reverse-engineering pass over the MochiBerry machine, not
 * like the embeddable bytecode array emitted by `-b` / `-e`. The result is
 * for humans and tooling; it is not compilable C.
 */

typedef struct {
    const char *name;
    unsigned char size;
    unsigned char n_pop;
    unsigned char n_push;
    const char *fmt;
} PseudoOpInfo;

static const PseudoOpInfo pseudo_ops[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) { #id, (unsigned char)(size), (unsigned char)(n_pop), (unsigned char)(n_push), #f },
#define def(id, size, n_pop, n_push, f)
#include "mbjs-opcode.h"
#undef def
#undef DEF
#undef FMT
};

#define PSEUDO_OP_COUNT ((int)(sizeof(pseudo_ops) / sizeof(pseudo_ops[0])))


static int32_t pseudo_rd_i32(const uint8_t *p)
{
    int32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint32_t pseudo_rd_u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static int16_t pseudo_rd_i16(const uint8_t *p)
{
    int16_t v;
    memcpy(&v, p, 2);
    return v;
}

static uint16_t pseudo_rd_u16(const uint8_t *p)
{
    uint16_t v;
    memcpy(&v, p, 2);
    return v;
}

static char *pseudo_fmt(const char *fmt, ...)
{
    va_list ap;
    int n;
    char *s;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        return NULL;
    s = (char *)malloc((size_t)n + 1);
    if (!s)
        return NULL;
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

static void pseudo_free_stack(PseudoCtx *c)
{
    while (c->depth > 0)
        free(c->stack[--c->depth]);
}

static void pseudo_push(PseudoCtx *c, char *s)
{
    if (!s)
        return;
    if (c->depth >= (int)(sizeof(c->stack) / sizeof(c->stack[0])) - 1) {
        free(s);
        return;
    }
    c->stack[c->depth++] = s;
}

static char *pseudo_pop(PseudoCtx *c)
{
    if (c->depth <= 0)
        return pseudo_fmt("undefined");
    return c->stack[--c->depth];
}

static char *pseudo_newv(PseudoCtx *c, const char *rhs)
{
    char *name = pseudo_fmt("v%d", c->next_v++);
    if (!name)
        return NULL;
    fprintf(c->fo, "  MBJSValue %s = %s;\n", name, rhs);
    return name;
}

/* Index encoded in a short-form opcode name ("get_loc2" -> 2). */
static int pseudo_named_idx(const char *name, const char *prefix, int max)
{
    size_t pl = strlen(prefix);
    if (strncmp(name, prefix, pl) != 0)
        return -1;
    if (name[pl] < '0' || name[pl] > (char)('0' + max))
        return -1;
    if (name[pl + 1] != '\0')
        return -1;
    return name[pl] - '0';
}

static int pseudo_branch_target(const char *n, const uint8_t *bc, size_t pc)
{
    if (!strcmp(n, "goto") || !strcmp(n, "if_true") || !strcmp(n, "if_false") ||
        !strcmp(n, "catch") || !strcmp(n, "gosub"))
        return (int)pc + 1 + pseudo_rd_i32(bc + pc + 1);
    if (!strcmp(n, "goto8") || !strcmp(n, "if_true8") ||
        !strcmp(n, "if_false8"))
        return (int)pc + 1 + (int)(int8_t)bc[pc + 1];
    if (!strcmp(n, "goto16"))
        return (int)pc + 1 + (int)pseudo_rd_i16(bc + pc + 1);
    return -1;
}

static void pseudo_binop(PseudoCtx *c, const char *sym)
{
    char *b = pseudo_pop(c), *a = pseudo_pop(c);
    char *rhs = pseudo_fmt("(%s %s %s)", a, b, sym);
    char *v = pseudo_newv(c, rhs ? rhs : "undefined");
    free(a);
    free(b);
    free(rhs);
    pseudo_push(c, v);
}

static const char *pseudo_binop_sym(const char *n)
{
    static const struct { const char *name, *sym; } t[] = {
        { "add", "+" }, { "sub", "-" }, { "mul", "*" }, { "div", "/" },
        { "mod", "%" }, { "pow", "**" }, { "shl", "<<" }, { "sar", ">>" },
        { "shr", ">>>" }, { "and", "&" }, { "xor", "^" }, { "or", "|" },
        { "lt", "<" }, { "lte", "<=" }, { "gt", ">" }, { "gte", ">=" },
        { "eq", "==" }, { "neq", "!=" }, { "strict_eq", "===" },
        { "strict_neq", "!==" }, { "instanceof", "instanceof" },
        { "in", "in" },
    };
    size_t i;
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        if (!strcmp(n, t[i].name))
            return t[i].sym;
    return NULL;
}

void pseudo_emit_function(MBJSContext *ctx, FILE *fo, MBJSValue fn,
                                 const char *filename, const char *cname)
{
    const uint8_t *bc;
    size_t len, pc;
    PseudoCtx c;
    unsigned char *is_target;

    (void)ctx;
    if (MBJS_GetFunctionBytecodeBuffer(fn, &bc, &len) < 0 || len == 0) {
        fprintf(fo, "/* %s: not a bytecode function; nothing to render */\n",
                filename);
        return;
    }
    is_target = (unsigned char *)calloc(len, 1);
    if (!is_target)
        return;
    memset(&c, 0, sizeof(c));
    c.fo = fo;

    /* First pass: mark branch targets so we can emit labels. */
    for (pc = 0; pc < len;) {
        int op = bc[pc], sz, t;
        const char *n;
        if (op < 0 || op >= PSEUDO_OP_COUNT)
            break;
        sz = pseudo_ops[op].size;
        if (sz <= 0 || pc + (size_t)sz > len)
            break;
        n = pseudo_ops[op].name;
        t = pseudo_branch_target(n, bc, pc);
        if (t >= 0 && (size_t)t < len)
            is_target[t] = 1;
        pc += (size_t)sz;
    }

    fprintf(fo,
            "\n/* %s -- pseudo-C reconstructed from the MochiBerry compiled\n"
            " * representation (parser -> AST -> tree nodes -> bytecode).\n"
            " * For reading/tooling only: this is not compilable C. */\n",
            filename);
    fprintf(fo, "MBJSValue %s(void)\n{\n", cname);

    for (pc = 0; pc < len;) {
        const PseudoOpInfo *oi;
        const char *n;
        int op, size, t, li;
        char *a, *b, *v, *rhs;

        op = bc[pc];
        if (op < 0 || op >= PSEUDO_OP_COUNT)
            break;
        oi = &pseudo_ops[op];
        size = oi->size;
        if (size <= 0 || pc + (size_t)size > len)
            break;
        n = oi->name;

        if (is_target[pc]) {
            fprintf(fo, "L%u:\n", (unsigned)pc);
            pseudo_free_stack(&c); /* stack shape across joins is unknown */
        }
        fprintf(fo, "  /* %04u: %s */\n", (unsigned)pc, n);

        /* literal pushes */
        if (!strcmp(n, "push_i32") || !strcmp(n, "push_bigint_i32")) {
            pseudo_push(&c, pseudo_fmt("%d", pseudo_rd_i32(bc + pc + 1)));
        } else if (!strcmp(n, "push_i8")) {
            pseudo_push(&c, pseudo_fmt("%d", (int)(int8_t)bc[pc + 1]));
        } else if (!strcmp(n, "push_i16")) {
            pseudo_push(&c, pseudo_fmt("%d", (int)pseudo_rd_i16(bc + pc + 1)));
        } else if (!strcmp(n, "push_minus1")) {
            pseudo_push(&c, pseudo_fmt("-1"));
        } else if (strlen(n) == 6 && !strncmp(n, "push_", 5) &&
                   n[5] >= '0' && n[5] <= '7') {
            pseudo_push(&c, pseudo_fmt("%c", n[5]));
        } else if (!strcmp(n, "push_true")) {
            pseudo_push(&c, pseudo_fmt("true"));
        } else if (!strcmp(n, "push_false")) {
            pseudo_push(&c, pseudo_fmt("false"));
        } else if (!strcmp(n, "push_null")) {
            pseudo_push(&c, pseudo_fmt("null"));
        } else if (!strcmp(n, "null")) {
            pseudo_push(&c, pseudo_fmt("null"));
        } else if (!strcmp(n, "undefined")) {
            pseudo_push(&c, pseudo_fmt("undefined"));
        } else if (!strcmp(n, "push_empty_string")) {
            pseudo_push(&c, pseudo_fmt("\"\""));
        } else if (!strcmp(n, "push_this")) {
            pseudo_push(&c, pseudo_fmt("this"));
        } else if (!strcmp(n, "object")) {
            pseudo_push(&c, pseudo_fmt("{}"));
        } else if (!strcmp(n, "push_const") || !strcmp(n, "push_const8") ||
                   !strcmp(n, "push_atom_value") || !strcmp(n, "private_symbol")) {
            unsigned idx = (size >= 5) ? pseudo_rd_u32(bc + pc + 1) : bc[pc + 1];
            pseudo_push(&c, pseudo_fmt("const(#%u)", idx));
        } else if (!strcmp(n, "fclosure") || !strcmp(n, "fclosure8")) {
            unsigned idx = (size >= 5) ? pseudo_rd_u32(bc + pc + 1) : bc[pc + 1];
            pseudo_push(&c, pseudo_fmt("closure(#%u)", idx));
        /* arithmetic / comparison */
        } else if ((rhs = (char *)pseudo_binop_sym(n)) != NULL) {
            pseudo_binop(&c, rhs);
        /* fields / elements */
        } else if (!strcmp(n, "get_field") || !strcmp(n, "get_field2")) {
            unsigned idx = pseudo_rd_u32(bc + pc + 1);
            a = pseudo_pop(&c);
            v = pseudo_fmt("%s.field%u", a, idx);
            if (!strcmp(n, "get_field2"))
                pseudo_push(&c, pseudo_fmt("%s", a));
            free(a);
            pseudo_push(&c, v);
        } else if (!strcmp(n, "put_field")) {
            unsigned idx = pseudo_rd_u32(bc + pc + 1);
            b = pseudo_pop(&c);
            a = pseudo_pop(&c);
            fprintf(fo, "  %s.field%u = %s;\n", a, idx, b);
            free(a);
            free(b);
        } else if (!strcmp(n, "get_array_el") || !strcmp(n, "get_array_el2")) {
            b = pseudo_pop(&c);
            a = pseudo_pop(&c);
            v = pseudo_fmt("%s[%s]", a, b);
            if (!strcmp(n, "get_array_el2"))
                pseudo_push(&c, pseudo_fmt("%s", a));
            free(a);
            free(b);
            pseudo_push(&c, v);
        } else if (!strcmp(n, "put_array_el")) {
            v = pseudo_pop(&c);
            b = pseudo_pop(&c);
            a = pseudo_pop(&c);
            fprintf(fo, "  %s[%s] = %s;\n", a, b, v);
            free(a);
            free(b);
            free(v);
        } else if (!strcmp(n, "get_length")) {
            a = pseudo_pop(&c);
            pseudo_push(&c, pseudo_fmt("%s.length", a));
            free(a);
        /* locals / arguments / closures */
        } else if (!strcmp(n, "get_loc")) {
            pseudo_push(&c, pseudo_fmt("loc[%u]", pseudo_rd_u16(bc + pc + 1)));
        } else if (!strcmp(n, "get_loc8")) {
            pseudo_push(&c, pseudo_fmt("loc[%u]", bc[pc + 1]));
        } else if (!strcmp(n, "get_loc0_loc1")) {
            pseudo_push(&c, pseudo_fmt("loc[0]"));
            pseudo_push(&c, pseudo_fmt("loc[1]"));
        } else if ((li = pseudo_named_idx(n, "get_loc", 3)) >= 0) {
            pseudo_push(&c, pseudo_fmt("loc[%d]", li));
        } else if (!strcmp(n, "put_loc") || !strcmp(n, "put_loc8")) {
            unsigned idx = (size >= 3) ? pseudo_rd_u16(bc + pc + 1) : bc[pc + 1];
            a = pseudo_pop(&c);
            fprintf(fo, "  loc[%u] = %s;\n", idx, a);
            free(a);
        } else if ((li = pseudo_named_idx(n, "put_loc", 3)) >= 0) {
            a = pseudo_pop(&c);
            fprintf(fo, "  loc[%d] = %s;\n", li, a);
            free(a);
        } else if (!strcmp(n, "set_loc") || !strcmp(n, "set_loc8")) {
            unsigned idx = (size >= 3) ? pseudo_rd_u16(bc + pc + 1) : bc[pc + 1];
            a = pseudo_pop(&c);
            fprintf(fo, "  loc[%u] = %s;\n", idx, a);
            pseudo_push(&c, pseudo_fmt("loc[%u]", idx));
            free(a);
        } else if ((li = pseudo_named_idx(n, "set_loc", 3)) >= 0) {
            a = pseudo_pop(&c);
            fprintf(fo, "  loc[%d] = %s;\n", li, a);
            pseudo_push(&c, pseudo_fmt("loc[%d]", li));
            free(a);
        } else if (!strcmp(n, "get_arg")) {
            pseudo_push(&c, pseudo_fmt("arg[%u]", pseudo_rd_u16(bc + pc + 1)));
        } else if ((li = pseudo_named_idx(n, "get_arg", 3)) >= 0) {
            pseudo_push(&c, pseudo_fmt("arg[%d]", li));
        } else if (!strcmp(n, "put_arg")) {
            a = pseudo_pop(&c);
            fprintf(fo, "  arg[%u] = %s;\n", pseudo_rd_u16(bc + pc + 1), a);
            free(a);
        } else if ((li = pseudo_named_idx(n, "put_arg", 3)) >= 0) {
            a = pseudo_pop(&c);
            fprintf(fo, "  arg[%d] = %s;\n", li, a);
            free(a);
        } else if (!strcmp(n, "get_var_ref")) {
            pseudo_push(&c, pseudo_fmt("ref[%u]", pseudo_rd_u16(bc + pc + 1)));
        } else if ((li = pseudo_named_idx(n, "get_var_ref", 3)) >= 0) {
            pseudo_push(&c, pseudo_fmt("ref[%d]", li));
        } else if (!strcmp(n, "get_var")) {
            pseudo_push(&c, pseudo_fmt("global.atom%u", pseudo_rd_u32(bc + pc + 1)));
        } else if (!strcmp(n, "get_var_undef")) {
            pseudo_push(&c, pseudo_fmt("global.atom%u", pseudo_rd_u32(bc + pc + 1)));
        } else if (!strcmp(n, "put_var") || !strcmp(n, "put_var_init")) {
            a = pseudo_pop(&c);
            fprintf(fo, "  global.atom%u = %s;\n", pseudo_rd_u32(bc + pc + 1), a);
            free(a);
        /* stack juggling */
        } else if (!strcmp(n, "drop")) {
            a = pseudo_pop(&c);
            fprintf(fo, "  /* value dropped */\n");
            free(a);
        } else if (!strcmp(n, "dup") || !strcmp(n, "dup1") ||
                   !strcmp(n, "dup2") || !strcmp(n, "dup3")) {
            /* Keep the model's stack depth consistent: duplicate the top as
             * many times as the opcode grows the stack. */
            int k, extra = (int)oi->n_push - (int)oi->n_pop;
            for (k = 0; k < extra && c.depth > 0; k++)
                pseudo_push(&c, pseudo_fmt("%s", c.stack[c.depth - 1]));
        } else if (!strcmp(n, "swap")) {
            if (c.depth >= 2) {
                char *tmp = c.stack[c.depth - 1];
                c.stack[c.depth - 1] = c.stack[c.depth - 2];
                c.stack[c.depth - 2] = tmp;
            }
        /* control flow */
        } else if (!strcmp(n, "goto") || !strcmp(n, "goto8") ||
                   !strcmp(n, "goto16")) {
            t = pseudo_branch_target(n, bc, pc);
            fprintf(fo, "  goto L%d;\n", t);
            pseudo_free_stack(&c);
        } else if (!strcmp(n, "if_true") || !strcmp(n, "if_true8")) {
            t = pseudo_branch_target(n, bc, pc);
            a = pseudo_pop(&c);
            fprintf(fo, "  if (%s) goto L%d;\n", a, t);
            free(a);
        } else if (!strcmp(n, "if_false") || !strcmp(n, "if_false8")) {
            t = pseudo_branch_target(n, bc, pc);
            a = pseudo_pop(&c);
            fprintf(fo, "  if (!(%s)) goto L%d;\n", a, t);
            free(a);
        } else if (!strcmp(n, "return")) {
            a = pseudo_pop(&c);
            fprintf(fo, "  return %s;\n", a);
            free(a);
        } else if (!strcmp(n, "return_undef")) {
            fprintf(fo, "  return undefined;\n");
        } else if (!strcmp(n, "throw")) {
            a = pseudo_pop(&c);
            fprintf(fo, "  throw %s;\n", a);
            free(a);
        /* calls */
        } else if (!strcmp(n, "call") || !strcmp(n, "tail_call") ||
                   (strlen(n) == 5 && !strncmp(n, "call", 4) &&
                    n[4] >= '0' && n[4] <= '3')) {
            int argc = (!strcmp(n, "call") || !strcmp(n, "tail_call"))
                           ? (int)pseudo_rd_u16(bc + pc + 1) : (n[4] - '0');
            char *args[5] = { NULL, NULL, NULL, NULL, NULL };
            int k;
            for (k = argc - 1; k >= 0; k--) {
                char *x = pseudo_pop(&c);
                if (k < 4)
                    args[k] = x;
                else
                    free(x);
            }
            a = pseudo_pop(&c); /* function */
            if (argc == 0)
                rhs = pseudo_fmt("%s()", a);
            else if (argc == 1)
                rhs = pseudo_fmt("%s(%s)", a, args[0]);
            else if (argc == 2)
                rhs = pseudo_fmt("%s(%s, %s)", a, args[0], args[1]);
            else if (argc == 3)
                rhs = pseudo_fmt("%s(%s, %s, %s)", a, args[0], args[1], args[2]);
            else if (argc == 4)
                rhs = pseudo_fmt("%s(%s, %s, %s, %s)", a, args[0], args[1],
                                 args[2], args[3]);
            else
                rhs = pseudo_fmt("%s(/* %d args */)", a, argc);
            v = pseudo_newv(&c, rhs ? rhs : "undefined");
            free(a);
            for (k = 0; k < 4; k++)
                free(args[k]);
            free(rhs);
            pseudo_push(&c, v);
        } else if (!strcmp(n, "call_method") || !strcmp(n, "tail_call_method")) {
            int argc = (int)pseudo_rd_u16(bc + pc + 1);
            char *args[5] = { NULL, NULL, NULL, NULL, NULL };
            int k;
            for (k = argc - 1; k >= 0; k--) {
                char *x = pseudo_pop(&c);
                if (k < 4)
                    args[k] = x;
                else
                    free(x);
            }
            b = pseudo_pop(&c); /* function */
            a = pseudo_pop(&c); /* this */
            rhs = pseudo_fmt("%s.call(%s%s%s%s%s)", b, a,
                             argc > 0 ? ", " : "", argc > 0 ? args[0] : "",
                             argc > 1 ? ", " : "", argc > 1 ? args[1] : "");
            v = pseudo_newv(&c, rhs ? rhs : "undefined");
            free(a);
            free(b);
            for (k = 0; k < 4; k++)
                free(args[k]);
            free(rhs);
            pseudo_push(&c, v);
        /* unary */
        } else if (!strcmp(n, "neg") || !strcmp(n, "plus") ||
                   !strcmp(n, "lnot") || !strcmp(n, "not") ||
                   !strcmp(n, "typeof")) {
            a = pseudo_pop(&c);
            if (!strcmp(n, "neg"))
                rhs = pseudo_fmt("(-%s)", a);
            else if (!strcmp(n, "plus"))
                rhs = pseudo_fmt("(+%s)", a);
            else if (!strcmp(n, "lnot"))
                rhs = pseudo_fmt("(!%s)", a);
            else if (!strcmp(n, "not"))
                rhs = pseudo_fmt("(~%s)", a);
            else
                rhs = pseudo_fmt("(typeof %s)", a);
            v = pseudo_newv(&c, rhs ? rhs : "undefined");
            free(a);
            free(rhs);
            pseudo_push(&c, v);
        /* generic fallback: keep the model consistent */
        } else {
            int k;
            fprintf(fo, "  /* %s: not lowered (operands", n);
            for (k = 1; k < size; k++)
                fprintf(fo, " %02x", bc[pc + k]);
            fprintf(fo, ") */\n");
            for (k = 0; k < oi->n_pop; k++) {
                a = pseudo_pop(&c);
                free(a);
            }
            for (k = 0; k < oi->n_push; k++)
                pseudo_push(&c, pseudo_fmt("undef"));
        }
        pc += (size_t)size;
    }

    fprintf(fo, "  return undefined;\n}\n");
    pseudo_free_stack(&c);
    free(is_target);
}
