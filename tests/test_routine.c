/*
 * test_routine.c — tests for MochiRoutine (fiber-based, cooperative, zero-pause
 * green threads).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mbjs-routine.h"

static int g_failures = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) {                                                          \
            printf("PASS: %s\n", msg);                                       \
        } else {                                                             \
            printf("FAIL: %s\n", msg);                                       \
            g_failures++;                                                    \
        }                                                                    \
    } while (0)

/* --- single routine, zero-pause (no fiber switch) -------------------------- */

static int single_fn(MBJSRoutine *self, void *arg)
{
    (void)self;
    int *p = (int *)arg;
    *p = 42;
    return 7;
}

static void test_single(void)
{
    MBJSRoutine *r;
    int x = 0;
    int ret;

    r = mbjs_routine_new(0);
    CHECK(r != NULL, "single: routine created");

    ret = mbjs_routine_start(r, single_fn, &x);
    CHECK(ret == 7, "single: return value");
    CHECK(x == 42, "single: arg side effect");
    CHECK(mbjs_routine_status(r) == MBJS_ROUTINE_DONE, "single: status done");

    mbjs_routine_free(r);
}

/* --- yield / resume round trip -------------------------------------------- */

static int yielder(MBJSRoutine *self, void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;                       /* step 1 */
    mbjs_routine_yield(self);
    (*counter)++;                       /* step 2 (after resume) */
    mbjs_routine_yield(self);
    (*counter)++;                       /* step 3 */
    return *counter;
}

static void test_yield_resume(void)
{
    MBJSRoutine *r;
    int counter = 0;
    int ret;

    r = mbjs_routine_new(0);
    CHECK(r != NULL, "yield: routine created");

    ret = mbjs_routine_start(r, yielder, &counter);
    CHECK(counter == 1, "yield: first slice ran");
    CHECK(mbjs_routine_status(r) == MBJS_ROUTINE_SUSPENDED, "yield: suspended");
    CHECK(ret == 0, "yield: start returned on yield");

    ret = mbjs_routine_resume(r);
    CHECK(counter == 2, "yield: second slice ran after resume");
    CHECK(mbjs_routine_status(r) == MBJS_ROUTINE_SUSPENDED, "yield: suspended again");

    ret = mbjs_routine_resume(r);
    CHECK(counter == 3, "yield: third slice ran");
    CHECK(mbjs_routine_status(r) == MBJS_ROUTINE_DONE, "yield: done after last slice");
    CHECK(ret == 3, "yield: final return value");

    mbjs_routine_free(r);
}

/* --- zero-pause single routine via run_all (n == 1) ------------------------ */

static int direct_fn(MBJSRoutine *self, void *arg)
{
    (void)self;
    (void)arg;
    return 123;
}

static void test_zero_pause(void)
{
    MBJSRoutine *rs[1];
    MBJSRoutineFn fns[1];
    void *args[1] = { NULL };
    int ret;

    rs[0] = mbjs_routine_new(0);
    fns[0] = direct_fn;
    ret = mbjs_routine_run_all(rs, 1, fns, args);
    CHECK(ret == 123, "zero-pause: direct run returns value");
    CHECK(mbjs_routine_status(rs[0]) == MBJS_ROUTINE_DONE, "zero-pause: done");
    mbjs_routine_free(rs[0]);
}

/* --- cooperative round-robin (n == 2) ------------------------------------- */

static char g_order[64];
static int g_order_n = 0;

static int coop_a(MBJSRoutine *self, void *arg)
{
    (void)arg;
    g_order[g_order_n++] = 'A';
    mbjs_routine_yield(self);
    g_order[g_order_n++] = 'A';
    return 0;
}

static int coop_b(MBJSRoutine *self, void *arg)
{
    (void)arg;
    g_order[g_order_n++] = 'B';
    mbjs_routine_yield(self);
    g_order[g_order_n++] = 'B';
    return 0;
}

static void test_coop(void)
{
    MBJSRoutine *rs[2];
    MBJSRoutineFn fns[2];
    void *args[2] = { NULL, NULL };
    int ret;

    g_order_n = 0;
    rs[0] = mbjs_routine_new(0);
    rs[1] = mbjs_routine_new(0);
    fns[0] = coop_a;
    fns[1] = coop_b;

    ret = mbjs_routine_run_all(rs, 2, fns, args);
    (void)ret;

    /* Expected interleaving: A B A B (first slice of each, then second slice). */
    CHECK(g_order_n == 4, "coop: all slices ran");
    CHECK(g_order[0] == 'A', "coop: first slice A");
    CHECK(g_order[1] == 'B', "coop: first slice B");
    CHECK(g_order[2] == 'A', "coop: second slice A");
    CHECK(g_order[3] == 'B', "coop: second slice B");

    mbjs_routine_free(rs[0]);
    mbjs_routine_free(rs[1]);
}

int main(void)
{
    test_single();
    test_yield_resume();
    test_zero_pause();
    test_coop();

    if (g_failures == 0) {
        printf("== ALL ROUTINE TESTS PASSED ==\n");
        return 0;
    }
    printf("== %d ROUTINE TESTS FAILED ==\n", g_failures);
    return 1;
}