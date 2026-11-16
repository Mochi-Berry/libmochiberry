/*
 * ECMA Test 262 Runner for mbjs
 *
 * Copyright (c) 2017-2021 Fabrice Bellard
 * Copyright (c) 2017-2021 Charlie Gordon
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
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

#include "cutils.h"
#include "list.h"
#include "mbjs.h"
#include "mbjs-c-atomics.h"
#include "mbjs-libc.h"

#define CMD_NAME "run-test262"

// not quite correct because in theory someone could compile mbjs.c
// with a different compiler but in practice no one does that, right?
#ifdef __TINYC__
#define CC_IS_TCC 1
#else
#define CC_IS_TCC 0
#endif

typedef struct {
    mbjs_mutex_t agent_mutex;
    mbjs_cond_t agent_cond;
    /* list of Test262Agent.link */
    struct list_head agent_list;
    mbjs_mutex_t report_mutex;
    /* list of AgentReport.link */
    struct list_head report_list;
    int async_done;
} ThreadLocalStorage;

typedef struct namelist_t {
    char **array;
    int count;
    int size;
} namelist_t;

long nthreads; // invariant: 0 < nthreads < countof(threads)
mbjs_thread_t threads[32];
mbjs_thread_t progress_thread;
mbjs_cond_t progress_cond;
mbjs_mutex_t progress_mutex;

namelist_t test_list;
namelist_t exclude_list;
namelist_t exclude_dir_list;

FILE *outfile;
enum test_mode_t {
    TEST_DEFAULT_NOSTRICT, /* run tests as nostrict unless test is flagged as strictonly */
    TEST_DEFAULT_STRICT,   /* run tests as strict unless test is flagged as nostrict */
    TEST_NOSTRICT,         /* run tests as nostrict, skip strictonly tests */
    TEST_STRICT,           /* run tests as strict, skip nostrict tests */
    TEST_ALL,              /* run tests in both strict and nostrict, unless restricted by spec */
} test_mode = TEST_DEFAULT_NOSTRICT;
int local;
int skip_async;
int skip_module;
int dump_memory;
int stats_count;
MBJSMemoryUsage stats_all, stats_avg, stats_min, stats_max;
char *stats_min_filename;
char *stats_max_filename;
mbjs_mutex_t stats_mutex;
int verbose;
char *harness_dir;
char *harness_exclude;
char *harness_features;
char *harness_skip_features;
char *prelude_file;
uint8_t *prelude_bin;
size_t prelude_bin_len;
char *error_filename;
char *error_file;
FILE *error_out;
int update_errors;
int slow_test_threshold;
int start_index, stop_index;
int test_excluded;
_Atomic int test_count, test_failed, test_skipped;
_Atomic int new_errors, changed_errors, fixed_errors;

void warning(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
void fatal(int, const char *, ...) __attribute__((__format__(__printf__, 2, 3)));

void atomic_inc(volatile _Atomic int *p)
{
    atomic_fetch_add(p, 1);
}

void warning(const char *fmt, ...)
{
    va_list ap;

    fflush(stdout);
    fprintf(stderr, "%s: ", CMD_NAME);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void fatal(int errcode, const char *fmt, ...)
{
    va_list ap;

    fflush(stdout);
    fprintf(stderr, "%s: ", CMD_NAME);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);

    exit(errcode);
}

void perror_exit(int errcode, const char *s)
{
    fflush(stdout);
    fprintf(stderr, "%s: ", CMD_NAME);
    perror(s);
    exit(errcode);
}

char *strdup_len(const char *str, int len)
{
    char *p = malloc(len + 1);
    memcpy(p, str, len);
    p[len] = '\0';
    return p;
}

static inline int str_equal(const char *a, const char *b) {
    return !strcmp(a, b);
}

static inline int str_count(const char *a, const char *b) {
    int count = 0;
    while ((a = strstr(a, b))) {
        a += strlen(b);
        count++;
    }
    return count;
}

char *str_append(char **pp, const char *sep, const char *str) {
    char *res, *p;
    size_t len = 0;
    p = *pp;
    if (p) {
        len = strlen(p) + strlen(sep);
    }
    res = malloc(len + strlen(str) + 1);
    if (p) {
        strcpy(res, p);
        strcat(res, sep);
    }
    strcpy(res + len, str);
    free(p);
    return *pp = res;
}

char *str_strip(char *p)
{
    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1]))
        p[--len] = '\0';
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

int has_prefix(const char *str, const char *prefix)
{
    return !strncmp(str, prefix, strlen(prefix));
}

char *skip_prefix(const char *str, const char *prefix)
{
    int i;
    for (i = 0;; i++) {
        if (prefix[i] == '\0') {  /* skip the prefix */
            str += i;
            break;
        }
        if (str[i] != prefix[i])
            break;
    }
    return (char *)str;
}

char *get_basename(const char *filename)
{
    const char *p = strrchr(filename, '/');
    if (!p)
        return NULL;
    return strdup_len(filename, p - filename);
}

char *compose_path(const char *path, const char *name)
{
    int path_len, name_len;
    char *d, *q;

    if (!path || path[0] == '\0' || *name == '/') {
        d = strdup(name);
    } else {
        path_len = strlen(path);
        name_len = strlen(name);
        d = malloc(path_len + 1 + name_len + 1);
        if (d) {
            q = d;
            memcpy(q, path, path_len);
            q += path_len;
            if (path[path_len - 1] != '/')
                *q++ = '/';
            memcpy(q, name, name_len + 1);
        }
    }
    return d;
}

int namelist_cmp(const char *a, const char *b)
{
    /* compare strings in modified lexicographical order */
    for (;;) {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if (isdigit(ca) && isdigit(cb)) {
            int na = ca - '0';
            int nb = cb - '0';
            while (isdigit(ca = (unsigned char)*a++))
                na = na * 10 + ca - '0';
            while (isdigit(cb = (unsigned char)*b++))
                nb = nb * 10 + cb - '0';
            if (na < nb)
                return -1;
            if (na > nb)
                return +1;
        }
        if (ca < cb)
            return -1;
        if (ca > cb)
            return +1;
        if (ca == '\0')
            return 0;
    }
}

int namelist_cmp_indirect(const void *a, const void *b)
{
    return namelist_cmp(*(const char **)a, *(const char **)b);
}

void namelist_sort(namelist_t *lp)
{
    int i, count;
    if (lp->count > 1) {
        qsort(lp->array, lp->count, sizeof(*lp->array), namelist_cmp_indirect);
        /* remove duplicates */
        for (count = i = 1; i < lp->count; i++) {
            if (namelist_cmp(lp->array[count - 1], lp->array[i]) == 0) {
                free(lp->array[i]);
            } else {
                lp->array[count++] = lp->array[i];
            }
        }
        lp->count = count;
    }
}

int namelist_find(const namelist_t *lp, const char *name)
{
    int a, b, m, cmp;

    for (a = 0, b = lp->count; a < b;) {
        m = a + (b - a) / 2;
        cmp = namelist_cmp(lp->array[m], name);
        if (cmp < 0)
            a = m + 1;
        else if (cmp > 0)
            b = m;
        else
            return m;
    }
    return -1;
}

void namelist_add(namelist_t *lp, const char *base, const char *name)
{
    char *s;

    s = compose_path(base, name);
    if (!s)
        goto fail;
    if (lp->count == lp->size) {
        size_t newsize = lp->size + (lp->size >> 1) + 4;
        char **a = realloc(lp->array, sizeof(lp->array[0]) * newsize);
        if (!a)
            goto fail;
        lp->array = a;
        lp->size = newsize;
    }
    lp->array[lp->count] = s;
    lp->count++;
    return;
fail:
    fatal(1, "allocation failure\n");
}

void namelist_load(namelist_t *lp, const char *filename)
{
    char buf[1024];
    char *base_name;
    FILE *f;

    f = fopen(filename, "r");
    if (!f) {
        perror_exit(1, filename);
    }
    base_name = get_basename(filename);

    while (fgets(buf, sizeof(buf), f) != NULL) {
        char *p = str_strip(buf);
        if (*p == '#' || *p == ';' || *p == '\0')
            continue;  /* line comment */

        namelist_add(lp, base_name, p);
    }
    free(base_name);
    fclose(f);
}

void namelist_add_from_error_file(namelist_t *lp, const char *file)
{
    const char *p, *p0;
    char *pp;

    for (p = file; (p = strstr(p, ".js:")) != NULL; p++) {
        for (p0 = p; p0 > file && p0[-1] != '\n'; p0--)
            continue;
        pp = strdup_len(p0, p + 3 - p0);
        namelist_add(lp, NULL, pp);
        free(pp);
    }
}

void namelist_free(namelist_t *lp)
{
    while (lp->count > 0) {
        free(lp->array[--lp->count]);
    }
    free(lp->array);
    lp->array = NULL;
    lp->size = 0;
}

static int add_test_file(const char *filename)
{
    namelist_t *lp = &test_list;
    if (mbjs__has_suffix(filename, ".js") && !mbjs__has_suffix(filename, "_FIXTURE.js"))
        namelist_add(lp, NULL, filename);
    return 0;
}

static void find_test_files(const char *path);

static bool ispathsep(int c)
{
    return c == '/' || c == '\\';
}

static void consider_test_file(const char *path, const char *name, int is_dir)
{
    size_t pathlen;
    char s[1024];

    if (str_equal(name, ".") || str_equal(name, ".."))
        return;
    pathlen = strlen(path);
    while (pathlen > 0 && ispathsep(path[pathlen-1]))
        pathlen--;
    snprintf(s, sizeof(s), "%.*s/%s", (int)pathlen, path, name);
#if !defined(_WIN32) && !defined(DT_DIR)
    struct stat st;
    if (is_dir < 0)
        is_dir = !stat(s, &st) && S_ISDIR(st.st_mode);
#endif
    if (is_dir)
        find_test_files(s);
    else
        add_test_file(s);
}

static void find_test_files(const char *path)
{
#ifdef _WIN32
    WIN32_FIND_DATAA d;
    HANDLE h;
    char s[1024];

    snprintf(s, sizeof(s), "%s/*", path);
    h = FindFirstFileA(s, &d);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            consider_test_file(path,
                               d.cFileName,
                               d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        } while (FindNextFileA(h, &d));
        FindClose(h);
    }
#else
    struct dirent *d, **ds = NULL;
    int i, n;

    n = scandir(path, &ds, NULL, alphasort);
    for (i = 0; i < n; i++) {
        d = ds[i];
#ifdef DT_DIR
        consider_test_file(path, d->d_name, d->d_type == DT_DIR);
#else
        consider_test_file(path, d->d_name, -1);
#endif
        free(d);
    }
    free(ds);
#endif
}

/* find js files from the directory tree and sort the list */
static void enumerate_tests(const char *path)
{
    namelist_t *lp = &test_list;
    int start = lp->count;
    find_test_files(path);
    qsort(lp->array + start, lp->count - start, sizeof(*lp->array),
          namelist_cmp_indirect);
}

static MBJSValue mbjs_print_262(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    ThreadLocalStorage *tls = MBJS_GetRuntimeOpaque(MBJS_GetRuntime(ctx));
    const char *s;
    MBJSValueConst v;
    int i;

    for (i = 0; i < argc; i++) {
        v = argv[i];
        s = MBJS_ToCString(ctx, v);
        // same logic as mbjs_print in mbjs-libc.c
        if (local && !s && MBJS_IsObject(v)) {
            MBJS_FreeValue(ctx, MBJS_GetException(ctx));
            MBJSValue t = MBJS_ToObjectString(ctx, v);
            s = MBJS_ToCString(ctx, t);
            MBJS_FreeValue(ctx, t);
        }
        if (!s)
            return MBJS_EXCEPTION;
        if (!strcmp(s, "Test262:AsyncTestComplete")) {
            tls->async_done++;
        } else if (mbjs__strstart(s, "Test262:AsyncTestFailure", NULL)) {
            tls->async_done = 2; /* force an error */
        }
        if (outfile) {
            if (i != 0)
                fputc(' ', outfile);
            fputs(s, outfile);
        }
        if (verbose > 1)
            printf("%s%s", &" "[i < 1], s);
        MBJS_FreeCString(ctx, s);
        if (verbose > 2 && MBJS_IsError(v)) {
            MBJSValue stack = MBJS_GetPropertyStr(ctx, v, "stack");
            s = MBJS_ToCString(ctx, stack);
            MBJS_FreeValue(ctx, stack);
            if (s) {
                printf("\n%s", s);
                MBJS_FreeCString(ctx, s);
            }
        }
    }
    if (outfile)
        fputc('\n', outfile);
    if (verbose > 1)
        printf("\n");
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_detachArrayBuffer(MBJSContext *ctx, MBJSValueConst this_val,
                                    int argc, MBJSValueConst *argv)
{
    MBJS_DetachArrayBuffer(ctx, argv[0]);
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_evalScript_262(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    const char *str;
    size_t len;
    MBJSValue ret;
    str = MBJS_ToCStringLen(ctx, &len, argv[0]);
    if (!str)
        return MBJS_EXCEPTION;
    ret = MBJS_Eval(ctx, str, len, "<evalScript>", MBJS_EVAL_TYPE_GLOBAL);
    MBJS_FreeCString(ctx, str);
    return ret;
}

static long cpu_count(void)
{
#ifdef _WIN32
    DWORD_PTR procmask, sysmask;
    long count;
    int i;

    count = 0;
    if (GetProcessAffinityMask(GetCurrentProcess(), &procmask, &sysmask))
        for (i = 0; i < 8 * sizeof(procmask); i++)
            count += 1 & (procmask >> i);
    return count;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

static void init_thread_local_storage(ThreadLocalStorage *p)
{
    mbjs_mutex_init(&p->agent_mutex);
    mbjs_cond_init(&p->agent_cond);
    init_list_head(&p->agent_list);
    mbjs_mutex_init(&p->report_mutex);
    init_list_head(&p->report_list);
    p->async_done = 0;
}

typedef struct {
    struct list_head link;
    mbjs_thread_t tid;
    char *script;
    MBJSValue broadcast_func;
    bool broadcast_pending;
    MBJSValue broadcast_sab; /* in the main context */
    uint8_t *broadcast_sab_buf;
    size_t broadcast_sab_size;
    int32_t broadcast_val;
    ThreadLocalStorage *tls;
} Test262Agent;

typedef struct {
    struct list_head link;
    char *str;
} AgentReport;

static MBJSValue add_helpers1(MBJSContext *ctx);
static void add_helpers(MBJSContext *ctx);

static void agent_start(void *arg)
{
    ThreadLocalStorage *tls;
    Test262Agent *agent;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue ret_val;
    int ret;

    agent = arg;
    tls = agent->tls; // shares thread-local storage with parent thread
    rt = MBJS_NewRuntime();
    if (rt == NULL) {
        fatal(1, "MBJS_NewRuntime failure");
    }
    MBJS_SetRuntimeOpaque(rt, tls);
    ctx = MBJS_NewContext(rt);
    if (ctx == NULL) {
        MBJS_FreeRuntime(rt);
        fatal(1, "MBJS_NewContext failure");
    }
    MBJS_SetContextOpaque(ctx, agent);
    MBJS_SetRuntimeInfo(rt, "agent");
    MBJS_SetCanBlock(rt, true);

    add_helpers(ctx);
    ret_val = MBJS_Eval(ctx, agent->script, strlen(agent->script),
                      "<evalScript>", MBJS_EVAL_TYPE_GLOBAL);
    free(agent->script);
    agent->script = NULL;
    if (MBJS_IsException(ret_val))
        mbjs_std_dump_error(ctx);
    MBJS_FreeValue(ctx, ret_val);

    for(;;) {
        MBJSContext *ctx1;
        ret = MBJS_ExecutePendingJob(MBJS_GetRuntime(ctx), &ctx1);
        if (ret < 0) {
            mbjs_std_dump_error(ctx);
            break;
        } else if (ret == 0) {
            if (MBJS_IsUndefined(agent->broadcast_func)) {
                break;
            } else {
                MBJSValue args[2];

                mbjs_mutex_lock(&tls->agent_mutex);
                while (!agent->broadcast_pending) {
                    mbjs_cond_wait(&tls->agent_cond, &tls->agent_mutex);
                }

                agent->broadcast_pending = false;
                mbjs_cond_signal(&tls->agent_cond);

                mbjs_mutex_unlock(&tls->agent_mutex);

                args[0] = MBJS_NewArrayBuffer(ctx, agent->broadcast_sab_buf,
                                            agent->broadcast_sab_size,
                                            /*max_len*/0, NULL, NULL, true);
                args[1] = MBJS_NewInt32(ctx, agent->broadcast_val);
                ret_val = MBJS_Call(ctx, agent->broadcast_func, MBJS_UNDEFINED,
                                  2, (MBJSValueConst *)args);
                MBJS_FreeValue(ctx, args[0]);
                MBJS_FreeValue(ctx, args[1]);
                if (MBJS_IsException(ret_val))
                    mbjs_std_dump_error(ctx);
                MBJS_FreeValue(ctx, ret_val);
                MBJS_FreeValue(ctx, agent->broadcast_func);
                agent->broadcast_func = MBJS_UNDEFINED;
            }
        }
    }
    MBJS_FreeValue(ctx, agent->broadcast_func);

    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static MBJSValue mbjs_agent_start(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    ThreadLocalStorage *tls = MBJS_GetRuntimeOpaque(MBJS_GetRuntime(ctx));
    const char *script;
    Test262Agent *agent;

    if (MBJS_GetContextOpaque(ctx) != NULL)
        return MBJS_ThrowTypeError(ctx, "cannot be called inside an agent");

    script = MBJS_ToCString(ctx, argv[0]);
    if (!script)
        return MBJS_EXCEPTION;
    agent = malloc(sizeof(*agent));
    memset(agent, 0, sizeof(*agent));
    agent->broadcast_func = MBJS_UNDEFINED;
    agent->broadcast_sab = MBJS_UNDEFINED;
    agent->script = strdup(script);
    agent->tls = tls;
    MBJS_FreeCString(ctx, script);
    list_add_tail(&agent->link, &tls->agent_list);
    mbjs_thread_create(&agent->tid, agent_start, agent, /*flags*/0);
    return MBJS_UNDEFINED;
}

static void mbjs_agent_free(MBJSContext *ctx)
{
    ThreadLocalStorage *tls = MBJS_GetRuntimeOpaque(MBJS_GetRuntime(ctx));
    struct list_head *el, *el1;
    Test262Agent *agent;

    list_for_each_safe(el, el1, &tls->agent_list) {
        agent = list_entry(el, Test262Agent, link);
        mbjs_thread_join(agent->tid);
        MBJS_FreeValue(ctx, agent->broadcast_sab);
        list_del(&agent->link);
        free(agent);
    }
}

static MBJSValue mbjs_agent_leaving(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv)
{
    Test262Agent *agent = MBJS_GetContextOpaque(ctx);
    if (!agent)
        return MBJS_ThrowTypeError(ctx, "must be called inside an agent");
    /* nothing to do */
    return MBJS_UNDEFINED;
}

static bool is_broadcast_pending(ThreadLocalStorage *tls)
{
    struct list_head *el;
    Test262Agent *agent;
    list_for_each(el, &tls->agent_list) {
        agent = list_entry(el, Test262Agent, link);
        if (agent->broadcast_pending)
            return true;
    }
    return false;
}

static MBJSValue mbjs_agent_broadcast(MBJSContext *ctx, MBJSValueConst this_val,
                                  int argc, MBJSValueConst *argv)
{
    ThreadLocalStorage *tls = MBJS_GetRuntimeOpaque(MBJS_GetRuntime(ctx));
    MBJSValueConst sab = argv[0];
    struct list_head *el;
    Test262Agent *agent;
    uint8_t *buf;
    size_t buf_size;
    int32_t val;

    if (MBJS_GetContextOpaque(ctx) != NULL)
        return MBJS_ThrowTypeError(ctx, "cannot be called inside an agent");

    buf = MBJS_GetArrayBuffer(ctx, &buf_size, sab);
    if (!buf)
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &val, argv[1]))
        return MBJS_EXCEPTION;

    /* broadcast the values and wait until all agents have started
       calling their callbacks */
    mbjs_mutex_lock(&tls->agent_mutex);
    list_for_each(el, &tls->agent_list) {
        agent = list_entry(el, Test262Agent, link);
        agent->broadcast_pending = true;
        /* the shared array buffer is used by the thread, so increment
           its refcount */
        agent->broadcast_sab = MBJS_DupValue(ctx, sab);
        agent->broadcast_sab_buf = buf;
        agent->broadcast_sab_size = buf_size;
        agent->broadcast_val = val;
    }
    mbjs_cond_broadcast(&tls->agent_cond);

    while (is_broadcast_pending(tls)) {
        mbjs_cond_wait(&tls->agent_cond, &tls->agent_mutex);
    }
    mbjs_mutex_unlock(&tls->agent_mutex);
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_agent_receiveBroadcast(MBJSContext *ctx, MBJSValueConst this_val,
                                         int argc, MBJSValueConst *argv)
{
    Test262Agent *agent = MBJS_GetContextOpaque(ctx);
    if (!agent)
        return MBJS_ThrowTypeError(ctx, "must be called inside an agent");
    if (!MBJS_IsFunction(ctx, argv[0]))
        return MBJS_ThrowTypeError(ctx, "expecting function");
    MBJS_FreeValue(ctx, agent->broadcast_func);
    agent->broadcast_func = MBJS_DupValue(ctx, argv[0]);
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_agent_sleep(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    uint32_t duration;
    if (MBJS_ToUint32(ctx, &duration, argv[0]))
        return MBJS_EXCEPTION;
#ifdef _WIN32
    Sleep(duration);
#else
    usleep(duration * 1000);
#endif
    return MBJS_UNDEFINED;
}

static int64_t get_clock_ms(void)
{
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
#endif
}

static MBJSValue mbjs_agent_monotonicNow(MBJSContext *ctx, MBJSValueConst this_val,
                                     int argc, MBJSValueConst *argv)
{
    return MBJS_NewInt64(ctx, get_clock_ms());
}

static MBJSValue mbjs_agent_getReport(MBJSContext *ctx, MBJSValueConst this_val,
                                  int argc, MBJSValueConst *argv)
{
    ThreadLocalStorage *tls = MBJS_GetRuntimeOpaque(MBJS_GetRuntime(ctx));
    AgentReport *rep;
    MBJSValue ret;

    mbjs_mutex_lock(&tls->report_mutex);
    if (list_empty(&tls->report_list)) {
        rep = NULL;
    } else {
        rep = list_entry(tls->report_list.next, AgentReport, link);
        list_del(&rep->link);
    }
    mbjs_mutex_unlock(&tls->report_mutex);
    if (rep) {
        ret = MBJS_NewString(ctx, rep->str);
        free(rep->str);
        free(rep);
    } else {
        ret = MBJS_NULL;
    }
    return ret;
}

static MBJSValue mbjs_agent_report(MBJSContext *ctx, MBJSValueConst this_val,
                               int argc, MBJSValueConst *argv)
{
    ThreadLocalStorage *tls = MBJS_GetRuntimeOpaque(MBJS_GetRuntime(ctx));
    const char *str;
    AgentReport *rep;

    str = MBJS_ToCString(ctx, argv[0]);
    if (!str)
        return MBJS_EXCEPTION;
    rep = malloc(sizeof(*rep));
    rep->str = strdup(str);
    MBJS_FreeCString(ctx, str);

    mbjs_mutex_lock(&tls->report_mutex);
    list_add_tail(&rep->link, &tls->report_list);
    mbjs_mutex_unlock(&tls->report_mutex);
    return MBJS_UNDEFINED;
}

static const MBJSCFunctionListEntry mbjs_agent_funcs[] = {
    /* only in main */
    MBJS_CFUNC_DEF("start", 1, mbjs_agent_start ),
    MBJS_CFUNC_DEF("getReport", 0, mbjs_agent_getReport ),
    MBJS_CFUNC_DEF("broadcast", 2, mbjs_agent_broadcast ),
    /* only in agent */
    MBJS_CFUNC_DEF("report", 1, mbjs_agent_report ),
    MBJS_CFUNC_DEF("leaving", 0, mbjs_agent_leaving ),
    MBJS_CFUNC_DEF("receiveBroadcast", 1, mbjs_agent_receiveBroadcast ),
    /* in both */
    MBJS_CFUNC_DEF("sleep", 1, mbjs_agent_sleep ),
    MBJS_CFUNC_DEF("monotonicNow", 0, mbjs_agent_monotonicNow ),
};

static MBJSValue mbjs_new_agent(MBJSContext *ctx)
{
    MBJSValue agent;
    agent = MBJS_NewObject(ctx);
    MBJS_SetPropertyFunctionList(ctx, agent, mbjs_agent_funcs,
                               countof(mbjs_agent_funcs));
    return agent;
}

static MBJSValue mbjs_createRealm(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    MBJSContext *ctx1;
    MBJSValue ret;

    ctx1 = MBJS_NewContext(MBJS_GetRuntime(ctx));
    if (!ctx1)
        return MBJS_ThrowOutOfMemory(ctx);
    ret = add_helpers1(ctx1);
    /* ctx1 has a refcount so it stays alive */
    MBJS_FreeContext(ctx1);
    return ret;
}

static MBJSValue mbjs_IsHTMLDDA(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    return MBJS_NULL;
}

static MBJSValue mbjs_gc(MBJSContext *ctx, MBJSValueConst this_val,
                     int argc, MBJSValueConst *argv)
{
    MBJS_RunGC(MBJS_GetRuntime(ctx));
    return MBJS_UNDEFINED;
}

static MBJSValue add_helpers1(MBJSContext *ctx)
{
    MBJSValue global_obj, obj262, is_html_dda;

    global_obj = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global_obj, "print",
                      MBJS_NewCFunction(ctx, mbjs_print_262, "print", 1));
    MBJS_SetPropertyStr(ctx, global_obj, "gc",
                      MBJS_NewCFunction(ctx, mbjs_gc, "gc", 0));
    static const MBJSCFunctionListEntry props[] = {
        MBJS_CFUNC_DEF("codePointRange", 2, mbjs_string_codePointRange),
        MBJS_CFUNC_DEF("createRealm", 0, mbjs_createRealm),
        MBJS_CFUNC_DEF("detachArrayBuffer", 1, mbjs_detachArrayBuffer),
        MBJS_CFUNC_DEF("evalScript", 1, mbjs_evalScript_262),
        MBJS_CFUNC_DEF("gc", 0, mbjs_gc),
    };
    is_html_dda = MBJS_NewCFunction(ctx, mbjs_IsHTMLDDA, "IsHTMLDDA", 0);
    MBJS_SetIsHTMLDDA(ctx, is_html_dda);
    obj262 = MBJS_NewObject(ctx);
    MBJS_SetPropertyFunctionList(ctx, obj262, props, countof(props));
    MBJS_SetPropertyStr(ctx, obj262, "IsHTMLDDA", is_html_dda);
    MBJS_SetPropertyStr(ctx, obj262, "agent", mbjs_new_agent(ctx));
    MBJS_SetPropertyStr(ctx, obj262, "global", MBJS_GetGlobalObject(ctx));
    /* $262 special object used by the tests */
    MBJS_SetPropertyStr(ctx, global_obj, "$262", MBJS_DupValue(ctx, obj262));
    MBJS_FreeValue(ctx, global_obj);
    return obj262;
}

static void add_helpers(MBJSContext *ctx)
{
    MBJS_FreeValue(ctx, add_helpers1(ctx));
}

static char *load_file(const char *filename, size_t *lenp)
{
    char *buf;
    size_t buf_len;
    buf = (char *)mbjs_load_file(NULL, &buf_len, filename);
    if (!buf)
        perror_exit(1, filename);
    if (lenp)
        *lenp = buf_len;
    return buf;
}

static MBJSModuleDef *mbjs_module_loader_test(MBJSContext *ctx,
                                          const char *module_name, void *opaque,
                                          MBJSValueConst attributes)
{
    char *filename, *slash, path[1024];

    // interpret import("bar.js") from path/to/foo.js as
    // import("path/to/bar.js") but leave import("./bar.js") untouched
    filename = opaque;
    if (!strchr(module_name, '/')) {
        slash = strrchr(filename, '/');
        if (slash) {
            snprintf(path, sizeof(path), "%.*s/%s",
                     (int) (slash - filename), filename, module_name);
            module_name = path;
        }
    }
    return mbjs_module_load(ctx, module_name, opaque, attributes, mbjs_load_file);
}

int is_line_sep(char c)
{
    return (c == '\0' || c == '\n' || c == '\r');
}

char *find_line(const char *str, const char *line)
{
    if (str) {
        const char *p;
        int len = strlen(line);
        for (p = str; (p = strstr(p, line)) != NULL; p += len + 1) {
            if ((p == str || is_line_sep(p[-1])) && is_line_sep(p[len]))
                return (char *)p;
        }
    }
    return NULL;
}

int is_word_sep(char c)
{
    return (c == '\0' || isspace((unsigned char)c) || c == ',');
}

char *find_word(const char *str, const char *word)
{
    const char *p;
    int len = strlen(word);
    if (str && len) {
        for (p = str; (p = strstr(p, word)) != NULL; p += len) {
            if ((p == str || is_word_sep(p[-1])) && is_word_sep(p[len]))
                return (char *)p;
        }
    }
    return NULL;
}

/* handle exclude directories */
void update_exclude_dirs(void)
{
    namelist_t *lp = &test_list;
    namelist_t *ep = &exclude_list;
    namelist_t *dp = &exclude_dir_list;
    char *name, *path;
    int i, j, count;
    size_t include, exclude;

    /* split directpries from exclude_list */
    for (count = i = 0; i < ep->count; i++) {
        name = ep->array[i];
        if (mbjs__has_suffix(name, "/")) {
            namelist_add(dp, NULL, name);
            free(name);
        } else {
            ep->array[count++] = name;
        }
    }
    ep->count = count;

    namelist_sort(dp);

    /* filter out excluded directories */
    for (count = i = 0; i < lp->count; i++) {
        name = lp->array[i];
        include = exclude = 0;
        for (j = 0; j < dp->count; j++) {
            path = dp->array[j];
            if (has_prefix(name, path))
                exclude = strlen(path);
            if (*path == '!' && has_prefix(name, &path[1]))
                include = strlen(&path[1]);
        }
        // most specific include/exclude wins
        if (exclude > include) {
            test_excluded++;
            free(name);
        } else {
            lp->array[count++] = name;
        }
    }
    lp->count = count;
}

void load_config(const char *filename, const char *ignore)
{
    char buf[1024];
    FILE *f;
    char *base_name;
    enum {
        SECTION_NONE = 0,
        SECTION_CONFIG,
        SECTION_EXCLUDE,
        SECTION_FEATURES,
        SECTION_TESTS,
    } section = SECTION_NONE;
    int lineno = 0;

    f = fopen(filename, "r");
    if (!f) {
        perror_exit(1, filename);
    }
    base_name = get_basename(filename);

    while (fgets(buf, sizeof(buf), f) != NULL) {
        char *p, *q;
        lineno++;
        p = str_strip(buf);
        if (*p == '#' || *p == ';' || *p == '\0')
            continue;  /* line comment */

        if (*p == "[]"[0]) {
            /* new section */
            p++;
            p[strcspn(p, "]")] = '\0';
            if (str_equal(p, "config"))
                section = SECTION_CONFIG;
            else if (str_equal(p, "exclude"))
                section = SECTION_EXCLUDE;
            else if (str_equal(p, "features"))
                section = SECTION_FEATURES;
            else if (str_equal(p, "tests"))
                section = SECTION_TESTS;
            else
                section = SECTION_NONE;
            continue;
        }
        q = strchr(p, '=');
        if (q) {
            /* setting: name=value */
            *q++ = '\0';
            q = str_strip(q);
        }
        switch (section) {
        case SECTION_CONFIG:
            if (!q) {
                printf("%s:%d: syntax error\n", filename, lineno);
                continue;
            }
            if (strstr(ignore, p)) {
                printf("%s:%d: ignoring %s=%s\n", filename, lineno, p, q);
                continue;
            }
            if (str_equal(p, "local")) {
                local = str_equal(q, "yes");
                continue;
            }
            if (str_equal(p, "testdir")) {
                char *testdir = compose_path(base_name, q);
                enumerate_tests(testdir);
                free(testdir);
                continue;
            }
            if (str_equal(p, "harnessdir")) {
                harness_dir = compose_path(base_name, q);
                continue;
            }
            if (str_equal(p, "harnessexclude")) {
                str_append(&harness_exclude, " ", q);
                continue;
            }
            if (str_equal(p, "features")) {
                str_append(&harness_features, " ", q);
                continue;
            }
            if (str_equal(p, "skip-features")) {
                str_append(&harness_skip_features, " ", q);
                continue;
            }
            if (str_equal(p, "mode")) {
                if (str_equal(q, "default") || str_equal(q, "default-nostrict"))
                    test_mode = TEST_DEFAULT_NOSTRICT;
                else if (str_equal(q, "default-strict"))
                    test_mode = TEST_DEFAULT_STRICT;
                else if (str_equal(q, "nostrict"))
                    test_mode = TEST_NOSTRICT;
                else if (str_equal(q, "strict"))
                    test_mode = TEST_STRICT;
                else if (str_equal(q, "all") || str_equal(q, "both"))
                    test_mode = TEST_ALL;
                else
                    fatal(2, "unknown test mode: %s", q);
                continue;
            }
            if (str_equal(p, "strict")) {
                if (str_equal(q, "skip") || str_equal(q, "no"))
                    test_mode = TEST_NOSTRICT;
                continue;
            }
            if (str_equal(p, "nostrict")) {
                if (str_equal(q, "skip") || str_equal(q, "no"))
                    test_mode = TEST_STRICT;
                continue;
            }
            if (str_equal(p, "async")) {
                skip_async = !str_equal(q, "yes");
                continue;
            }
            if (str_equal(p, "module")) {
                skip_module = !str_equal(q, "yes");
                continue;
            }
            if (str_equal(p, "verbose")) {
                int count = str_count(q, "yes");
                verbose = max_int(verbose, count);
                continue;
            }
            if (str_equal(p, "errorfile")) {
                error_filename = compose_path(base_name, q);
                continue;
            }
            if (str_equal(p, "excludefile")) {
                char *path = compose_path(base_name, q);
                namelist_load(&exclude_list, path);
                free(path);
                continue;
            }
        case SECTION_EXCLUDE:
            namelist_add(&exclude_list, base_name, p);
            break;
        case SECTION_FEATURES:
            if (!q || str_equal(q, "yes") || (!CC_IS_TCC && str_equal(q, "!tcc")))
                str_append(&harness_features, " ", p);
            else
                str_append(&harness_skip_features, " ", p);
            break;
        case SECTION_TESTS:
            namelist_add(&test_list, base_name, p);
            break;
        default:
            /* ignore settings in other sections */
            break;
        }
    }
    fclose(f);
    free(base_name);
}

char *find_error(const char *filename, int *pline, int is_strict)
{
    if (error_file) {
        size_t len = strlen(filename);
        const char *p, *q, *r;
        int line;

        for (p = error_file; (p = strstr(p, filename)) != NULL; p += len) {
            if ((p == error_file || p[-1] == '\n' || p[-1] == '(') && p[len] == ':') {
                q = p + len;
                line = 1;
                if (*q == ':') {
                    line = strtol(q + 1, (char**)&q, 10);
                    if (*q == ':')
                        q++;
                }
                while (*q == ' ') {
                    q++;
                }
                /* check strict mode indicator */
                if (!mbjs__strstart(q, "strict mode: ", &q) != !is_strict)
                    continue;
                r = q = skip_prefix(q, "unexpected error: ");
                r += strcspn(r, "\n");
                while (r[0] == '\n' && r[1] && strncmp(r + 1, filename, 8)) {
                    r++;
                    r += strcspn(r, "\n");
                }
                if (pline)
                    *pline = line;
                return strdup_len(q, r - q);
            }
        }
    }
    return NULL;
}

int skip_comments(const char *str, int line, int *pline)
{
    const char *p;
    int c;

    p = str;
    while ((c = (unsigned char)*p++) != '\0') {
        if (isspace(c)) {
            if (c == '\n')
                line++;
            continue;
        }
        if (c == '/' && *p == '/') {
            while (*++p && *p != '\n')
                continue;
            continue;
        }
        if (c == '/' && *p == '*') {
            for (p += 1; *p; p++) {
                if (*p == '\n') {
                    line++;
                    continue;
                }
                if (*p == '*' && p[1] == '/') {
                    p += 2;
                    break;
                }
            }
            continue;
        }
        break;
    }
    if (pline)
        *pline = line;

    return p - str;
}

int longest_match(const char *str, const char *find, int pos, int *ppos, int line, int *pline)
{
    int len, maxlen;

    maxlen = 0;

    if (*find) {
        const char *p;
        for (p = str + pos; *p; p++) {
            if (*p == *find) {
                for (len = 1; p[len] && p[len] == find[len]; len++)
                    continue;
                if (len > maxlen) {
                    maxlen = len;
                    if (ppos)
                        *ppos = p - str;
                    if (pline)
                        *pline = line;
                    if (!find[len])
                        break;
                }
            }
            if (*p == '\n')
                line++;
        }
    }
    return maxlen;
}

static int eval_buf(MBJSContext *ctx, const char *buf, size_t buf_len,
                    const char *filename, int is_test, int is_negative,
                    const char *error_type, int eval_flags, int is_async,
                    int *msec)
{
    ThreadLocalStorage *tls = MBJS_GetRuntimeOpaque(MBJS_GetRuntime(ctx));
    MBJSValue res_val, exception_val;
    int ret, error_line, pos, pos_line;
    bool is_error, has_error_line, ret_promise;
    const char *error_name;
    int start, duration;

    pos = skip_comments(buf, 1, &pos_line);
    error_line = pos_line;
    has_error_line = false;
    exception_val = MBJS_UNDEFINED;
    error_name = NULL;

    /* a module evaluation returns a promise */
    ret_promise = ((eval_flags & MBJS_EVAL_TYPE_MODULE) != 0);
    tls->async_done = 0; /* counter of "Test262:AsyncTestComplete" messages */

    start = get_clock_ms();
    res_val = MBJS_Eval(ctx, buf, buf_len, filename, eval_flags);

    if ((is_async || ret_promise) && !MBJS_IsException(res_val)) {
        MBJSValue promise = MBJS_UNDEFINED;
        if (ret_promise) {
            promise = res_val;
        } else {
            MBJS_FreeValue(ctx, res_val);
        }
        for(;;) {
            MBJSContext *ctx1;
            ret = MBJS_ExecutePendingJob(MBJS_GetRuntime(ctx), &ctx1);
            if (ret < 0) {
                res_val = MBJS_EXCEPTION;
                break;
            } else if (ret == 0) {
                if (is_async) {
                    /* test if the test called $DONE() once */
                    if (tls->async_done != 1) {
                        res_val = MBJS_ThrowTypeError(ctx, "$DONE() not called");
                    } else {
                        res_val = MBJS_UNDEFINED;
                    }
                } else {
                    /* check that the returned promise is fulfilled */
                    MBJSPromiseStateEnum state = MBJS_PromiseState(ctx, promise);
                    if (state == MBJS_PROMISE_FULFILLED)
                        res_val = MBJS_UNDEFINED;
                    else if (state == MBJS_PROMISE_REJECTED)
                        res_val = MBJS_Throw(ctx, MBJS_PromiseResult(ctx, promise));
                    else
                        res_val = MBJS_ThrowTypeError(ctx, "promise is pending");
                }
                break;
            }
        }
        MBJS_FreeValue(ctx, promise);
    }

    duration = get_clock_ms() - start;
    *msec += duration;

    if (MBJS_IsException(res_val)) {
        exception_val = MBJS_GetException(ctx);
        is_error = MBJS_IsError(exception_val);
        mbjs_print_262(ctx, MBJS_NULL, 1, (MBJSValueConst *)&exception_val);
        if (is_error) {
            MBJSValue name, stack;
            const char *stack_str;

            name = MBJS_GetPropertyStr(ctx, exception_val, "name");
            error_name = MBJS_ToCString(ctx, name);
            stack = MBJS_GetPropertyStr(ctx, exception_val, "stack");
            if (!MBJS_IsUndefined(stack)) {
                stack_str = MBJS_ToCString(ctx, stack);
                if (stack_str) {
                    const char *p;
                    int len;

                    len = strlen(filename);
                    p = strstr(stack_str, filename);
                    if (p != NULL && p[len] == ':') {
                        error_line = atoi(p + len + 1);
                        has_error_line = true;
                    }
                    MBJS_FreeCString(ctx, stack_str);
                }
            }
            MBJS_FreeValue(ctx, stack);
            MBJS_FreeValue(ctx, name);
        }
        if (is_negative) {
            ret = 0;
            if (error_type) {
                char *error_class;
                const char *msg;

                msg = MBJS_ToCString(ctx, exception_val);
                if (msg == NULL) {
                    ret = -1;
                } else {
                    error_class = strdup_len(msg, strcspn(msg, ":"));
                    if (!str_equal(error_class, error_type))
                        ret = -1;
                    free(error_class);
                    MBJS_FreeCString(ctx, msg);
                }
            }
        } else {
            ret = -1;
        }
    } else {
        if (is_negative)
            ret = -1;
        else
            ret = 0;
    }

    if (verbose && is_test) {
        MBJSValue msg_val = MBJS_UNDEFINED;
        const char *msg = NULL;
        int s_line;
        char *s = find_error(filename, &s_line, eval_flags & MBJS_EVAL_FLAG_STRICT);
        const char *strict_mode = (eval_flags & MBJS_EVAL_FLAG_STRICT) ? "strict mode: " : "";
        bool is_unexpected_error = true;

        if (!MBJS_IsUndefined(exception_val)) {
            msg_val = MBJS_ToString(ctx, exception_val);
            msg = MBJS_ToCString(ctx, msg_val);
        }
        if (is_negative) {  // expect error
            if (ret == 0) {
                if (msg && s &&
                    (str_equal(s, "expected error") ||
                     mbjs__strstart(s, "unexpected error type:", NULL) ||
                     str_equal(s, msg))) {     // did not have error yet
                    if (!has_error_line) {
                        longest_match(buf, msg, pos, &pos, pos_line, &error_line);
                    }
                    printf("%s:%d: %sOK, now has error %s\n",
                           filename, error_line, strict_mode, msg);
                    atomic_inc(&fixed_errors);
                    is_unexpected_error = false;
                }
            } else {
                if (!s) {   // not yet reported
                    if (msg) {
                        fprintf(error_out, "%s:%d: %sunexpected error type: %s\n",
                                filename, error_line, strict_mode, msg);
                    } else {
                        fprintf(error_out, "%s:%d: %sexpected error\n",
                                filename, error_line, strict_mode);
                    }
                    atomic_inc(&new_errors);
                }
            }
        } else {            // should not have error
            if (msg) {
                if (!s || !str_equal(s, msg)) {
                    if (!has_error_line) {
                        char *p = skip_prefix(msg, "Test262 Error: ");
                        if (strstr(p, "Test case returned non-true value!")) {
                            longest_match(buf, "runTestCase", pos, &pos, pos_line, &error_line);
                        } else {
                            longest_match(buf, p, pos, &pos, pos_line, &error_line);
                        }
                    }
                    fprintf(error_out, "%s:%d: %s%s%s\n", filename, error_line, strict_mode,
                            error_file ? "unexpected error: " : "", msg);

                    if (s && (!str_equal(s, msg) || error_line != s_line)) {
                        printf("%s:%d: %sprevious error: %s\n", filename, s_line, strict_mode, s);
                        atomic_inc(&changed_errors);
                    } else {
                        atomic_inc(&new_errors);
                    }
                }
            } else {
                if (s) {
                    printf("%s:%d: %sOK, fixed error: %s\n", filename, s_line, strict_mode, s);
                    atomic_inc(&fixed_errors);
                    is_unexpected_error = false;
                }
            }
        }
        if (is_unexpected_error && verbose > 1 && MBJS_IsException(exception_val)) {
            MBJSValue val = MBJS_GetPropertyStr(ctx, exception_val, "stack");
            if (!MBJS_IsException(val) &&
                !MBJS_IsUndefined(val) &&
                !MBJS_IsNull(val)) {
                const char *str = MBJS_ToCString(ctx, val);
                if (str)
                    printf("%s\n", str);
                MBJS_FreeCString(ctx, str);
                MBJS_FreeValue(ctx, val);
            }
        }
        MBJS_FreeValue(ctx, msg_val);
        MBJS_FreeCString(ctx, msg);
        free(s);
    }

    if (local) {
        ret = mbjs_std_loop(ctx);
        if (ret)
            mbjs_std_dump_error(ctx);
    }

    MBJS_FreeCString(ctx, error_name);
    MBJS_FreeValue(ctx, exception_val);
    MBJS_FreeValue(ctx, res_val);
    return ret;
}

static int eval_file(MBJSContext *ctx, const char *base, const char *p,
                     int eval_flags)
{
    char *buf;
    size_t buf_len;
    char *filename = compose_path(base, p);
    int msec = 0;

    buf = load_file(filename, &buf_len);
    if (!buf) {
        warning("cannot load %s", filename);
        goto fail;
    }
    if (eval_buf(ctx, buf, buf_len, filename, false, false, NULL,
                 eval_flags, false, &msec)) {
        warning("error evaluating %s", filename);
        goto fail;
    }
    free(buf);
    free(filename);
    return 0;

fail:
    free(buf);
    free(filename);
    return 1;
}

char *extract_desc(const char *buf)
{
    const char *p, *desc_start;
    char *desc;
    int len;

    p = buf;
    while (*p != '\0') {
        if (p[0] == '/' && p[1] == '*' && p[2] == '-' && p[3] != '/') {
            p += 3;
            desc_start = p;
            while (*p != '\0' && (p[0] != '*' || p[1] != '/'))
                p++;
            if (*p == '\0') {
                warning("Expecting end of desc comment");
                return NULL;
            }
            len = p - desc_start;
            desc = malloc(len + 1);
            memcpy(desc, desc_start, len);
            desc[len] = '\0';
            return desc;
        } else {
            p++;
        }
    }
    return NULL;
}

static char *find_tag(char *desc, const char *tag, int *state)
{
    char *p;
    p = strstr(desc, tag);
    if (p) {
        p += strlen(tag);
        *state = 0;
    }
    return p;
}

static char *get_option(char **pp, int *state)
{
    char *p, *p0, *option = NULL;
    if (*pp) {
        for (p = *pp;; p++) {
            switch (*p) {
            case '[':
                *state += 1;
                continue;
            case ']':
                *state -= 1;
                if (*state > 0)
                    continue;
                p = NULL;
                break;
            case ' ':
            case '\t':
            case '\r':
            case ',':
            case '-':
                continue;
            case '\n':
                if (*state > 0 || p[1] == ' ')
                    continue;
                p = NULL;
                break;
            case '\0':
                p = NULL;
                break;
            default:
                p0 = p;
                p += strcspn(p0, " \t\r\n,]");
                option = strdup_len(p0, p - p0);
                break;
            }
            break;
        }
        *pp = p;
    }
    return option;
}

void update_stats(MBJSRuntime *rt, const char *filename) {
    MBJSMemoryUsage stats;
    MBJS_ComputeMemoryUsage(rt, &stats);
    mbjs_mutex_lock(&stats_mutex);
    if (stats_count++ == 0) {
        stats_avg = stats_all = stats_min = stats_max = stats;
        free(stats_min_filename);
        stats_min_filename = strdup(filename);
        free(stats_max_filename);
        stats_max_filename = strdup(filename);
    } else {
        if (stats_max.malloc_size < stats.malloc_size) {
            stats_max = stats;
            free(stats_max_filename);
            stats_max_filename = strdup(filename);
        }
        if (stats_min.malloc_size > stats.malloc_size) {
            stats_min = stats;
            free(stats_min_filename);
            stats_min_filename = strdup(filename);
        }

#define update(f)  stats_avg.f = (stats_all.f += stats.f) / stats_count
        update(malloc_count);
        update(malloc_size);
        update(memory_used_count);
        update(memory_used_size);
        update(atom_count);
        update(atom_size);
        update(str_count);
        update(str_size);
        update(obj_count);
        update(obj_size);
        update(prop_count);
        update(prop_size);
        update(shape_count);
        update(shape_size);
        update(mbjs_func_count);
        update(mbjs_func_size);
        update(mbjs_func_code_size);
        update(mbjs_func_pc2line_count);
        update(mbjs_func_pc2line_size);
        update(c_func_count);
        update(array_count);
        update(fast_array_count);
        update(fast_array_elements);
    }
#undef update
    mbjs_mutex_unlock(&stats_mutex);
}

static MBJSValue mbjs_black_box(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst argv[], int magic)
{
    return MBJS_NewInt32(ctx, mbjs_std_cmd(magic, ctx, &argv[0]));
}

static const MBJSCFunctionListEntry mbjs_methods[] = {
    MBJS_CFUNC_MAGIC_DEF("getStringKind", 1, mbjs_black_box, /*GetStringKind*/3),
};

static const MBJSCFunctionListEntry mbjs_object =
    MBJS_OBJECT_DEF("mbjs", mbjs_methods, countof(mbjs_methods), MBJS_PROP_C_W_E);

MBJSContext *MBJS_NewCustomContext(MBJSRuntime *rt)
{
    MBJSContext *ctx;
    MBJSValue obj;

    ctx = MBJS_NewContext(rt);
    if (ctx && local) {
        mbjs_init_module_std(ctx, "mbjs:std");
        mbjs_init_module_os(ctx, "mbjs:os");
        mbjs_init_module_bjson(ctx, "mbjs:bjson");
        obj = MBJS_GetGlobalObject(ctx);
        MBJS_SetPropertyFunctionList(ctx, obj, &mbjs_object, 1);
        MBJS_FreeValue(ctx, obj);
    }
    return ctx;
}

/* Interrupt handler used by --timeout: count down the opcode budget. */
static int interrupt_handler(MBJSContext *ctx, void *opaque)
{
    int *interrupt_countdown = opaque;
    return !--*interrupt_countdown;
}

/* Compile the prelude once and cache the bytecode next to the source file
 * (<prelude>.binc). Re-parsing a ~100 KB prelude for each of the 50k+
 * test262 tests dominates the runtime of a full conformance run. */
static void prelude_prepare(const char *path)
{
    char *cache_path, *src;
    size_t src_len, bin_len;
    uint8_t *bin, *copy;
    time_t t_js, t_bin;
    struct stat st_js, st_bin;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue val;

    cache_path = malloc(strlen(path) + 6);
    if (!cache_path)
        return;
    sprintf(cache_path, "%s.binc", path);

    /* FIX: compare the ACTUAL mtimes.  t_js/t_bin were declared and never
     * assigned, so this used uninitialised stack garbage: the stale
     * <prelude>.binc was loaded as if it were fresh, and an edited prelude
     * (a changed .js with a fresh mtime) still ran with the OLD cached
     * bytecode -- silently. */
    if (stat(path, &st_js) == 0 && stat(cache_path, &st_bin) == 0 &&
        st_js.st_mtime <= st_bin.st_mtime) {
        bin = load_file(cache_path, &bin_len);
        if (bin) {
            prelude_bin = bin;
            prelude_bin_len = bin_len;
            free(cache_path);
            return;
        }
    }

    src = load_file(path, &src_len);
    if (!src) {
        warning("cannot load prelude %s", path);
        free(cache_path);
        return;
    }
    rt = MBJS_NewRuntime();
    ctx = rt ? MBJS_NewContext(rt) : NULL;
    if (!ctx) {
        free(src);
        if (rt)
            MBJS_FreeRuntime(rt);
        free(cache_path);
        return;
    }
    val = MBJS_Eval(ctx, src, src_len, path,
                    MBJS_EVAL_TYPE_GLOBAL | MBJS_EVAL_FLAG_COMPILE_ONLY);
    free(src);
    if (!MBJS_IsException(val)) {
        bin = MBJS_WriteObject(ctx, &bin_len, val, MBJS_WRITE_OBJ_BYTECODE);
        if (bin) {
            copy = malloc(bin_len ? bin_len : 1);
            if (copy) {
                memcpy(copy, bin, bin_len);
                FILE *f = fopen(cache_path, "wb");
                if (f) {
                    fwrite(copy, 1, bin_len, f);
                    fclose(f);
                }
                prelude_bin = copy;
                prelude_bin_len = bin_len;
            }
            mbjs_free(ctx, bin);
        }
    } else {
        warning("error compiling prelude %s", path);
    }
    MBJS_FreeValue(ctx, val);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    free(cache_path);
}

int run_test_buf(ThreadLocalStorage *tls, const char *filename, char *harness,
                 namelist_t *ip, char *buf, size_t buf_len,
                 const char* error_type, int eval_flags, bool is_negative,
                 bool is_async, bool can_block, bool track_promise_rejections,
                 int *msec)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    int i, ret;

    rt = MBJS_NewRuntime();
    if (rt == NULL) {
        fatal(1, "MBJS_NewRuntime failure");
    }
    MBJS_SetDumpFlags(rt, MBJS_DUMP_LEAKS);
    MBJS_SetRuntimeOpaque(rt, tls);
    mbjs_std_init_handlers(rt);
    ctx = MBJS_NewCustomContext(rt);
    if (ctx == NULL) {
        MBJS_FreeRuntime(rt);
        fatal(1, "MBJS_NewContext failure");
    }
    MBJS_SetRuntimeInfo(rt, filename);

    MBJS_SetCanBlock(rt, can_block);

    /* loader for ES6 modules */
    MBJS_SetModuleLoaderFunc2(rt, NULL, mbjs_module_loader_test, NULL, (void *) filename);

    if (track_promise_rejections)
        MBJS_SetHostPromiseRejectionTracker(rt, mbjs_std_promise_rejection_tracker, NULL);

    add_helpers(ctx);

    /* Load the OTA patch prelude (Intl + ECMA2025/2026 + Temporal shims) into
     * a fresh context before the harness, so feature-patched tests run with
     * the same surface the engine ships with. Each patch is IIFE-wrapped in
     * the prelude file (see test262_prelude.js) to avoid top-level clashes.
     * The prelude is compiled once and reused as cached bytecode (see
     * prelude_prepare) so a full 50k-test run does not re-parse it 50k times. */
    if (prelude_file) {
        if (prelude_bin) {
            MBJSValue pobj = MBJS_ReadObject(ctx, prelude_bin, prelude_bin_len,
                                             MBJS_READ_OBJ_BYTECODE);
            if (MBJS_IsException(pobj)) {
                warning("error loading prelude bytecode");
            } else {
                MBJSValue pres = MBJS_EvalFunction(ctx, pobj);
                if (MBJS_IsException(pres))
                    warning("error evaluating prelude %s", prelude_file);
                MBJS_FreeValue(ctx, pres);
            }
        } else {
            char *pc;
            size_t pl;
            int psec = 0;
            pc = load_file(prelude_file, &pl);
            if (pc) {
                if (eval_buf(ctx, pc, pl, prelude_file, false, false, NULL,
                             MBJS_EVAL_TYPE_GLOBAL, false, &psec)) {
                    warning("error evaluating prelude %s", prelude_file);
                }
                free(pc);
            } else {
                fatal(1, "cannot load prelude %s", prelude_file);
            }
        }
    }

    for (i = 0; i < ip->count; i++) {
        if (eval_file(ctx, harness, ip->array[i], MBJS_EVAL_TYPE_GLOBAL)) {
            fatal(1, "error including %s for %s", ip->array[i], filename);
        }
        // hack to get useful stack traces from Test262Error exceptions
        if (verbose > 1 && str_equal(ip->array[i], "sta.js")) {
            static const char hack[] =
                ";(function(C){"
                "globalThis.Test262Error = class Test262Error extends Error {};"
                "globalThis.Test262Error.thrower = C.thrower;"
                "})(Test262Error)";
            MBJS_FreeValue(ctx, MBJS_Eval(ctx, hack, sizeof(hack)-1, "sta.js", MBJS_EVAL_TYPE_GLOBAL));
        }
    }

    ret = eval_buf(ctx, buf, buf_len, filename, true, is_negative,
                   error_type, eval_flags, is_async, msec);
    ret = (ret != 0);

    if (dump_memory) {
        update_stats(rt, filename);
    }
    mbjs_agent_free(ctx);
    MBJS_FreeContext(ctx);
    mbjs_std_free_handlers(rt);
    MBJS_FreeRuntime(rt);

    atomic_inc(&test_count);
    if (ret)
        atomic_inc(&test_failed);
    return ret;
}

int run_test(ThreadLocalStorage *tls, const char *filename, int *msec)
{
    char harnessbuf[1024];
    char *harness;
    char *buf;
    size_t buf_len;
    char *desc, *p;
    char *error_type;
    int ret, eval_flags, use_strict, use_nostrict;
    bool is_negative, is_nostrict, is_onlystrict, is_async, is_module, skip;
    bool detect_module = true;
    bool track_promise_rejections = false;
    bool can_block;
    namelist_t include_list = { 0 }, *ip = &include_list;

    is_nostrict = is_onlystrict = is_negative = is_async = is_module = skip = false;
    can_block = true;
    error_type = NULL;
    buf = load_file(filename, &buf_len);

    harness = harness_dir;

    if (!harness) {
        const char *p1 = strstr(filename, "test/");
        if (p1) {
            snprintf(harnessbuf, sizeof(harnessbuf), "%.*s%s",
                     (int)(p1 - filename), filename, "harness");
        }
        harness = harnessbuf;
    }
    if (!local) {
        namelist_add(ip, NULL, "sta.js");
        namelist_add(ip, NULL, "assert.js");
    }
    /* extract the YAML frontmatter */
    desc = extract_desc(buf);
    if (desc) {
        char *ifile, *option;
        int state;
        p = find_tag(desc, "includes:", &state);
        if (p) {
            while ((ifile = get_option(&p, &state)) != NULL) {
                // skip unsupported harness files
                if (find_word(harness_exclude, ifile)) {
                    skip |= 1;
                } else {
                    namelist_add(ip, NULL, ifile);
                }
                free(ifile);
            }
        }
        p = find_tag(desc, "flags:", &state);
        if (p) {
            while ((option = get_option(&p, &state)) != NULL) {
                if (str_equal(option, "noStrict") ||
                    str_equal(option, "raw")) {
                    is_nostrict = true;
                    skip |= (test_mode == TEST_STRICT);
                }
                else if (str_equal(option, "onlyStrict")) {
                    is_onlystrict = true;
                    skip |= (test_mode == TEST_NOSTRICT);
                }
                else if (str_equal(option, "async")) {
                    is_async = true;
                    skip |= skip_async;
                }
                else if (str_equal(option, "mbjs:no-detect-module")) {
                    detect_module = false;
                }
                else if (str_equal(option, "mbjs:track-promise-rejections")) {
                    track_promise_rejections = true;
                }
                else if (str_equal(option, "module")) {
                    is_module = true;
                    skip |= skip_module;
                }
                else if (str_equal(option, "CanBlockIsFalse")) {
                    can_block = false;
                }
                free(option);
            }
        }
        p = find_tag(desc, "negative:", &state);
        if (p) {
            /* XXX: should extract the phase */
            char *q = find_tag(p, "type:", &state);
            if (q) {
                while (isspace((unsigned char)*q))
                    q++;
                error_type = strdup_len(q, strcspn(q, " \r\n"));
            }
            is_negative = true;
        }
        p = find_tag(desc, "features:", &state);
        if (p) {
            while ((option = get_option(&p, &state)) != NULL) {
                if (find_word(harness_features, option)) {
                    /* feature is enabled */
                } else if (find_word(harness_skip_features, option)) {
                    /* skip disabled feature */
                    skip |= 1;
                } else {
                    /* feature is not listed: skip and warn */
                    printf("%s:%d: unknown feature: %s\n", filename, 1, option);
                    skip |= 1;
                }
                free(option);
            }
        }
        free(desc);
    }
    if (is_async)
        namelist_add(ip, NULL, "doneprintHandle.js");

    use_strict = use_nostrict = 0;
    /* XXX: should remove 'test_mode' or simplify it just to force
       strict or non strict mode for single file tests */
    switch (test_mode) {
    case TEST_DEFAULT_NOSTRICT:
        if (is_onlystrict)
            use_strict = 1;
        else
            use_nostrict = 1;
        break;
    case TEST_DEFAULT_STRICT:
        if (is_nostrict)
            use_nostrict = 1;
        else
            use_strict = 1;
        break;
    case TEST_NOSTRICT:
        if (!is_onlystrict)
            use_nostrict = 1;
        break;
    case TEST_STRICT:
        if (!is_nostrict)
            use_strict = 1;
        break;
    case TEST_ALL:
        if (is_module) {
            use_nostrict = 1;
        } else {
            if (!is_nostrict)
                use_strict = 1;
            if (!is_onlystrict)
                use_nostrict = 1;
        }
        break;
    }

    if (skip || use_strict + use_nostrict == 0) {
        atomic_inc(&test_skipped);
        ret = -2;
    } else {
        if (local && detect_module) {
            is_module = MBJS_DetectModule(buf, buf_len);
        }
        if (is_module) {
            eval_flags = MBJS_EVAL_TYPE_MODULE;
        } else {
            eval_flags = MBJS_EVAL_TYPE_GLOBAL;
        }
        ret = 0;
        if (use_nostrict) {
            ret = run_test_buf(tls, filename, harness, ip, buf, buf_len,
                               error_type, eval_flags, is_negative, is_async,
                               can_block, track_promise_rejections, msec);
        }
        if (use_strict) {
            ret |= run_test_buf(tls, filename, harness, ip, buf, buf_len,
                                error_type, eval_flags | MBJS_EVAL_FLAG_STRICT,
                                is_negative, is_async, can_block,
                                track_promise_rejections, msec);
        }
    }
    namelist_free(&include_list);
    free(error_type);
    free(buf);

    return ret;
}

/* run a test when called by test262-harness+eshost */
int run_test262_harness_test(ThreadLocalStorage *tls, const char *filename,
                             bool is_module, bool can_block)
{
    MBJSRuntime *rt;
    MBJSContext *ctx;
    char *buf;
    size_t buf_len;
    int eval_flags, ret_code, ret;
    MBJSValue res_val;

    outfile = stdout; /* for mbjs_print_262 */

    rt = MBJS_NewRuntime();
    if (rt == NULL) {
        fatal(1, "MBJS_NewRuntime failure");
    }
    MBJS_SetDumpFlags(rt, MBJS_DUMP_LEAKS);
    MBJS_SetRuntimeOpaque(rt, tls);
    ctx = MBJS_NewContext(rt);
    if (ctx == NULL) {
        MBJS_FreeRuntime(rt);
        fatal(1, "MBJS_NewContext failure");
    }
    MBJS_SetRuntimeInfo(rt, filename);

    MBJS_SetCanBlock(rt, can_block);

    /* loader for ES6 modules */
    MBJS_SetModuleLoaderFunc2(rt, NULL, mbjs_module_loader_test, NULL, (void *) filename);

    add_helpers(ctx);

    buf = load_file(filename, &buf_len);

    if (is_module) {
      eval_flags = MBJS_EVAL_TYPE_MODULE;
    } else {
      eval_flags = MBJS_EVAL_TYPE_GLOBAL;
    }
    res_val = MBJS_Eval(ctx, buf, buf_len, filename, eval_flags);
    ret_code = 0;
    if (MBJS_IsException(res_val)) {
       mbjs_std_dump_error(ctx);
       ret_code = 1;
    } else {
        MBJSValue promise = MBJS_UNDEFINED;
        if (is_module) {
            promise = res_val;
        } else {
            MBJS_FreeValue(ctx, res_val);
        }
        for(;;) {
            MBJSContext *ctx1;
            ret = MBJS_ExecutePendingJob(MBJS_GetRuntime(ctx), &ctx1);
            if (ret < 0) {
                mbjs_std_dump_error(ctx1);
                ret_code = 1;
            } else if (ret == 0) {
                break;
            }
         }
         /* dump the error if the module returned an error. */
         if (is_module) {
             MBJSPromiseStateEnum state = MBJS_PromiseState(ctx, promise);
             if (state == MBJS_PROMISE_REJECTED) {
                 MBJS_Throw(ctx, MBJS_PromiseResult(ctx, promise));
                 mbjs_std_dump_error(ctx);
                 ret_code = 1;
             }
         }
         MBJS_FreeValue(ctx, promise);
    }
    free(buf);
    mbjs_agent_free(ctx);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    return ret_code;
}

clock_t last_clock;

void show_progress(void *unused) {
    int interval = 1000*1000*1000 / 4; // 250 ms

    mbjs_mutex_lock(&progress_mutex);
    while (mbjs_cond_timedwait(&progress_cond, &progress_mutex, interval)) {
        /* output progress indicator: erase end of line and return to col 0 */
        fprintf(stderr, "%d/%d/%d        \r",
                atomic_load(&test_failed),
                atomic_load(&test_count),
                atomic_load(&test_skipped));
        fflush(stderr);
    }
    mbjs_mutex_unlock(&progress_mutex);
}

enum { INCLUDE, EXCLUDE, SKIP };

int include_exclude_or_skip(int i) // naming is hard...
{
    if (namelist_find(&exclude_list, test_list.array[i]) >= 0)
        return EXCLUDE;
    if (i < start_index)
        return SKIP;
    if (stop_index >= 0 && i > stop_index)
        return SKIP;
    return INCLUDE;
}

void run_test_dir_list(void *arg)
{
    ThreadLocalStorage tls_s, *tls = &tls_s;
    const char *p;
    int i, msec;

    init_thread_local_storage(tls);

    for (i = (uintptr_t)arg; i < test_list.count; i += nthreads) {
        if (INCLUDE != include_exclude_or_skip(i))
            continue;
        p = test_list.array[i];
        msec = 0;
        int r = run_test(tls, p, &msec);
        fprintf(stdout, "###R %s %s\n", r == 0 ? "PASS" : (r < 0 ? "SKIP" : "FAIL"), p);
        fflush(stdout);
        if (verbose > 1 || (slow_test_threshold && msec >= slow_test_threshold))
            fprintf(stderr, "%s (%d ms)\n", p, msec);
    }
}

void help(void)
{
    printf("run-test262 version %s\n"
           "usage: run-test262 [options] {-f file ... | [dir_list] [index range]}\n"
           "-h             help\n"
           "-a             run tests in strict and nostrict modes\n"
           "-m             print memory usage summary\n"
           "-N             run test prepared by test262-harness+eshost\n"
           "-s             run tests in strict mode, skip @nostrict tests\n"
           "-E             only run tests from the error file\n"
           "-u             update error file\n"
           "-v             verbose: output error messages\n"
           "-vv            like -v but also print test name and running time\n"
           "-T duration    display tests taking more than 'duration' ms\n"
           "-t threads     number of parallel threads; default: numcpus - 1\n"
           "-c file        read configuration from 'file'\n"
           "-d dir         run all test files in directory tree 'dir'\n"
           "-e file        load the known errors from 'file'\n"
           "-f file        execute single test from 'file'\n"
           "-x file        exclude tests listed in 'file'\n"
           "--no-can-block set [[CanBlock]] to false (Atomics.wait will throw)\n",
           MBJS_GetVersion());
    exit(1);
}

char *get_opt_arg(const char *option, char *arg)
{
    if (!arg) {
        fatal(2, "missing argument for option %s", option);
    }
    return arg;
}

int main(int argc, char **argv)
{
    ThreadLocalStorage tls_s, *tls = &tls_s;
    int i, optind;
    bool is_dir_list;
    bool only_check_errors = false;
    const char *filename;
    const char *ignore = "";
    bool is_test262_harness = false;
    bool is_module = false;
    bool can_block = true;
    bool enable_progress = true;

    mbjs_std_set_worker_new_context_func(MBJS_NewCustomContext);

    init_thread_local_storage(tls);
    mbjs_mutex_init(&stats_mutex);

#ifndef _WIN32
    /* Date tests assume California local time */
    setenv("TZ", "America/Los_Angeles", 1);
#endif

    // minus one to not (over)commit the system completely
    nthreads = cpu_count() - 1;

    optind = 1;
    while (optind < argc) {
        char *arg = argv[optind];
        if (*arg != '-')
            break;
        optind++;
        if (strstr("-c -d -e -x -f -E -T -t", arg))
            optind++;
        if (strstr("-d -f", arg))
            ignore = "testdir"; // run only the tests from -d or -f
    }

    /* cannot use getopt because we want to pass the command line to
       the script */
    optind = 1;
    is_dir_list = true;
    while (optind < argc) {
        char *arg = argv[optind];
        if (*arg != '-')
            break;
        optind++;
        if (str_equal(arg, "-h")) {
            help();
        } else if (str_equal(arg, "-m")) {
            dump_memory++;
        } else if (str_equal(arg, "-s")) {
            test_mode = TEST_STRICT;
        } else if (str_equal(arg, "-a")) {
            test_mode = TEST_ALL;
        } else if (str_equal(arg, "-u")) {
            update_errors++;
        } else if (arg == strstr(arg, "-v")) {
            verbose += str_count(arg, "v");
        } else if (str_equal(arg, "-c")) {
            load_config(get_opt_arg(arg, argv[optind++]), ignore);
        } else if (str_equal(arg, "-P")) {
            prelude_file = get_opt_arg(arg, argv[optind++]);
        } else if (str_equal(arg, "-d")) {
            enumerate_tests(get_opt_arg(arg, argv[optind++]));
        } else if (str_equal(arg, "-e")) {
            error_filename = get_opt_arg(arg, argv[optind++]);
        } else if (str_equal(arg, "-x")) {
            namelist_load(&exclude_list, get_opt_arg(arg, argv[optind++]));
        } else if (str_equal(arg, "-f")) {
            is_dir_list = false;
        } else if (str_equal(arg, "-E")) {
            only_check_errors = true;
        } else if (str_equal(arg, "-T")) {
            slow_test_threshold = atoi(get_opt_arg(arg, argv[optind++]));
        } else if (str_equal(arg, "-t")) {
            nthreads = atoi(get_opt_arg(arg, argv[optind++]));
        } else if (str_equal(arg, "-N")) {
            is_test262_harness = true;
        } else if (str_equal(arg, "--module")) {
            is_module = true;
        } else if (str_equal(arg, "--no-can-block")) {
            can_block = false;
        } else {
            fatal(1, "unknown option: %s", arg);
            break;
        }
    }

    if (optind >= argc && !test_list.count)
        help();

    if (prelude_file)
        prelude_prepare(prelude_file);

    if (is_test262_harness) {
        return run_test262_harness_test(tls, argv[optind], is_module, can_block);
    }

    nthreads = max_int(nthreads, 1);
    nthreads = min_int(nthreads, countof(threads));

    error_out = stdout;
    if (error_filename) {
        error_file = load_file(error_filename, NULL);
        if (only_check_errors && error_file) {
            namelist_free(&test_list);
            namelist_add_from_error_file(&test_list, error_file);
        }
        if (update_errors) {
            free(error_file);
            error_file = NULL;
            error_out = fopen(error_filename, "w");
            if (!error_out) {
                perror_exit(1, error_filename);
            }
        }
    }

    update_exclude_dirs();

#ifndef _WIN32
    if (!isatty(STDOUT_FILENO)) {
        enable_progress = false;
    }
#endif

    if (is_dir_list) {
        if (optind < argc && !isdigit((unsigned char)argv[optind][0])) {
            filename = argv[optind++];
            namelist_load(&test_list, filename);
        }
        start_index = 0;
        stop_index = -1;
        if (optind < argc) {
            start_index = atoi(argv[optind++]);
            if (optind < argc) {
                stop_index = atoi(argv[optind++]);
            }
        }
        // exclude_dir_list has already been sorted by update_exclude_dirs()
        namelist_sort(&test_list);
        namelist_sort(&exclude_list);
        for (i = 0; i < test_list.count; i++) {
            switch (include_exclude_or_skip(i)) {
            case EXCLUDE:
                test_excluded++;
                break;
            case SKIP:
                test_skipped++;
                break;
            }
        }
        mbjs_cond_init(&progress_cond);
        mbjs_mutex_init(&progress_mutex);
        if (enable_progress) {
            mbjs_thread_create(&progress_thread, show_progress, NULL, /*flags*/0);
        }
        for (i = 0; i < nthreads; i++) {
            mbjs_thread_create(&threads[i], run_test_dir_list,
                             (void *)(uintptr_t)i, /*flags*/0);
        }
        for (i = 0; i < nthreads; i++)
            mbjs_thread_join(threads[i]);
        mbjs_mutex_lock(&progress_mutex);
        mbjs_cond_signal(&progress_cond);
        mbjs_mutex_unlock(&progress_mutex);
        if (enable_progress) {
            mbjs_thread_join(progress_thread);
        }
        mbjs_mutex_destroy(&progress_mutex);
        mbjs_cond_destroy(&progress_cond);
    } else {
        while (optind < argc) {
            int msec = 0;
            run_test(tls, argv[optind++], &msec);
        }
    }

    if (dump_memory) {
        if (dump_memory > 1 && stats_count > 1) {
            printf("\nMininum memory statistics for %s:\n\n", stats_min_filename);
            MBJS_DumpMemoryUsage(stdout, &stats_min, NULL);
            printf("\nMaximum memory statistics for %s:\n\n", stats_max_filename);
            MBJS_DumpMemoryUsage(stdout, &stats_max, NULL);
        }
        printf("\nAverage memory statistics for %d tests:\n\n", stats_count);
        MBJS_DumpMemoryUsage(stdout, &stats_avg, NULL);
        printf("\n");
    }

    if (is_dir_list) {
        fprintf(stderr, "Result: %d/%d error%s",
                test_failed, test_count, test_count != 1 ? "s" : "");
        if (test_excluded)
            fprintf(stderr, ", %d excluded", test_excluded);
        if (test_skipped)
            fprintf(stderr, ", %d skipped", test_skipped);
        if (error_file) {
            if (new_errors)
                fprintf(stderr, ", %d new", new_errors);
            if (changed_errors)
                fprintf(stderr, ", %d changed", changed_errors);
            if (fixed_errors)
                fprintf(stderr, ", %d fixed", fixed_errors);
        }
        fprintf(stderr, "\n");
    }

    if (error_out && error_out != stdout) {
        fclose(error_out);
        error_out = NULL;
    }

    namelist_free(&test_list);
    namelist_free(&exclude_list);
    namelist_free(&exclude_dir_list);
    free(harness_dir);
    free(harness_features);
    free(harness_exclude);
    free(harness_skip_features);
    free(error_file);
    free(error_filename);
    free(stats_min_filename);
    free(stats_max_filename);

    /* Signal that the error file is out of date. */
    return new_errors || changed_errors || fixed_errors;
}
