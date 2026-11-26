/*
 * mbjs C library
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
#include "mbjs.h"

#include "builtin-mochievents.h"
#include "builtin-mochipath.h"
#include "builtin-mochiutil.h"
#include "builtin-mochibuffer.h"
#include "builtin-mochistream.h"
#include "builtin-mochios.h"
#include "builtin-mochiurl.h"
#include "builtin-mochiasync_hooks.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#if !defined(_MSC_VER)
#include <sys/time.h>
#include <unistd.h>
#endif
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>
#if !defined(_MSC_VER)
#include <dirent.h>
#endif
#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utime.h>
#define popen _popen
#define pclose _pclose
#define rmdir _rmdir
#define getcwd _getcwd
#define chdir _chdir
#else
#include <sys/ioctl.h>
#include <poll.h>
#if !defined(__wasi__)
#include <dlfcn.h>
#endif

#if !defined(__wasi__)
#include <termios.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <grp.h>
#endif

#if defined(__APPLE__)
typedef sig_t sighandler_t;
#include <crt_externs.h>
#include <TargetConditionals.h>
#define environ (*_NSGetEnviron())
#endif

#ifdef __sun
typedef void (*sighandler_t)(int);
extern char **environ;
#endif

#if defined(__OpenBSD__) || defined(__FreeBSD__) || defined(__NetBSD__)
typedef sig_t sighandler_t;
extern char **environ;
#endif

#endif /* _WIN32 */

#include "cutils.h"
#include "list.h"
#include "mbjs-libc.h"

#if MBJS_HAVE_THREADS
#include "mbjs-c-atomics.h"
#define USE_WORKER // enable os.Worker
#endif

#ifndef S_IFBLK
#define S_IFBLK 0
#endif

#ifndef S_IFIFO
#define S_IFIFO 0
#endif

#ifndef MAX_SAFE_INTEGER // already defined in amalgamation builds
#define MAX_SAFE_INTEGER (((int64_t) 1 << 53) - 1)
#endif

#ifndef MBJS_NATIVE_MODULE_SUFFIX
#ifdef _WIN32
#define MBJS_NATIVE_MODULE_SUFFIX ".dll"
#else
#define MBJS_NATIVE_MODULE_SUFFIX ".so"
#endif
#endif

/* TODO:
   - add socket calls
*/

typedef struct {
    struct list_head link;
    int fd;
    MBJSValue rw_func[2];
} MBJSOSRWHandler;

typedef struct {
    struct list_head link;
    int sig_num;
    MBJSValue func;
} MBJSOSSignalHandler;

typedef struct {
    struct list_head link;
    int64_t timer_id;
    uint8_t repeats:1;
    int64_t timeout;
    int64_t delay;
    MBJSValue func;
} MBJSOSTimer;

typedef struct {
    struct list_head link;
    MBJSValue promise;
    MBJSValue reason;
} MBJSRejectedPromiseEntry;

#ifdef USE_WORKER

typedef struct {
    struct list_head link;
    uint8_t *data;
    size_t data_len;
    /* list of SharedArrayBuffers, necessary to free the message */
    uint8_t **sab_tab;
    size_t sab_tab_len;
} MBJSWorkerMessage;

typedef struct MBJSWaker {
#ifdef _WIN32
    HANDLE handle;
#else
    int read_fd;
    int write_fd;
#endif
} MBJSWaker;

typedef struct {
    int ref_count;
    mbjs_mutex_t mutex;
    struct list_head msg_queue; /* list of MBJSWorkerMessage.link */
    MBJSWaker waker;
} MBJSWorkerMessagePipe;

typedef struct {
    struct list_head link;
    MBJSWorkerMessagePipe *recv_pipe;
    MBJSValue on_message_func;
} MBJSWorkerMessageHandler;

#endif // USE_WORKER

typedef struct MBJSThreadState {
    struct list_head os_rw_handlers; /* list of MBJSOSRWHandler.link */
    struct list_head os_signal_handlers; /* list MBJSOSSignalHandler.link */
    struct list_head os_timers; /* list of MBJSOSTimer.link */
    struct list_head port_list; /* list of MBJSWorkerMessageHandler.link */
    struct list_head rejected_promise_list; /* list of MBJSRejectedPromiseEntry.link */
    int eval_script_recurse; /* only used in the main thread */
    int64_t next_timer_id; /* for setTimeout / setInterval */
    bool can_js_os_poll;
    /* not used in the main thread */
#ifdef USE_WORKER
    MBJSWorkerMessagePipe *recv_pipe, *send_pipe;
#else
    void *recv_pipe;
#endif // USE_WORKER
    MBJSClassID std_file_class_id;
    MBJSClassID worker_class_id;
} MBJSThreadState;

static uint64_t os_pending_signals;

static void *mbjs_std_dbuf_realloc(void *opaque, void *ptr, size_t size)
{
    MBJSRuntime *rt = opaque;
    return mbjs_realloc_rt(rt, ptr, size);
}

static void mbjs_std_dbuf_init(MBJSContext *ctx, DynBuf *s)
{
    dbuf_init2(s, MBJS_GetRuntime(ctx), mbjs_std_dbuf_realloc);
}

static bool my_isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

static MBJSThreadState *mbjs_get_thread_state(MBJSRuntime *rt)
{
    return (MBJSThreadState *)mbjs_std_cmd(/*GetOpaque*/0, rt);
}

static void mbjs_set_thread_state(MBJSRuntime *rt, MBJSThreadState *ts)
{
    mbjs_std_cmd(/*SetOpaque*/1, rt, ts);
}

// Non-CL Clang on Windows does not define __GNUC__
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif // __GNUC__ || __clang__
static MBJSValue mbjs_printf_internal(MBJSContext *ctx,
                                  int argc, MBJSValueConst *argv, FILE *fp)
{
    char fmtbuf[32];
    uint8_t cbuf[UTF8_CHAR_LEN_MAX+1];
    MBJSValue res;
    DynBuf dbuf;
    const char *fmt_str = NULL;
    const uint8_t *fmt, *fmt_end;
    const uint8_t *p;
    char *q;
    int i, c, len, mod;
    size_t fmt_len;
    int32_t int32_arg;
    int64_t int64_arg;
    double double_arg;
    const char *string_arg;

    mbjs_std_dbuf_init(ctx, &dbuf);

    if (argc > 0) {
        fmt_str = MBJS_ToCStringLen(ctx, &fmt_len, argv[0]);
        if (!fmt_str)
            goto fail;

        i = 1;
        fmt = (const uint8_t *)fmt_str;
        fmt_end = fmt + fmt_len;
        while (fmt < fmt_end) {
            for (p = fmt; fmt < fmt_end && *fmt != '%'; fmt++)
                continue;
            dbuf_put(&dbuf, p, fmt - p);
            if (fmt >= fmt_end)
                break;
            q = fmtbuf;
            *q++ = *fmt++;  /* copy '%' */

            /* flags */
            for(;;) {
                c = *fmt;
                if (c == '0' || c == '#' || c == '+' || c == '-' || c == ' ' ||
                    c == '\'') {
                    if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                        goto invalid;
                    *q++ = c;
                    fmt++;
                } else {
                    break;
                }
            }
            /* width */
            if (*fmt == '*') {
                if (i >= argc)
                    goto missing;
                if (MBJS_ToInt32(ctx, &int32_arg, argv[i++]))
                    goto fail;
                q += snprintf(q, fmtbuf + sizeof(fmtbuf) - q, "%d", int32_arg);
                fmt++;
            } else {
                while (my_isdigit(*fmt)) {
                    if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                        goto invalid;
                    *q++ = *fmt++;
                }
            }
            if (*fmt == '.') {
                if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                    goto invalid;
                *q++ = *fmt++;
                if (*fmt == '*') {
                    if (i >= argc)
                        goto missing;
                    if (MBJS_ToInt32(ctx, &int32_arg, argv[i++]))
                        goto fail;
                    q += snprintf(q, fmtbuf + sizeof(fmtbuf) - q, "%d", int32_arg);
                    fmt++;
                } else {
                    while (my_isdigit(*fmt)) {
                        if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                            goto invalid;
                        *q++ = *fmt++;
                    }
                }
            }

            /* we only support the "l" modifier for 64 bit numbers */
            mod = ' ';
            if (*fmt == 'l') {
                mod = *fmt++;
            }

            /* type */
            c = *fmt++;
            if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                goto invalid;
            *q++ = c;
            *q = '\0';

            switch (c) {
            case 'c':
                if (i >= argc)
                    goto missing;
                if (MBJS_IsString(argv[i])) {
                    // TODO(chqrlie) need an API to wrap charCodeAt and codePointAt */
                    string_arg = MBJS_ToCString(ctx, argv[i++]);
                    if (!string_arg)
                        goto fail;
                    int32_arg = utf8_decode((const uint8_t *)string_arg, &p);
                    MBJS_FreeCString(ctx, string_arg);
                } else {
                    if (MBJS_ToInt32(ctx, &int32_arg, argv[i++]))
                        goto fail;
                }
                // XXX: throw an exception?
                if ((unsigned)int32_arg > 0x10FFFF)
                    int32_arg = 0xFFFD;
                /* ignore conversion flags, width and precision */
                len = utf8_encode(cbuf, int32_arg);
                dbuf_put(&dbuf, cbuf, len);
                break;

            case 'd':
            case 'i':
            case 'o':
            case 'u':
            case 'x':
            case 'X':
                if (i >= argc)
                    goto missing;
                if (MBJS_ToInt64Ext(ctx, &int64_arg, argv[i++]))
                    goto fail;
                if (mod == 'l') {
                    /* 64 bit number */
#if defined(_WIN32)
                    if (q >= fmtbuf + sizeof(fmtbuf) - 3)
                        goto invalid;
                    q[2] = q[-1];
                    q[-1] = 'I';
                    q[0] = '6';
                    q[1] = '4';
                    q[3] = '\0';
                    dbuf_printf(&dbuf, fmtbuf, (int64_t)int64_arg);
#else
                    if (q >= fmtbuf + sizeof(fmtbuf) - 2)
                        goto invalid;
                    q[1] = q[-1];
                    q[-1] = q[0] = 'l';
                    q[2] = '\0';
                    dbuf_printf(&dbuf, fmtbuf, (long long)int64_arg);
#endif
                } else {
                    dbuf_printf(&dbuf, fmtbuf, (int)int64_arg);
                }
                break;

            case 's':
                if (i >= argc)
                    goto missing;
                /* XXX: handle strings containing null characters */
                string_arg = MBJS_ToCString(ctx, argv[i++]);
                if (!string_arg)
                    goto fail;
                dbuf_printf(&dbuf, fmtbuf, string_arg);
                MBJS_FreeCString(ctx, string_arg);
                break;

            case 'e':
            case 'f':
            case 'g':
            case 'a':
            case 'E':
            case 'F':
            case 'G':
            case 'A':
                if (i >= argc)
                    goto missing;
                if (MBJS_ToFloat64(ctx, &double_arg, argv[i++]))
                    goto fail;
                dbuf_printf(&dbuf, fmtbuf, double_arg);
                break;

            case '%':
                dbuf_putc(&dbuf, '%');
                break;

            default:
                /* XXX: should support an extension mechanism */
            invalid:
                MBJS_ThrowTypeError(ctx, "invalid conversion specifier in format string");
                goto fail;
            missing:
                MBJS_ThrowReferenceError(ctx, "missing argument for conversion specifier");
                goto fail;
            }
        }
        MBJS_FreeCString(ctx, fmt_str);
    }
    if (dbuf.error) {
        res = MBJS_ThrowOutOfMemory(ctx);
    } else {
        if (fp) {
            len = fwrite(dbuf.buf, 1, dbuf.size, fp);
            res = MBJS_NewInt32(ctx, len);
        } else {
            res = MBJS_NewStringLen(ctx, (char *)dbuf.buf, dbuf.size);
        }
    }
    dbuf_free(&dbuf);
    return res;

fail:
    MBJS_FreeCString(ctx, fmt_str);
    dbuf_free(&dbuf);
    return MBJS_EXCEPTION;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop // ignored "-Wformat-nonliteral"
#endif // __GNUC__ || __clang__

uint8_t *mbjs_load_file(MBJSContext *ctx, size_t *pbuf_len, const char *filename)
{
    FILE *f;
    size_t n, len;
    uint8_t *p, *buf, tmp[8192];

    f = fopen(filename, "rb");
    if (!f)
        return NULL;
    buf = NULL;
    len = 0;
    do {
        n = fread(tmp, 1, sizeof(tmp), f);
        if (ctx) {
            p = mbjs_realloc(ctx, buf, len + n + 1);
        } else {
            p = realloc(buf, len + n + 1);
        }
        if (!p) {
            if (ctx) {
                mbjs_free(ctx, buf);
            } else {
                free(buf);
            }
            fclose(f);
            return NULL;
        }
        memcpy(&p[len], tmp, n);
        buf = p;
        len += n;
        buf[len] = '\0';
    } while (n == sizeof(tmp));
    fclose(f);
    *pbuf_len = len;
    return buf;
}

/* load and evaluate a file */
static MBJSValue mbjs_loadScript(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    MBJSValue ret;
    size_t buf_len;

    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        return MBJS_EXCEPTION;
    buf = mbjs_load_file(ctx, &buf_len, filename);
    if (!buf) {
        MBJS_ThrowReferenceError(ctx, "could not load '%s'", filename);
        MBJS_FreeCString(ctx, filename);
        return MBJS_EXCEPTION;
    }
    ret = MBJS_Eval(ctx, (char *)buf, buf_len, filename,
                  MBJS_EVAL_TYPE_GLOBAL);
    mbjs_free(ctx, buf);
    MBJS_FreeCString(ctx, filename);
    return ret;
}

static int get_bool_option(MBJSContext *ctx, bool *pbool,
                           MBJSValueConst obj,
                           const char *option)
{
    MBJSValue val;
    val = MBJS_GetPropertyStr(ctx, obj, option);
    if (MBJS_IsException(val))
        return -1;
    if (!MBJS_IsUndefined(val)) {
        *pbool = MBJS_ToBool(ctx, val);
    }
    MBJS_FreeValue(ctx, val);
    return 0;
}

// mbjs_realloc_array_buffer to avoid a name conflict with
// mbjs_array_buffer_realloc from mbjs.c in the amalgamation build
static void *mbjs_realloc_array_buffer(MBJSRuntime *rt, void *opaque, void *ptr,
                                     size_t size)
{
    return mbjs_realloc_rt(rt, ptr, size);
}

/* load a file as a UTF-8 encoded string or Uint8Array */
static MBJSValue mbjs_std_loadFile(MBJSContext *ctx, MBJSValueConst this_val,
                               int argc, MBJSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    MBJSValueConst options_obj;
    MBJSValue ret;
    size_t buf_len;
    bool binary = false;

    if (argc >= 2) {
        options_obj = argv[1];
        if (get_bool_option(ctx, &binary, options_obj,
                            "binary"))
            return MBJS_EXCEPTION;
    }

    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        return MBJS_EXCEPTION;
    buf = mbjs_load_file(ctx, &buf_len, filename);
    MBJS_FreeCString(ctx, filename);
    if (!buf)
        return MBJS_NULL;
    if (binary) {
        ret = MBJS_NewUint8Array(ctx, buf, buf_len, mbjs_realloc_array_buffer,
                               NULL, false);
    } else {
        ret = MBJS_NewStringLen(ctx, (char *)buf, buf_len);
        mbjs_free(ctx, buf);
    }

    return ret;
}

static MBJSValue mbjs_std_writeFile(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv)
{
    const char *filename;
    const char *mode;
    const void *buf;
    size_t len, n;
    MBJSValueConst data;
    MBJSValue val, ret, unref;
    bool release;
    FILE *fp;

    ret = MBJS_EXCEPTION;
    len = 0;
    buf = "";
    mode = "w";
    data = argv[1];
    unref = MBJS_UNDEFINED;
    release = false;
    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        return MBJS_EXCEPTION;
    if (MBJS_IsObject(data)) {
        val = MBJS_GetPropertyStr(ctx, data, "buffer");
        if (MBJS_IsException(val))
            goto exception;
        if (MBJS_IsArrayBuffer(val)) {
            data = unref = val;
        } else {
            MBJS_FreeValue(ctx, val);
        }
    }
    if (MBJS_IsArrayBuffer(data)) {
        buf = MBJS_GetArrayBuffer(ctx, &len, data);
        mode = "wb";
    } else if (!MBJS_IsUndefined(data)) {
        buf = MBJS_ToCStringLen(ctx, &len, data);
        release = true;
    }
    if (!buf)
        goto exception;
    fp = fopen(filename, mode);
    if (!fp) {
        MBJS_ThrowPlainError(ctx, "error opening %s for writing", filename);
        goto exception;
    }
    n = fwrite(buf, len, 1, fp);
    fclose(fp);
    if (n != 1) {
        MBJS_ThrowPlainError(ctx, "error writing to %s", filename);
        goto exception;
    }
    ret = MBJS_UNDEFINED;
exception:
    MBJS_FreeCString(ctx, filename);
    if (release)
        MBJS_FreeCString(ctx, buf);
    MBJS_FreeValue(ctx, unref);
    return ret;
}

typedef MBJSModuleDef *(MBJSInitModuleFunc)(MBJSContext *ctx,
                                        const char *module_name);


#if defined(_WIN32)
static MBJSModuleDef *mbjs_module_loader_so(MBJSContext *ctx,
                                        const char *module_name)
{
    MBJSModuleDef *m;
    HINSTANCE hd;
    MBJSInitModuleFunc *init;
    char *filename = NULL;
    size_t len = strlen(module_name);
    bool is_absolute = len > 2 && ((module_name[0] >= 'A' && module_name[0] <= 'Z') ||
            (module_name[0] >= 'a' && module_name[0] <= 'z')) && module_name[1] == ':';
    bool is_relative = len > 2 && module_name[0] == '.' && (module_name[1] == '/' || module_name[1] == '\\');
    if (is_absolute || is_relative) {
        filename = (char *)module_name;
    } else {
        filename = mbjs_malloc(ctx, len + 2 + 1);
        if (!filename)
            return NULL;
        strcpy(filename, "./");
        strcpy(filename + 2, module_name);
    }
    hd = LoadLibraryA(filename);
    if (filename != module_name)
        mbjs_free(ctx, filename);
    if (hd == NULL) {
        MBJS_ThrowReferenceError(ctx, "mbjs_load_module '%s' error: %lu",
                               module_name, GetLastError());
        goto fail;
    }
    init = (MBJSInitModuleFunc *)(uintptr_t)GetProcAddress(hd, "mbjs_init_module");
    if (!init) {
        MBJS_ThrowReferenceError(ctx, "mbjs_init_module '%s' not found: %lu",
                               module_name, GetLastError());
        goto fail;
    }
    m = init(ctx, module_name);
    if (!m) {
        MBJS_ThrowReferenceError(ctx, "mbjs_call_module '%s' initialization error",
                               module_name);
    fail:
        if (hd != NULL)
            FreeLibrary(hd);
        return NULL;
    }
    return m;
}
#elif defined(__wasi__)
static MBJSModuleDef *mbjs_module_loader_so(MBJSContext *ctx,
                                        const char *module_name)
{
    MBJS_ThrowReferenceError(ctx, "shared library modules are not supported yet");
    return NULL;
}
#else
static MBJSModuleDef *mbjs_module_loader_so(MBJSContext *ctx,
                                        const char *module_name)
{
    MBJSModuleDef *m;
    void *hd;
    MBJSInitModuleFunc *init;
    char *filename;

    if (!strchr(module_name, '/')) {
        /* must add a '/' so that the DLL is not searched in the
           system library paths */
        filename = mbjs_malloc(ctx, strlen(module_name) + 2 + 1);
        if (!filename)
            return NULL;
        strcpy(filename, "./");
        strcpy(filename + 2, module_name);
    } else {
        filename = (char *)module_name;
    }

    /* C module */
    hd = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
    if (filename != module_name)
        mbjs_free(ctx, filename);
    if (!hd) {
        MBJS_ThrowReferenceError(ctx, "could not load module filename '%s' as shared library: %s",
                               module_name, dlerror());
        goto fail;
    }

    *(void **) (&init) = dlsym(hd, "mbjs_init_module");
    if (!init) {
        MBJS_ThrowReferenceError(ctx, "could not load module filename '%s': mbjs_init_module not found",
                               module_name);
        goto fail;
    }

    m = init(ctx, module_name);
    if (!m) {
        MBJS_ThrowReferenceError(ctx, "could not load module filename '%s': initialization error",
                               module_name);
    fail:
        if (hd)
            dlclose(hd);
        return NULL;
    }
    return m;
}
#endif /* !_WIN32 */

int mbjs_module_set_import_meta(MBJSContext *ctx, MBJSValueConst func_val,
                              bool use_realpath, bool is_main)
{
    MBJSModuleDef *m;
    char buf[MBJS__PATH_MAX + 16];
    MBJSValue meta_obj;
    MBJSAtom module_name_atom;
    const char *module_name;

    assert(MBJS_VALUE_GET_TAG(func_val) == MBJS_TAG_MODULE);
    m = MBJS_VALUE_GET_PTR(func_val);

    module_name_atom = MBJS_GetModuleName(ctx, m);
    module_name = MBJS_AtomToCString(ctx, module_name_atom);
    MBJS_FreeAtom(ctx, module_name_atom);
    if (!module_name)
        return -1;
    if (!strchr(module_name, ':')) {
            strcpy(buf, "file://");
    #if !defined(_WIN32) && !defined(__wasi__)
            /* realpath() cannot be used with modules compiled with mbjsc
               because the corresponding module source code is not
               necessarily present */
            if (use_realpath) {
                char *res = realpath(module_name, buf + strlen(buf));
                if (!res) {
                    MBJS_ThrowTypeError(ctx, "realpath failure");
                    MBJS_FreeCString(ctx, module_name);
                    return -1;
                }
            } else {
                mbjs__pstrcat(buf, sizeof(buf), module_name);
            }
    #elif defined(_WIN32)
            /* Windows has no realpath().  _fullpath still resolves against the
               cwd, so a module reached through a RELATIVE specifier gets an
               absolute file URL ('file://E:/...') instead of one that only
               works from the exact cwd the process happened to start in. */
            if (use_realpath) {
                char *res = _fullpath(buf + strlen(buf), module_name,
                                      sizeof(buf) - strlen(buf) - 1);
                if (!res) {
                    MBJS_ThrowTypeError(ctx, "realpath failure");
                    MBJS_FreeCString(ctx, module_name);
                    return -1;
                }
                /* _fullpath writes backslashes; make it a forward-slash URL */
                {
                    char *q;
                    for (q = buf; *q; q++)
                        if (*q == '\\') *q = '/';
                }
            } else {
                mbjs__pstrcat(buf, sizeof(buf), module_name);
            }
    #else
            mbjs__pstrcat(buf, sizeof(buf), module_name);
    #endif
        } else {
        mbjs__pstrcpy(buf, sizeof(buf), module_name);
    }
    MBJS_FreeCString(ctx, module_name);

    meta_obj = MBJS_GetImportMeta(ctx, m);
    if (MBJS_IsException(meta_obj))
        return -1;
    MBJS_DefinePropertyValueStr(ctx, meta_obj, "url",
                              MBJS_NewString(ctx, buf),
                              MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, meta_obj, "main",
                              MBJS_NewBool(ctx, is_main),
                              MBJS_PROP_C_W_E);
    MBJS_FreeValue(ctx, meta_obj);
    return 0;
}

static int default_module_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    MBJSValue val;
    val = MBJS_GetModulePrivateValue(ctx, m);
    MBJS_SetModuleExport(ctx, m, "default", val);
    return 0;
}

/* in order to conform with the specification, only the keys should be
   tested and not the associated values. */
int mbjs_module_check_attributes(MBJSContext *ctx, void *opaque,
                               MBJSValueConst attributes)
{
    MBJSPropertyEnum *tab;
    uint32_t i, len;
    int ret;
    const char *cstr;
    size_t cstr_len;

    if (MBJS_GetOwnPropertyNames(ctx, &tab, &len, attributes, MBJS_GPN_ENUM_ONLY | MBJS_GPN_STRING_MASK))
        return -1;
    ret = 0;
    for(i = 0; i < len; i++) {
        cstr = MBJS_AtomToCStringLen(ctx, &cstr_len, tab[i].atom);
        if (!cstr) {
            ret = -1;
            break;
        }
        if (!(cstr_len == 4 && !memcmp(cstr, "type", cstr_len))) {
            MBJS_ThrowTypeError(ctx, "import attribute '%s' is not supported", cstr);
            ret = -1;
        }
        MBJS_FreeCString(ctx, cstr);
        if (ret)
            break;
    }
    MBJS_FreePropertyEnum(ctx, tab, len);
    return ret;
}

enum {
    MBJS_IMPORT_TYPE_JS,
    MBJS_IMPORT_TYPE_JSON,
    MBJS_IMPORT_TYPE_TEXT,
    MBJS_IMPORT_TYPE_BYTES,
};

/* return > 0 if the attributes indicate a JSON module, 0 otherwise, -1 on error */
static int mbjs_module_import_type(MBJSContext *ctx, MBJSValueConst attributes)
{
    MBJSValue str;
    const char *cstr;
    size_t len;
    int res;

    if (MBJS_IsUndefined(attributes))
        return MBJS_IMPORT_TYPE_JS;
    str = MBJS_GetPropertyStr(ctx, attributes, "type");
    if (MBJS_IsException(str))
        return -1;
    if (!MBJS_IsString(str)) {
        MBJS_FreeValue(ctx, str);
        return MBJS_IMPORT_TYPE_JS;
    }
    cstr = MBJS_ToCStringLen(ctx, &len, str);
    MBJS_FreeValue(ctx, str);
    if (!cstr)
        return -1;
    if (len == 4 && !memcmp(cstr, "json", len)) {
        res = MBJS_IMPORT_TYPE_JSON;
    } else if (len == 4 && !memcmp(cstr, "text", len)) {
        res = MBJS_IMPORT_TYPE_TEXT;
    } else if (len == 5 && !memcmp(cstr, "bytes", len)) {
        res = MBJS_IMPORT_TYPE_BYTES;
    } else {
        /* unknown type - throw error */
        MBJS_ThrowTypeError(ctx, "unsupported module type: '%s'", cstr);
        res = -1;
    }
    MBJS_FreeCString(ctx, cstr);
    return res;
}

/* --- CommonJS interop ---------------------------------------------------- */
/* npm packages mix ESM and CommonJS. When a .js file looks like CommonJS
   (module.exports / exports. / require()), it is handed to the JS-side
   loader hook globalThis.__MOCHI_CJS_LOAD__(absolute_path), which returns
   module.exports. Those exports are then exposed to ESM importers the way
   Node does: 'default' plus one named export per own enumerable key. */

static int mbjs_source_looks_cjs(const char *buf, size_t len)
{
    size_t i;
    if (!buf)
        return 0;
    if (len > 8 * 1024 * 1024)
        len = 8 * 1024 * 1024;
    /* A top-level import/export statement means the file is an ES module,
       even when its comments mention require() or module.exports. */
    for (i = 0; i + 7 <= len; i++) {
        const char *p = buf + i;
        if (i != 0 && p[-1] != '\n' && p[-1] != '\r')
            continue;
        if (!memcmp(p, "import ", 7) || !memcmp(p, "export ", 7))
            return 0;
    }
    for (i = 0; i + 13 <= len; i++) {
        const char *p = buf + i;
        if (p[0] == 'm' && !memcmp(p, "module.exports", 13))
            return 1;
    }
    for (i = 0; i + 8 <= len; i++) {
        const char *p = buf + i;
        if (p[0] == 'e' && !memcmp(p, "exports.", 8))
            return 1;
        if (p[0] == 'r' && !memcmp(p, "require(", 8))
            return 1;
    }
    return 0;
}

/* CJS module init: runs at instantiation, after mbjs_create_module_function has
   created the export var_refs, so MBJS_SetModuleExport is safe here. The module's
   private value holds module.exports (the object returned by the loader hook). */
static int cjs_module_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    MBJSValue exports_obj;
    MBJSPropertyEnum *tab = NULL;
    uint32_t len = 0, i;
    const char *name;

    exports_obj = MBJS_GetModulePrivateValue(ctx, m); /* dup */
    MBJS_SetModuleExport(ctx, m, "default", MBJS_DupValue(ctx, exports_obj));
    if (MBJS_GetOwnPropertyNames(ctx, &tab, &len, exports_obj,
                                 MBJS_GPN_ENUM_ONLY | MBJS_GPN_STRING_MASK) == 0) {
        for (i = 0; i < len; i++) {
            name = MBJS_AtomToCString(ctx, tab[i].atom);
            if (name) {
                MBJSValue v = MBJS_GetProperty(ctx, exports_obj, tab[i].atom);
                if (!MBJS_IsException(v))
                    MBJS_SetModuleExport(ctx, m, name, v); /* consumes v */
                else
                    MBJS_FreeValue(ctx, v);
                MBJS_FreeCString(ctx, name);
            }
        }
        MBJS_FreePropertyEnum(ctx, tab, len);
    }
    MBJS_FreeValue(ctx, exports_obj);
    return 0;
}

static MBJSModuleDef *mbjs_module_load_cjs(MBJSContext *ctx,
                                           const char *module_name)
{
    MBJSValue global_obj, hook, args[1], ret, exports_obj;
    MBJSModuleDef *m;
    MBJSPropertyEnum *tab = NULL;
    uint32_t len = 0, i;
    const char *name;

    global_obj = MBJS_GetGlobalObject(ctx);
    hook = MBJS_GetPropertyStr(ctx, global_obj, "__MOCHI_CJS_LOAD__");
    MBJS_FreeValue(ctx, global_obj);
    if (MBJS_IsException(hook) || !MBJS_IsFunction(ctx, hook)) {
        MBJS_FreeValue(ctx, hook);
        return NULL;
    }
    args[0] = MBJS_NewString(ctx, module_name);
    ret = MBJS_Call(ctx, hook, MBJS_UNDEFINED, 1, args);
    MBJS_FreeValue(ctx, args[0]);
    MBJS_FreeValue(ctx, hook);
    if (MBJS_IsException(ret)) {
        if (getenv("MBJS_CJS_TRACE")) fprintf(stderr, "[mbjs-cjs-trace] hook threw for %s\n", module_name);
        MBJSValue exc = MBJS_GetException(ctx);
        MBJS_FreeValue(ctx, exc);
        MBJS_FreeValue(ctx, ret);
        return NULL;
    }
    if (!MBJS_IsObject(ret)) {
        if (getenv("MBJS_CJS_TRACE")) fprintf(stderr, "[mbjs-cjs-trace] hook returned non-object for %s\n", module_name);
        MBJS_FreeValue(ctx, ret);
        return NULL;
    }
    exports_obj = ret;

    /* Export entries are registered NOW (load time) so that
       mbjs_create_module_function creates their var_refs; the values are set
       later in cjs_module_init. */
    m = MBJS_NewCModule(ctx, module_name, cjs_module_init);
    if (!m) {
        MBJS_FreeValue(ctx, exports_obj);
        return NULL;
    }
    MBJS_AddModuleExport(ctx, m, "default");
    if (MBJS_GetOwnPropertyNames(ctx, &tab, &len, exports_obj,
                                 MBJS_GPN_ENUM_ONLY | MBJS_GPN_STRING_MASK) == 0) {
        for (i = 0; i < len; i++) {
            name = MBJS_AtomToCString(ctx, tab[i].atom);
            if (name) {
                /* 'default' is already registered above (module.exports). */
                if (strcmp(name, "default") != 0)
                    MBJS_AddModuleExport(ctx, m, name);
                MBJS_FreeCString(ctx, name);
            }
        }
        MBJS_FreePropertyEnum(ctx, tab, len);
    }
    MBJS_SetModulePrivateValue(ctx, m, exports_obj); /* consumes exports_obj */
    return m;
}

MBJSModuleDef *mbjs_module_load(MBJSContext *ctx, const char *module_name,
                            void *opaque, MBJSValueConst attributes,
                            MBJSLoadFileFunc *load_file)
{
    MBJSModuleDef *m;
    MBJSValue val;
    size_t buf_len;
    char *buf;
    int type;

    type = mbjs_module_import_type(ctx, attributes);
    if (type < 0)
        return NULL;
    /* the .json suffix only selects the JSON type when no type attribute
       was given, so that e.g. 'with { type: "text" }' is honored */
    if (type == MBJS_IMPORT_TYPE_JS)
        if (mbjs__has_suffix(module_name, ".json"))
            type = MBJS_IMPORT_TYPE_JSON;
    buf = (char *)load_file(ctx, &buf_len, module_name);
    if (!buf) {
        MBJS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                               module_name);
        return NULL;
    }
    switch (type) {
    case MBJS_IMPORT_TYPE_JS:
        /* CommonJS files are loaded through the JS-side hook (if installed). */
        if (getenv("MBJS_CJS_TRACE")) fprintf(stderr, "[mbjs-cjs-trace] %s len=%zu looks_cjs=%d\n", module_name, buf_len, mbjs_source_looks_cjs(buf, buf_len));
        if (mbjs_source_looks_cjs(buf, buf_len)) {
            m = mbjs_module_load_cjs(ctx, module_name);
            if (m) {
                mbjs_free(ctx, buf);
                return m;
            }
        }
        val = MBJS_Eval(ctx, buf, buf_len, module_name,
                      MBJS_EVAL_TYPE_MODULE | MBJS_EVAL_FLAG_COMPILE_ONLY);
        break;
    case MBJS_IMPORT_TYPE_JSON:
        val = MBJS_ParseJSON(ctx, buf, buf_len, module_name);
        break;
    case MBJS_IMPORT_TYPE_TEXT:
        val = MBJS_NewStringLen(ctx, buf, buf_len);
        break;
    case MBJS_IMPORT_TYPE_BYTES:
        val = MBJS_NewUint8Array(ctx, (uint8_t *)buf, buf_len,
                               mbjs_realloc_array_buffer, NULL,
                               /*is_shared*/false);
        if (!MBJS_IsException(val)) {
            MBJSValue abuf = MBJS_GetTypedArrayBuffer(ctx, val, NULL, NULL, NULL);
            MBJS_SetImmutableArrayBuffer(abuf, /*immutable*/true);
            MBJS_FreeValue(ctx, abuf);
            buf = NULL;
        }
        break;
    default:
        val = MBJS_ThrowInternalError(ctx, "unhandled import type");
        break;
    }
    mbjs_free(ctx, buf);
    if (MBJS_IsException(val))
        return NULL;
    if (type == MBJS_IMPORT_TYPE_JS) {
        if (mbjs_module_set_import_meta(ctx, val, true, false) < 0) {
            MBJS_FreeValue(ctx, val);
            return NULL;
        }
        // the module is already referenced, so we must free it
        m = MBJS_VALUE_GET_PTR(val);
        MBJS_FreeValue(ctx, val);
    } else {
        m = MBJS_NewCModule(ctx, module_name, default_module_init);
        if (!m) {
            MBJS_FreeValue(ctx, val);
            return NULL;
        }
        // only export the "default" symbol which will contain the string
        // or JSON object
        MBJS_AddModuleExport(ctx, m, "default");
        MBJS_SetModulePrivateValue(ctx, m, val);
    }
    return m;
}

typedef struct {
    const char *name;
    const uint8_t *src;
    size_t len;
} MBJSMochiBuiltin;

static MBJSMochiBuiltin mbjs_mochi_builtins[] = {
    { "mbjs:mochievents", mbjs_builtin_mochievents_src, 0 },
    { "mbjs:mochipath", mbjs_builtin_mochipath_src, 0 },
    { "mbjs:mochiutil", mbjs_builtin_mochiutil_src, 0 },
    { "mbjs:mochibuffer", mbjs_builtin_mochibuffer_src, 0 },
    { "mbjs:mochistream", mbjs_builtin_mochistream_src, 0 },
    { "mbjs:mochios", mbjs_builtin_mochios_src, 0 },
    { "mbjs:mochiurl", mbjs_builtin_mochiurl_src, 0 },
    { "mbjs:mochiasync_hooks", mbjs_builtin_mochiasync_hooks_src, 0 },
    { "mochievents", mbjs_builtin_mochievents_src, 0 },
    { "mochipath", mbjs_builtin_mochipath_src, 0 },
    { "mochiutil", mbjs_builtin_mochiutil_src, 0 },
    { "mochibuffer", mbjs_builtin_mochibuffer_src, 0 },
    { "mochistream", mbjs_builtin_mochistream_src, 0 },
    { "mochios", mbjs_builtin_mochios_src, 0 },
    { "mochiurl", mbjs_builtin_mochiurl_src, 0 },
    { "mochiasync_hooks", mbjs_builtin_mochiasync_hooks_src, 0 },
};

static int mbjs_mochi_builtins_init_done;

static void mbjs_mochi_builtins_init(void)
{
    if (mbjs_mochi_builtins_init_done)
        return;
    mbjs_mochi_builtins[0].len = mbjs_builtin_mochievents_len;
    mbjs_mochi_builtins[1].len = mbjs_builtin_mochipath_len;
    mbjs_mochi_builtins[2].len = mbjs_builtin_mochiutil_len;
    mbjs_mochi_builtins[3].len = mbjs_builtin_mochibuffer_len;
    mbjs_mochi_builtins[4].len = mbjs_builtin_mochistream_len;
    mbjs_mochi_builtins[5].len = mbjs_builtin_mochios_len;
    mbjs_mochi_builtins[6].len = mbjs_builtin_mochiurl_len;
    mbjs_mochi_builtins[7].len = mbjs_builtin_mochiasync_hooks_len;
    mbjs_mochi_builtins[8].len = mbjs_builtin_mochievents_len;
    mbjs_mochi_builtins[9].len = mbjs_builtin_mochipath_len;
    mbjs_mochi_builtins[10].len = mbjs_builtin_mochiutil_len;
    mbjs_mochi_builtins[11].len = mbjs_builtin_mochibuffer_len;
    mbjs_mochi_builtins[12].len = mbjs_builtin_mochistream_len;
    mbjs_mochi_builtins[13].len = mbjs_builtin_mochios_len;
    mbjs_mochi_builtins[14].len = mbjs_builtin_mochiurl_len;
    mbjs_mochi_builtins[15].len = mbjs_builtin_mochiasync_hooks_len;
    mbjs_mochi_builtins_init_done = 1;
}

static const MBJSMochiBuiltin *mbjs_find_mochi_builtin(const char *module_name)
{
    size_t i;
    mbjs_mochi_builtins_init();
    for (i = 0; i < sizeof(mbjs_mochi_builtins) / sizeof(mbjs_mochi_builtins[0]); i++) {
        if (!strcmp(module_name, mbjs_mochi_builtins[i].name))
            return &mbjs_mochi_builtins[i];
    }
    return NULL;
}

static MBJSModuleDef *mbjs_module_loader_mochi(MBJSContext *ctx, const char *module_name)
{
    const MBJSMochiBuiltin *b = mbjs_find_mochi_builtin(module_name);
    MBJSValue val;
    MBJSModuleDef *m;
    if (!b)
        return NULL;
    val = MBJS_Eval(ctx, (const char *)b->src, b->len, module_name,
                    MBJS_EVAL_TYPE_MODULE | MBJS_EVAL_FLAG_COMPILE_ONLY);
    if (MBJS_IsException(val))
        return NULL;
    if (mbjs_module_set_import_meta(ctx, val, false, false) < 0) {
        MBJS_FreeValue(ctx, val);
        return NULL;
    }
    m = MBJS_VALUE_GET_PTR(val);
    MBJS_FreeValue(ctx, val);
    return m;
}

/* --- bare specifier resolution (node_modules walk) ------------------------ */
/* The engine's ESM loader resolves only mochi builtins, native `.dll`s and
   file paths. To host npm-style ESM packages (e.g. Baileys) without touching
   the module graph, bare specifiers are resolved against node_modules by
   walking up from the current working directory. Node core modules ('crypto',
   'fs', ...) are aliased to their mochi shim package ('mochi<name>') first. */

static int mbjs_file_is_regular(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & _S_IFREG);
}

static int mbjs_name_is_path(const char *name)
{
    if (name[0] == '/' || name[0] == '\\' || name[0] == '.')
        return 1;
    if (name[0] && name[1] == ':' && (name[2] == '/' || name[2] == '\\'))
        return 1;  /* drive letter path, e.g. 'E:/...' */
    return 0;
}

static char *mbjs_dup_str(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = malloc(len);
    if (p)
        memcpy(p, s, len);
    return p;
}

/* Return the first string value of JSON field |field| in |path|, or NULL.
   Hand-rolled scanner (no JSON dependency); only "main"/"module" are needed. */
static char *mbjs_pkg_json_field(const char *path, const char *field)
{
    FILE *f = fopen(path, "rb");
    char buf[8192];
    size_t n;
    char key[32];
    const char *p, *end;
    if (!f)
        return NULL;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    snprintf(key, sizeof(key), "\"%s\"", field);
    p = strstr(buf, key);
    if (!p)
        return NULL;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p != ':')
        return NULL;  /* that occurrence was a value, not a key */
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p != '"')
        return NULL;
    p++;
    end = strchr(p, '"');
    if (!end)
        return NULL;
    {
        size_t vlen = (size_t)(end - p);
        char *out = malloc(vlen + 1);
        if (!out)
            return NULL;
        memcpy(out, p, vlen);
        out[vlen] = '\0';
        return out;
    }
}

/* Return the package's "exports" default entry: either the string form
   ("exports": "./index.js") or the object form's "default"/"." key. Returns a
   malloc'd relative path or NULL. */
static char *mbjs_pkg_json_exports_default(const char *path)
{
    FILE *f = fopen(path, "rb");
    char buf[8192];
    size_t n, len;
    const char *p, *q, *end;
    char *out;
    int k;
    static const char *const obj_keys[2] = { "\"default\"", "\".\"" };

    if (!f)
        return NULL;
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    p = strstr(buf, "\"exports\"");
    if (!p)
        return NULL;
    p += strlen("\"exports\"");
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p != ':')
        return NULL;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    if (*p == '"') {
        p++;
        end = strchr(p, '"');
        if (!end)
            return NULL;
        len = (size_t)(end - p);
        out = malloc(len + 1);
        if (!out)
            return NULL;
        memcpy(out, p, len);
        out[len] = '\0';
        return out;
    }
    if (*p == '{') {
        for (k = 0; k < 2; k++) {
            q = strstr(p, obj_keys[k]);
            if (!q)
                continue;
            q += strlen(obj_keys[k]);
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
                q++;
            if (*q != ':')
                continue;
            q++;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
                q++;
            if (*q != '"')
                continue;
            q++;
            end = strchr(q, '"');
            if (!end)
                continue;
            len = (size_t)(end - q);
            out = malloc(len + 1);
            if (!out)
                return NULL;
            memcpy(out, q, len);
            out[len] = '\0';
            return out;
        }
        /* Fallback: nested condition maps, e.g.
           {".": {"import": "./dist/index.js"}}. Take the first string value
           inside the exports object that points at a .js/.mjs file. */
        {
            const char *s = p;
            while ((s = strstr(s, "\"./")) != NULL) {
                const char *v = s + 3;
                const char *e = strchr(v, '"');
                if (e && (size_t)(e - v) > 3) {
                    size_t vl = (size_t)(e - v);
                    if (!memcmp(e - 3, ".js", 3) || !memcmp(e - 4, ".mjs", 4)) {
                        out = malloc(vl + 3);
                        if (out) {
                            out[0] = '.';
                            out[1] = '/';
                            memcpy(out + 2, v, vl);
                            out[vl + 2] = '\0';
                            return out;
                        }
                    }
                }
                s += 3;
            }
        }
    }
    return NULL;
}

/* Try to resolve <nmroot>/node_modules/<pkgdir> to a loadable file.
   If |sub| is non-NULL it is a subpath within the package (a file);
   otherwise resolve the package entry point (main/module/index). */
static int mbjs_try_package(const char *nmroot, const char *pkgdir,
                            const char *sub, char *out, size_t outsz)
{
    char base[8192];
    int len;
    len = snprintf(base, sizeof(base), "%s/node_modules/%s", nmroot, pkgdir);
    if (len < 0 || len >= (int)sizeof(base))
        return 0;
    if (sub) {
        /* subpath within the package: exact file, then .js/.mjs, then a
           directory with an index.js (Node's CJS-style probing) */
        len = snprintf(out, outsz, "%s/%s", base, sub);
        if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out))
            return 1;
        len = snprintf(out, outsz, "%s/%s.js", base, sub);
        if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out))
            return 1;
        len = snprintf(out, outsz, "%s/%s.mjs", base, sub);
        if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out))
            return 1;
        len = snprintf(out, outsz, "%s/%s/index.js", base, sub);
        if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out))
            return 1;
        return 0;
    }
    len = snprintf(out, outsz, "%s/index.js", base);
    if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out))
        return 1;
    len = snprintf(out, outsz, "%s/index.mjs", base);
    if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out))
        return 1;
    {
        char pj[8192];
        char *mainf;
        snprintf(pj, sizeof(pj), "%s/package.json", base);
        mainf = mbjs_pkg_json_field(pj, "main");
        if (!mainf)
            mainf = mbjs_pkg_json_field(pj, "module");
        if (!mainf)
            mainf = mbjs_pkg_json_exports_default(pj);
        if (mainf) {
            const char *mf = mainf;
            while (mf[0] == '.' && mf[1] == '/')
                mf += 2;
            len = snprintf(out, outsz, "%s/%s", base, mf);
            if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out)) {
                free(mainf);
                return 1;
            }
            /* Node resolves an extension-less "main" the same way. */
            len = snprintf(out, outsz, "%s/%s.js", base, mf);
            if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out)) {
                free(mainf);
                return 1;
            }
            len = snprintf(out, outsz, "%s/%s/index.js", base, mf);
            if (len >= 0 && len < (int)outsz && mbjs_file_is_regular(out)) {
                free(mainf);
                return 1;
            }
            free(mainf);
        }
    }
    return 0;
}

/* Resolve a bare specifier to an absolute file path (malloc'd) or NULL. */
static char *mbjs_resolve_bare_specifier(const char *name)
{
    static const char *const core_mods[] = {
        "crypto", "fs", "path", "buffer", "events", "util", "stream", "os",
        "url", "net", "http", "https", "tls", "zlib", "assert", "tty",
        "worker_threads", "child_process", "perf_hooks", "async_hooks",
        "diagnostics_channel", "module", "process", "querystring",
        "string_decoder", "punycode", "dns", "dgram", "v8", "vm", "readline",
        "timers", "console", NULL
    };
    char cwd[4096];
    char dir[512];
    char out[8192];
    const char *sub = NULL;
    const char *slash;
    size_t plen;
    int is_core = 0, i;

    if (mbjs_name_is_path(name))
        return NULL;
    slash = strchr(name, '/');
    if (slash && name[0] == '@') {
        /* Scoped package: '@scope/name[/subpath]' */
        const char *slash2 = strchr(slash + 1, '/');
        if (slash2) {
            plen = (size_t)(slash2 - name);
            sub = slash2 + 1;
        } else {
            plen = strlen(name);
        }
    } else if (slash) {
        plen = (size_t)(slash - name);
        sub = slash + 1;
    } else {
        plen = strlen(name);
    }
    if (plen == 0 || plen >= sizeof(dir))
        return NULL;
    memcpy(dir, name, plen);
    dir[plen] = '\0';

    for (i = 0; core_mods[i]; i++) {
        if (strcmp(core_mods[i], dir) == 0) {
            is_core = 1;
            break;
        }
    }

    /* MochiBerry package overrides: the Node implementation of these
       packages needs a native network stack that the engine does not have,
       so they are served by their mochi* counterpart instead. */
    {
        static const char *const overrides[][2] = {
            { "ws", "mochiws" },
            { "node:ws", "mochiws" },
            { NULL, NULL }
        };
        for (i = 0; overrides[i][0]; i++) {
            if (strcmp(overrides[i][0], dir) == 0) {
                snprintf(dir, sizeof(dir), "%s", overrides[i][1]);
                is_core = 0;
                break;
            }
        }
    }

    if (!getcwd(cwd, sizeof(cwd)))
        return NULL;
    for (;;) {
        if (is_core) {
            char pkg[512];
            snprintf(pkg, sizeof(pkg), "mochi%s", dir);
            if (mbjs_try_package(cwd, pkg, sub, out, sizeof(out)))
                return mbjs_dup_str(out);
        }
        if (mbjs_try_package(cwd, dir, sub, out, sizeof(out)))
            return mbjs_dup_str(out);
        {
            char *ls = strrchr(cwd, '/');
            if (!ls)
                break;
            *ls = '\0';
            if (strlen(cwd) <= 2)
                break;
        }
    }
    return NULL;
}

MBJSModuleDef *mbjs_module_loader(MBJSContext *ctx, const char *module_name,
                              void *opaque, MBJSValueConst attributes)
{
    MBJSModuleDef *m;
    m = mbjs_module_loader_mochi(ctx, module_name);
    if (m)
        return m;
    if (mbjs__has_suffix(module_name, MBJS_NATIVE_MODULE_SUFFIX))
        return mbjs_module_loader_so(ctx, module_name);
    /* Bare specifiers are not file paths; try node_modules before giving up. */
    if (!mbjs_name_is_path(module_name)) {
        char *resolved = mbjs_resolve_bare_specifier(module_name);
        if (resolved) {
            m = mbjs_module_load(ctx, resolved, opaque, attributes,
                                 mbjs_load_file);
            free(resolved);
            return m;
        }
    }
    return mbjs_module_load(ctx, module_name, opaque, attributes, mbjs_load_file);
}

static MBJSValue mbjs_std_exit(MBJSContext *ctx, MBJSValueConst this_val,
                           int argc, MBJSValueConst *argv)
{
    int status;
    if (MBJS_ToInt32(ctx, &status, argv[0]))
        status = -1;
    exit(status);
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_std_getenv(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    const char *name, *str;
    name = MBJS_ToCString(ctx, argv[0]);
    if (!name)
        return MBJS_EXCEPTION;
    str = getenv(name);
    MBJS_FreeCString(ctx, name);
    if (!str)
        return MBJS_UNDEFINED;
    else
        return MBJS_NewString(ctx, str);
}

#if defined(_WIN32)
static void setenv(const char *name, const char *value, int overwrite)
{
    char *str;
    size_t name_len, value_len;
    name_len = strlen(name);
    value_len = strlen(value);
    str = malloc(name_len + 1 + value_len + 1);
    memcpy(str, name, name_len);
    str[name_len] = '=';
    memcpy(str + name_len + 1, value, value_len);
    str[name_len + 1 + value_len] = '\0';
    _putenv(str);
    free(str);
}

static void unsetenv(const char *name)
{
    setenv(name, "", true);
}
#endif /* _WIN32 */

static MBJSValue mbjs_std_setenv(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    const char *name, *value;
    name = MBJS_ToCString(ctx, argv[0]);
    if (!name)
        return MBJS_EXCEPTION;
    value = MBJS_ToCString(ctx, argv[1]);
    if (!value) {
        MBJS_FreeCString(ctx, name);
        return MBJS_EXCEPTION;
    }
    setenv(name, value, true);
    MBJS_FreeCString(ctx, name);
    MBJS_FreeCString(ctx, value);
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_std_unsetenv(MBJSContext *ctx, MBJSValueConst this_val,
                               int argc, MBJSValueConst *argv)
{
    const char *name;
    name = MBJS_ToCString(ctx, argv[0]);
    if (!name)
        return MBJS_EXCEPTION;
    unsetenv(name);
    MBJS_FreeCString(ctx, name);
    return MBJS_UNDEFINED;
}

/* return an object containing the list of the available environment
   variables. */
static MBJSValue mbjs_std_getenviron(MBJSContext *ctx, MBJSValueConst this_val,
                                 int argc, MBJSValueConst *argv)
{
    char **envp;
    const char *name, *p, *value;
    MBJSValue obj;
    uint32_t idx;
    size_t name_len;
    MBJSAtom atom;
    int ret;

    obj = MBJS_NewObject(ctx);
    if (MBJS_IsException(obj))
        return MBJS_EXCEPTION;
    envp = environ;
    for(idx = 0; envp[idx] != NULL; idx++) {
        name = envp[idx];
        p = strchr(name, '=');
        name_len = p - name;
        if (!p)
            continue;
        value = p + 1;
        atom = MBJS_NewAtomLen(ctx, name, name_len);
        if (atom == MBJS_ATOM_NULL)
            goto fail;
        ret = MBJS_DefinePropertyValue(ctx, obj, atom, MBJS_NewString(ctx, value),
                                     MBJS_PROP_C_W_E);
        MBJS_FreeAtom(ctx, atom);
        if (ret < 0)
            goto fail;
    }
    return obj;
 fail:
    MBJS_FreeValue(ctx, obj);
    return MBJS_EXCEPTION;
}

static MBJSValue mbjs_std_gc(MBJSContext *ctx, MBJSValueConst this_val,
                         int argc, MBJSValueConst *argv)
{
    MBJS_RunGC(MBJS_GetRuntime(ctx));
    return MBJS_UNDEFINED;
}

static int interrupt_handler(MBJSContext *ctx, void *opaque)
{
    return (os_pending_signals >> SIGINT) & 1;
}

static MBJSValue mbjs_evalScript(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    const char *str = NULL;
    size_t len;
    MBJSValue ret, obj;
    MBJSValueConst options_obj, arg;
    bool backtrace_barrier = false;
    bool eval_function = false;
    bool eval_module = false;
    bool compile_only = false;
    bool compile_module = false;
    bool is_async = false;
    int flags;

    if (argc >= 2) {
        options_obj = argv[1];
        if (get_bool_option(ctx, &backtrace_barrier, options_obj,
                            "backtrace_barrier"))
            return MBJS_EXCEPTION;
        if (get_bool_option(ctx, &eval_function, options_obj,
                            "eval_function"))
            return MBJS_EXCEPTION;
        if (get_bool_option(ctx, &eval_module, options_obj,
                            "eval_module"))
            return MBJS_EXCEPTION;
        if (get_bool_option(ctx, &compile_only, options_obj,
                            "compile_only"))
            return MBJS_EXCEPTION;
        if (get_bool_option(ctx, &compile_module, options_obj,
                            "compile_module"))
            return MBJS_EXCEPTION;
        if (get_bool_option(ctx, &is_async, options_obj,
                            "async"))
            return MBJS_EXCEPTION;
    }

    if (eval_module) {
        arg = argv[0];
        if (MBJS_VALUE_GET_TAG(arg) != MBJS_TAG_MODULE)
            return MBJS_ThrowTypeError(ctx, "not a module");

        if (MBJS_ResolveModule(ctx, arg) < 0)
            return MBJS_EXCEPTION;

        if (mbjs_module_set_import_meta(ctx, arg, false, false) < 0)
            return MBJS_EXCEPTION;

        return MBJS_EvalFunction(ctx, MBJS_DupValue(ctx, arg));
    }

    if (!eval_function) {
        str = MBJS_ToCStringLen(ctx, &len, argv[0]);
        if (!str)
            return MBJS_EXCEPTION;
    }
    if (!ts->recv_pipe && ++ts->eval_script_recurse == 1) {
        /* install the interrupt handler */
        MBJS_SetInterruptHandler(MBJS_GetRuntime(ctx), interrupt_handler, NULL);
    }
    flags = compile_module ? MBJS_EVAL_TYPE_MODULE : MBJS_EVAL_TYPE_GLOBAL;
    if (backtrace_barrier)
        flags |= MBJS_EVAL_FLAG_BACKTRACE_BARRIER;
    if (compile_only)
        flags |= MBJS_EVAL_FLAG_COMPILE_ONLY;
    if (is_async)
        flags |= MBJS_EVAL_FLAG_ASYNC;
    if (eval_function) {
        obj = MBJS_DupValue(ctx, argv[0]);
        ret = MBJS_EvalFunction(ctx, obj); // takes ownership of |obj|
    } else {
        ret = MBJS_Eval(ctx, str, len, "<evalScript>", flags);
    }
    MBJS_FreeCString(ctx, str);
    if (!ts->recv_pipe && --ts->eval_script_recurse == 0) {
        /* remove the interrupt handler */
        MBJS_SetInterruptHandler(MBJS_GetRuntime(ctx), NULL, NULL);
        os_pending_signals &= ~((uint64_t)1 << SIGINT);
        /* convert the uncatchable "interrupted" error into a normal error
           so that it can be caught by the REPL */
        if (MBJS_IsException(ret))
            MBJS_ResetUncatchableError(ctx);
    }
    return ret;
}

typedef struct {
    FILE *f;
    bool is_popen;
} MBJSSTDFile;

static bool is_stdio(FILE *f)
{
    return f == stdin || f == stdout || f == stderr;
}

static void safe_close(FILE *f, bool is_popen)
{
    if (!f)
        return;
    if (is_stdio(f))
        return;
    if (is_popen) {
#if !defined(__wasi__)
        pclose(f);
#endif
    } else {
        fclose(f);
    }
}

static void mbjs_std_file_finalizer(MBJSRuntime *rt, MBJSValueConst val)
{
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSSTDFile *s = MBJS_GetOpaque(val, ts->std_file_class_id);
    if (s) {
        safe_close(s->f, s->is_popen);
        mbjs_free_rt(rt, s);
    }
}

static ssize_t mbjs_get_errno(ssize_t ret)
{
    if (ret == -1)
        ret = -errno;
    return ret;
}

static MBJSValue mbjs_std_strerror(MBJSContext *ctx, MBJSValueConst this_val,
                               int argc, MBJSValueConst *argv)
{
    int err;
    if (MBJS_ToInt32(ctx, &err, argv[0]))
        return MBJS_EXCEPTION;
    return MBJS_NewString(ctx, strerror(err));
}

static MBJSValue mbjs_new_std_file(MBJSContext *ctx, FILE *f, bool is_popen)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSSTDFile *s;
    MBJSValue obj;
    obj = MBJS_NewObjectClass(ctx, ts->std_file_class_id);
    if (MBJS_IsException(obj))
        goto exception;
    s = mbjs_mallocz(ctx, sizeof(*s));
    if (!s)
        goto exception;
    s->is_popen = is_popen;
    s->f = f;
    MBJS_SetOpaque(obj, s);
    return obj;
exception:
    safe_close(f, is_popen);
    MBJS_FreeValue(ctx, obj);
    return MBJS_EXCEPTION;
}

static void mbjs_set_error_object(MBJSContext *ctx, MBJSValueConst obj, int err)
{
    if (!MBJS_IsUndefined(obj)) {
        MBJS_SetPropertyStr(ctx, obj, "errno", MBJS_NewInt32(ctx, err));
    }
}

static MBJSValue mbjs_std_open(MBJSContext *ctx, MBJSValueConst this_val,
                           int argc, MBJSValueConst *argv)
{
    const char *filename, *mode = NULL;
    FILE *f;
    int err;

    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        goto fail;
    mode = MBJS_ToCString(ctx, argv[1]);
    if (!mode)
        goto fail;
    if (mode[strspn(mode, "rwa+bx")] != '\0') {
        MBJS_ThrowTypeError(ctx, "invalid file mode");
        goto fail;
    }

    f = fopen(filename, mode);
    if (!f)
        err = errno;
    else
        err = 0;
    if (argc >= 3)
        mbjs_set_error_object(ctx, argv[2], err);
    MBJS_FreeCString(ctx, filename);
    MBJS_FreeCString(ctx, mode);
    if (!f)
        return MBJS_NULL;
    return mbjs_new_std_file(ctx, f, false);
 fail:
    MBJS_FreeCString(ctx, filename);
    MBJS_FreeCString(ctx, mode);
    return MBJS_EXCEPTION;
}

#if !defined(__wasi__)
static MBJSValue mbjs_std_popen(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    const char *filename, *mode = NULL;
    FILE *f;
    int err;

    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        goto fail;
    mode = MBJS_ToCString(ctx, argv[1]);
    if (!mode)
        goto fail;
    if (strcmp(mode, "r") && strcmp(mode, "w")) {
        MBJS_ThrowTypeError(ctx, "invalid file mode");
        goto fail;
    }

    f = popen(filename, mode);
    if (!f)
        err = errno;
    else
        err = 0;
    if (argc >= 3)
        mbjs_set_error_object(ctx, argv[2], err);
    MBJS_FreeCString(ctx, filename);
    MBJS_FreeCString(ctx, mode);
    if (!f)
        return MBJS_NULL;
    return mbjs_new_std_file(ctx, f, true);
 fail:
    MBJS_FreeCString(ctx, filename);
    MBJS_FreeCString(ctx, mode);
    return MBJS_EXCEPTION;
}
#endif // !defined(__wasi__)

static MBJSValue mbjs_std_fdopen(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    const char *mode;
    FILE *f;
    int fd, err;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    mode = MBJS_ToCString(ctx, argv[1]);
    if (!mode)
        goto fail;
    if (mode[strspn(mode, "rwa+")] != '\0') {
        MBJS_ThrowTypeError(ctx, "invalid file mode");
        goto fail;
    }

    f = fdopen(fd, mode);
    if (!f)
        err = errno;
    else
        err = 0;
    if (argc >= 3)
        mbjs_set_error_object(ctx, argv[2], err);
    MBJS_FreeCString(ctx, mode);
    if (!f)
        return MBJS_NULL;
    return mbjs_new_std_file(ctx, f, false);
 fail:
    MBJS_FreeCString(ctx, mode);
    return MBJS_EXCEPTION;
}

#if !defined(__wasi__)
static MBJSValue mbjs_std_tmpfile(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    FILE *f;
    f = tmpfile();
    if (argc >= 1)
        mbjs_set_error_object(ctx, argv[0], f ? 0 : errno);
    if (!f)
        return MBJS_NULL;
    return mbjs_new_std_file(ctx, f, false);
}
#endif

static MBJSValue mbjs_std_sprintf(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    return mbjs_printf_internal(ctx, argc, argv, NULL);
}

static MBJSValue mbjs_std_printf(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    return mbjs_printf_internal(ctx, argc, argv, stdout);
}

static FILE *mbjs_std_file_get(MBJSContext *ctx, MBJSValueConst obj)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSSTDFile *s = MBJS_GetOpaque2(ctx, obj, ts->std_file_class_id);
    if (!s)
        return NULL;
    if (!s->f) {
        MBJS_ThrowTypeError(ctx, "invalid file handle");
        return NULL;
    }
    return s->f;
}

static MBJSValue mbjs_std_file_puts(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv, int magic)
{
    FILE *f;
    int i;
    const char *str;
    size_t len;

    if (magic == 0) {
        f = stdout;
    } else {
        f = mbjs_std_file_get(ctx, this_val);
        if (!f)
            return MBJS_EXCEPTION;
    }

    for(i = 0; i < argc; i++) {
        str = MBJS_ToCStringLen(ctx, &len, argv[i]);
        if (!str)
            return MBJS_EXCEPTION;
        fwrite(str, 1, len, f);
        MBJS_FreeCString(ctx, str);
    }
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_std_file_close(MBJSContext *ctx, MBJSValueConst this_val,
                                 int argc, MBJSValueConst *argv)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSSTDFile *s = MBJS_GetOpaque2(ctx, this_val, ts->std_file_class_id);
    int err;
    if (!s)
        return MBJS_EXCEPTION;
    if (!s->f)
        return MBJS_ThrowTypeError(ctx, "invalid file handle");
    if (is_stdio(s->f))
        return MBJS_ThrowTypeError(ctx, "cannot close stdio");
#if !defined(__wasi__)
    if (s->is_popen)
        err = mbjs_get_errno(pclose(s->f));
    else
#endif
        err = mbjs_get_errno(fclose(s->f));
    s->f = NULL;
    return MBJS_NewInt32(ctx, err);
}

static MBJSValue mbjs_std_file_printf(MBJSContext *ctx, MBJSValueConst this_val,
                                  int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    if (!f)
        return MBJS_EXCEPTION;
    return mbjs_printf_internal(ctx, argc, argv, f);
}

static MBJSValue mbjs_std_file_flush(MBJSContext *ctx, MBJSValueConst this_val,
                                 int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    if (!f)
        return MBJS_EXCEPTION;
    fflush(f);
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_std_file_tell(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv, int is_bigint)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    int64_t pos;
    if (!f)
        return MBJS_EXCEPTION;
#if defined(__linux__) || defined(__GLIBC__)
    pos = ftello(f);
#else
    pos = ftell(f);
#endif
    if (is_bigint)
        return MBJS_NewBigInt64(ctx, pos);
    else
        return MBJS_NewInt64(ctx, pos);
}

static MBJSValue mbjs_std_file_seek(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    int64_t pos;
    int whence, ret;
    if (!f)
        return MBJS_EXCEPTION;
    if (MBJS_ToInt64Ext(ctx, &pos, argv[0]))
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &whence, argv[1]))
        return MBJS_EXCEPTION;
#if defined(__linux__) || defined(__GLIBC__)
    ret = fseeko(f, pos, whence);
#else
    ret = fseek(f, pos, whence);
#endif
    if (ret < 0)
        ret = -errno;
    return MBJS_NewInt32(ctx, ret);
}

static MBJSValue mbjs_std_file_eof(MBJSContext *ctx, MBJSValueConst this_val,
                               int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    if (!f)
        return MBJS_EXCEPTION;
    return MBJS_NewBool(ctx, feof(f));
}

static MBJSValue mbjs_std_file_error(MBJSContext *ctx, MBJSValueConst this_val,
                                 int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    if (!f)
        return MBJS_EXCEPTION;
    return MBJS_NewBool(ctx, ferror(f));
}

static MBJSValue mbjs_std_file_clearerr(MBJSContext *ctx, MBJSValueConst this_val,
                                    int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    if (!f)
        return MBJS_EXCEPTION;
    clearerr(f);
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_std_file_fileno(MBJSContext *ctx, MBJSValueConst this_val,
                                  int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    if (!f)
        return MBJS_EXCEPTION;
    return MBJS_NewInt32(ctx, fileno(f));
}

static MBJSValue mbjs_std_file_read_write(MBJSContext *ctx, MBJSValueConst this_val,
                                      int argc, MBJSValueConst *argv, int magic)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    bool is_write = (magic != 0);
    uint64_t pos, len;
    size_t size, ret;
    const char *str;
    uint8_t *buf;

    if (!f)
        return MBJS_EXCEPTION;
    pos = 0;
    if (argc > 1 && MBJS_ToIndex(ctx, &pos, argv[1]))
        return MBJS_EXCEPTION;
    len = 0;
    if (argc > 2 && MBJS_ToIndex(ctx, &len, argv[2]))
        return MBJS_EXCEPTION;
    if (is_write && MBJS_IsString(argv[0])) {
        str = MBJS_ToCStringLen(ctx, &size, argv[0]);
        buf = (void *)str;
    } else {
        str = NULL;
        buf = MBJS_GetArrayBuffer(ctx, &size, argv[0]);
    }
    if (!buf)
        return MBJS_EXCEPTION;
    if (pos > size)
        pos = size;
    if (argc < 3)
        len = size - pos;
    if (pos + len > size)
        len = size - pos;
    if (is_write) {
        ret = fwrite(buf + pos, 1, len, f);
    } else {
        ret = fread(buf + pos, 1, len, f);
    }
    MBJS_FreeCString(ctx, str);
    return MBJS_NewInt64(ctx, ret);
}

/* XXX: could use less memory and go faster */
static MBJSValue mbjs_std_file_getline(MBJSContext *ctx, MBJSValueConst this_val,
                                   int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    int c;
    DynBuf dbuf;
    MBJSValue obj;

    if (!f)
        return MBJS_EXCEPTION;

    mbjs_std_dbuf_init(ctx, &dbuf);
    for(;;) {
        c = fgetc(f);
        if (c == EOF) {
            if (dbuf.size == 0) {
                /* EOF */
                dbuf_free(&dbuf);
                return MBJS_NULL;
            } else {
                break;
            }
        }
        if (c == '\n')
            break;
        if (dbuf_putc(&dbuf, c)) {
            dbuf_free(&dbuf);
            return MBJS_ThrowOutOfMemory(ctx);
        }
    }
    obj = MBJS_NewStringLen(ctx, (const char *)dbuf.buf, dbuf.size);
    dbuf_free(&dbuf);
    return obj;
}

/* XXX: could use less memory and go faster */
static MBJSValue mbjs_std_file_readAs(MBJSContext *ctx, MBJSValueConst this_val,
                                  int argc, MBJSValueConst *argv, int magic)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    int c;
    DynBuf dbuf;
    MBJSValue obj;
    uint64_t max_size64;
    size_t max_size;
    MBJSValueConst max_size_val;

    if (!f)
        return MBJS_EXCEPTION;

    if (argc >= 1)
        max_size_val = argv[0];
    else
        max_size_val = MBJS_UNDEFINED;
    max_size = (size_t)-1;
    if (!MBJS_IsUndefined(max_size_val)) {
        if (MBJS_ToIndex(ctx, &max_size64, max_size_val))
            return MBJS_EXCEPTION;
        if (max_size64 < max_size)
            max_size = max_size64;
    }

    mbjs_std_dbuf_init(ctx, &dbuf);
    while (max_size != 0) {
        c = fgetc(f);
        if (c == EOF)
            break;
        if (dbuf_putc(&dbuf, c)) {
            dbuf_free(&dbuf);
            return MBJS_EXCEPTION;
        }
        max_size--;
    }
    if (magic) {
        obj = MBJS_NewStringLen(ctx, (const char *)dbuf.buf, dbuf.size);
    } else {
        obj = MBJS_NewArrayBufferCopy(ctx, dbuf.buf, dbuf.size);
    }
    dbuf_free(&dbuf);
    return obj;
}

static MBJSValue mbjs_std_file_getByte(MBJSContext *ctx, MBJSValueConst this_val,
                                   int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    if (!f)
        return MBJS_EXCEPTION;
    return MBJS_NewInt32(ctx, fgetc(f));
}

static MBJSValue mbjs_std_file_putByte(MBJSContext *ctx, MBJSValueConst this_val,
                                   int argc, MBJSValueConst *argv)
{
    FILE *f = mbjs_std_file_get(ctx, this_val);
    int c;
    if (!f)
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &c, argv[0]))
        return MBJS_EXCEPTION;
    c = fputc(c, f);
    return MBJS_NewInt32(ctx, c);
}

/* urlGet */
#if !defined(__wasi__)

#define URL_GET_PROGRAM "curl -s -i --"
#define URL_GET_BUF_SIZE 4096

static int http_get_header_line(FILE *f, char *buf, size_t buf_size,
                                DynBuf *dbuf)
{
    int c;
    char *p;

    p = buf;
    for(;;) {
        c = fgetc(f);
        if (c < 0)
            return -1;
        if ((p - buf) < buf_size - 1)
            *p++ = c;
        if (dbuf)
            dbuf_putc(dbuf, c);
        if (c == '\n')
            break;
    }
    *p = '\0';
    return 0;
}

static int http_get_status(const char *buf)
{
    const char *p = buf;
    while (*p != ' ' && *p != '\0')
        p++;
    if (*p != ' ')
        return 0;
    while (*p == ' ')
        p++;
    return atoi(p);
}

static MBJSValue mbjs_std_urlGet(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    const char *url;
    DynBuf cmd_buf;
    DynBuf data_buf_s, *data_buf = &data_buf_s;
    DynBuf header_buf_s, *header_buf = &header_buf_s;
    char *buf;
    size_t i, len;
    int status;
    MBJSValue response = MBJS_UNDEFINED, ret_obj;
    MBJSValueConst options_obj;
    FILE *f;
    bool binary_flag, full_flag;

    url = MBJS_ToCString(ctx, argv[0]);
    if (!url)
        return MBJS_EXCEPTION;

    binary_flag = false;
    full_flag = false;

    if (argc >= 2) {
        options_obj = argv[1];

        if (get_bool_option(ctx, &binary_flag, options_obj, "binary"))
            goto fail_obj;

        if (get_bool_option(ctx, &full_flag, options_obj, "full")) {
        fail_obj:
            MBJS_FreeCString(ctx, url);
            return MBJS_EXCEPTION;
        }
    }

    mbjs_std_dbuf_init(ctx, &cmd_buf);
    dbuf_printf(&cmd_buf, "%s '", URL_GET_PROGRAM);
    for(i = 0; url[i] != '\0'; i++) {
        unsigned char c = url[i];
        switch (c) {
        case '\'':
            /* shell single quoted string does not support \' */
            dbuf_putstr(&cmd_buf, "'\\''");
            break;
        case '[': case ']': case '{': case '}': case '\\':
            /* prevent interpretation by curl as range or set specification */
            dbuf_putc(&cmd_buf, '\\');
            /* FALLTHROUGH */
        default:
            dbuf_putc(&cmd_buf, c);
            break;
        }
    }
    MBJS_FreeCString(ctx, url);
    dbuf_putstr(&cmd_buf, "'");
    dbuf_putc(&cmd_buf, '\0');
    if (dbuf_error(&cmd_buf)) {
        dbuf_free(&cmd_buf);
        return MBJS_EXCEPTION;
    }
    //    printf("%s\n", (char *)cmd_buf.buf);
    f = popen((char *)cmd_buf.buf, "r");
    dbuf_free(&cmd_buf);
    if (!f) {
        return MBJS_ThrowTypeError(ctx, "could not start curl");
    }

    mbjs_std_dbuf_init(ctx, data_buf);
    mbjs_std_dbuf_init(ctx, header_buf);

    buf = mbjs_malloc(ctx, URL_GET_BUF_SIZE);
    if (!buf)
        goto fail;

    /* get the HTTP status */
    if (http_get_header_line(f, buf, URL_GET_BUF_SIZE, NULL) < 0) {
        status = 0;
        goto bad_header;
    }
    status = http_get_status(buf);
    if (!full_flag && !(status >= 200 && status <= 299)) {
        goto bad_header;
    }

    /* wait until there is an empty line */
    for(;;) {
        if (http_get_header_line(f, buf, URL_GET_BUF_SIZE, header_buf) < 0) {
        bad_header:
            response = MBJS_NULL;
            goto done;
        }
        if (!strcmp(buf, "\r\n"))
            break;
    }
    if (dbuf_error(header_buf))
        goto fail;
    header_buf->size -= 2; /* remove the trailing CRLF */

    /* download the data */
    for(;;) {
        len = fread(buf, 1, URL_GET_BUF_SIZE, f);
        if (len == 0)
            break;
        dbuf_put(data_buf, (uint8_t *)buf, len);
    }
    if (dbuf_error(data_buf))
        goto fail;
    if (binary_flag) {
        response = MBJS_NewArrayBufferCopy(ctx,
                                         data_buf->buf, data_buf->size);
    } else {
        response = MBJS_NewStringLen(ctx, (char *)data_buf->buf, data_buf->size);
    }
    if (MBJS_IsException(response))
        goto fail;
 done:
    mbjs_free(ctx, buf);
    buf = NULL;
    pclose(f);
    f = NULL;
    dbuf_free(data_buf);
    data_buf = NULL;

    if (full_flag) {
        ret_obj = MBJS_NewObject(ctx);
        if (MBJS_IsException(ret_obj))
            goto fail;
        MBJS_DefinePropertyValueStr(ctx, ret_obj, "response",
                                  response,
                                  MBJS_PROP_C_W_E);
        if (!MBJS_IsNull(response)) {
            MBJS_DefinePropertyValueStr(ctx, ret_obj, "responseHeaders",
                                      MBJS_NewStringLen(ctx, (char *)header_buf->buf,
                                                      header_buf->size),
                                      MBJS_PROP_C_W_E);
            MBJS_DefinePropertyValueStr(ctx, ret_obj, "status",
                                      MBJS_NewInt32(ctx, status),
                                      MBJS_PROP_C_W_E);
        }
    } else {
        ret_obj = response;
    }
    dbuf_free(header_buf);
    return ret_obj;
 fail:
    if (f)
        pclose(f);
    mbjs_free(ctx, buf);
    if (data_buf)
        dbuf_free(data_buf);
    if (header_buf)
        dbuf_free(header_buf);
    MBJS_FreeValue(ctx, response);
    return MBJS_EXCEPTION;
}
#endif // !defined(__wasi__)

static MBJSClassDef mbjs_std_file_class = {
    "FILE",
    .finalizer = mbjs_std_file_finalizer,
};

static const MBJSCFunctionListEntry mbjs_std_error_props[] = {
    /* various errno values */
#define DEF(x) MBJS_PROP_INT32_DEF(#x, x, MBJS_PROP_CONFIGURABLE )
    DEF(EINVAL),
    DEF(EIO),
    DEF(EACCES),
    DEF(EEXIST),
    DEF(ENOSPC),
    DEF(ENOSYS),
    DEF(EBUSY),
    DEF(ENOENT),
    DEF(EPERM),
    DEF(EPIPE),
    DEF(EBADF),
#undef DEF
};

static const MBJSCFunctionListEntry mbjs_std_funcs[] = {
    MBJS_CFUNC_DEF("exit", 1, mbjs_std_exit ),
    MBJS_CFUNC_DEF("gc", 0, mbjs_std_gc ),
    MBJS_CFUNC_DEF("evalScript", 1, mbjs_evalScript ),
    MBJS_CFUNC_DEF("loadScript", 1, mbjs_loadScript ),
    MBJS_CFUNC_DEF("getenv", 1, mbjs_std_getenv ),
    MBJS_CFUNC_DEF("setenv", 1, mbjs_std_setenv ),
    MBJS_CFUNC_DEF("unsetenv", 1, mbjs_std_unsetenv ),
    MBJS_CFUNC_DEF("getenviron", 1, mbjs_std_getenviron ),
#if !defined(__wasi__)
    MBJS_CFUNC_DEF("urlGet", 1, mbjs_std_urlGet ),
#endif
    MBJS_CFUNC_DEF("loadFile", 1, mbjs_std_loadFile ),
    MBJS_CFUNC_DEF("writeFile", 2, mbjs_std_writeFile ),
    MBJS_CFUNC_DEF("strerror", 1, mbjs_std_strerror ),

    /* FILE I/O */
    MBJS_CFUNC_DEF("open", 2, mbjs_std_open ),
#if !defined(__wasi__)
    MBJS_CFUNC_DEF("popen", 2, mbjs_std_popen ),
    MBJS_CFUNC_DEF("tmpfile", 0, mbjs_std_tmpfile ),
#endif
    MBJS_CFUNC_DEF("fdopen", 2, mbjs_std_fdopen ),
    MBJS_CFUNC_MAGIC_DEF("puts", 1, mbjs_std_file_puts, 0 ),
    MBJS_CFUNC_DEF("printf", 1, mbjs_std_printf ),
    MBJS_CFUNC_DEF("sprintf", 1, mbjs_std_sprintf ),
    MBJS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, MBJS_PROP_CONFIGURABLE ),
    MBJS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, MBJS_PROP_CONFIGURABLE ),
    MBJS_PROP_INT32_DEF("SEEK_END", SEEK_END, MBJS_PROP_CONFIGURABLE ),
    MBJS_OBJECT_DEF("Error", mbjs_std_error_props, countof(mbjs_std_error_props), MBJS_PROP_CONFIGURABLE),
};

static const MBJSCFunctionListEntry mbjs_std_file_proto_funcs[] = {
    MBJS_CFUNC_DEF("close", 0, mbjs_std_file_close ),
    MBJS_CFUNC_MAGIC_DEF("puts", 1, mbjs_std_file_puts, 1 ),
    MBJS_CFUNC_DEF("printf", 1, mbjs_std_file_printf ),
    MBJS_CFUNC_DEF("flush", 0, mbjs_std_file_flush ),
    MBJS_CFUNC_MAGIC_DEF("tell", 0, mbjs_std_file_tell, 0 ),
    MBJS_CFUNC_MAGIC_DEF("tello", 0, mbjs_std_file_tell, 1 ),
    MBJS_CFUNC_DEF("seek", 2, mbjs_std_file_seek ),
    MBJS_CFUNC_DEF("eof", 0, mbjs_std_file_eof ),
    MBJS_CFUNC_DEF("fileno", 0, mbjs_std_file_fileno ),
    MBJS_CFUNC_DEF("error", 0, mbjs_std_file_error ),
    MBJS_CFUNC_DEF("clearerr", 0, mbjs_std_file_clearerr ),
    MBJS_CFUNC_MAGIC_DEF("read", 1, mbjs_std_file_read_write, 0 ),
    MBJS_CFUNC_MAGIC_DEF("write", 1, mbjs_std_file_read_write, 1 ),
    MBJS_CFUNC_DEF("getline", 0, mbjs_std_file_getline ),
    MBJS_CFUNC_MAGIC_DEF("readAsArrayBuffer", 0, mbjs_std_file_readAs, 0 ),
    MBJS_CFUNC_MAGIC_DEF("readAsString", 0, mbjs_std_file_readAs, 1 ),
    MBJS_CFUNC_DEF("getByte", 0, mbjs_std_file_getByte ),
    MBJS_CFUNC_DEF("putByte", 1, mbjs_std_file_putByte ),
    /* setvbuf, ...  */
};

static int mbjs_std_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    MBJSValue proto;
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);

    /* FILE class */
    /* the class ID is created once */
    MBJS_NewClassID(rt, &ts->std_file_class_id);
    /* the class is created once per runtime */
    MBJS_NewClass(rt, ts->std_file_class_id, &mbjs_std_file_class);
    proto = MBJS_NewObject(ctx);
    MBJS_SetPropertyFunctionList(ctx, proto, mbjs_std_file_proto_funcs,
                               countof(mbjs_std_file_proto_funcs));
    MBJS_SetClassProto(ctx, ts->std_file_class_id, proto);

    MBJS_SetModuleExportList(ctx, m, mbjs_std_funcs,
                           countof(mbjs_std_funcs));
    MBJS_SetModuleExport(ctx, m, "in", mbjs_new_std_file(ctx, stdin, false));
    MBJS_SetModuleExport(ctx, m, "out", mbjs_new_std_file(ctx, stdout, false));
    MBJS_SetModuleExport(ctx, m, "err", mbjs_new_std_file(ctx, stderr, false));
    return 0;
}

MBJSModuleDef *mbjs_init_module_std(MBJSContext *ctx, const char *module_name)
{
    MBJSModuleDef *m;
    m = MBJS_NewCModule(ctx, module_name, mbjs_std_init);
    if (!m)
        return NULL;
    MBJS_AddModuleExportList(ctx, m, mbjs_std_funcs, countof(mbjs_std_funcs));
    MBJS_AddModuleExport(ctx, m, "in");
    MBJS_AddModuleExport(ctx, m, "out");
    MBJS_AddModuleExport(ctx, m, "err");
    return m;
}

/**********************************************************/
/* 'os' object */

static MBJSValue mbjs_os_open(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv)
{
    const char *filename;
    int flags, mode, ret;

    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &flags, argv[1]))
        goto fail;
    if (argc >= 3 && !MBJS_IsUndefined(argv[2])) {
        if (MBJS_ToInt32(ctx, &mode, argv[2])) {
        fail:
            MBJS_FreeCString(ctx, filename);
            return MBJS_EXCEPTION;
        }
    } else {
        mode = 0666;
    }
#if defined(_WIN32)
    /* force binary mode by default */
    if (!(flags & O_TEXT))
        flags |= O_BINARY;
#endif
    ret = mbjs_get_errno(open(filename, flags, mode));
    MBJS_FreeCString(ctx, filename);
    return MBJS_NewInt32(ctx, ret);
}

static MBJSValue mbjs_os_close(MBJSContext *ctx, MBJSValueConst this_val,
                           int argc, MBJSValueConst *argv)
{
    int fd, ret;
    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    ret = mbjs_get_errno(close(fd));
    return MBJS_NewInt32(ctx, ret);
}

static MBJSValue mbjs_os_seek(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv)
{
    int fd, whence;
    int64_t pos, ret;
    bool is_bigint;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    is_bigint = MBJS_IsBigInt(argv[1]);
    if (MBJS_ToInt64Ext(ctx, &pos, argv[1]))
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &whence, argv[2]))
        return MBJS_EXCEPTION;
    ret = lseek(fd, pos, whence);
    if (ret == -1)
        ret = -errno;
    if (is_bigint)
        return MBJS_NewBigInt64(ctx, ret);
    else
        return MBJS_NewInt64(ctx, ret);
}

static MBJSValue mbjs_os_read_write(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv, int magic)
{
    int fd;
    uint64_t pos, len;
    size_t size;
    ssize_t ret;
    uint8_t *buf;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    if (MBJS_ToIndex(ctx, &pos, argv[2]))
        return MBJS_EXCEPTION;
    if (MBJS_ToIndex(ctx, &len, argv[3]))
        return MBJS_EXCEPTION;
    buf = MBJS_GetArrayBuffer(ctx, &size, argv[1]);
    if (!buf)
        return MBJS_EXCEPTION;
    if (pos + len > size)
        return MBJS_ThrowRangeError(ctx, "read/write array buffer overflow");
    if (magic)
        ret = mbjs_get_errno(write(fd, buf + pos, len));
    else
        ret = mbjs_get_errno(read(fd, buf + pos, len));
    return MBJS_NewInt64(ctx, ret);
}

static MBJSValue mbjs_os_isatty(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    int fd;
    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    return MBJS_NewBool(ctx, (isatty(fd) != 0));
}

#if defined(_WIN32)
static MBJSValue mbjs_os_ttyGetWinSize(MBJSContext *ctx, MBJSValueConst this_val,
                                   int argc, MBJSValueConst *argv)
{
    int fd;
    HANDLE handle;
    CONSOLE_SCREEN_BUFFER_INFO info;
    MBJSValue obj;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    handle = (HANDLE)_get_osfhandle(fd);

    if (!GetConsoleScreenBufferInfo(handle, &info))
        return MBJS_NULL;
    obj = MBJS_NewArray(ctx);
    if (MBJS_IsException(obj))
        return obj;
    MBJS_DefinePropertyValueUint32(ctx, obj, 0, MBJS_NewInt32(ctx, info.dwSize.X), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueUint32(ctx, obj, 1, MBJS_NewInt32(ctx, info.dwSize.Y), MBJS_PROP_C_W_E);
    return obj;
}

/* Windows 10 built-in VT100 emulation */
#define __ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#define __ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200

static MBJSValue mbjs_os_ttySetRaw(MBJSContext *ctx, MBJSValueConst this_val,
                               int argc, MBJSValueConst *argv)
{
    int fd;
    HANDLE handle;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    handle = (HANDLE)_get_osfhandle(fd);
    SetConsoleMode(handle, ENABLE_WINDOW_INPUT | __ENABLE_VIRTUAL_TERMINAL_INPUT);
    _setmode(fd, _O_BINARY);
    if (fd == 0) {
        handle = (HANDLE)_get_osfhandle(1); /* corresponding output */
        SetConsoleMode(handle, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | __ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    return MBJS_UNDEFINED;
}
#elif !defined(__wasi__)
static MBJSValue mbjs_os_ttyGetWinSize(MBJSContext *ctx, MBJSValueConst this_val,
                                   int argc, MBJSValueConst *argv)
{
    int fd;
    struct winsize ws;
    MBJSValue obj;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col >= 4 && ws.ws_row >= 4) {
        obj = MBJS_NewArray(ctx);
        if (MBJS_IsException(obj))
            return obj;
        MBJS_DefinePropertyValueUint32(ctx, obj, 0, MBJS_NewInt32(ctx, ws.ws_col), MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueUint32(ctx, obj, 1, MBJS_NewInt32(ctx, ws.ws_row), MBJS_PROP_C_W_E);
        return obj;
    } else {
        return MBJS_NULL;
    }
}

static struct termios oldtty;

static void term_exit(void)
{
    tcsetattr(0, TCSANOW, &oldtty);
}

/* XXX: should add a way to go back to normal mode */
static MBJSValue mbjs_os_ttySetRaw(MBJSContext *ctx, MBJSValueConst this_val,
                               int argc, MBJSValueConst *argv)
{
    struct termios tty;
    int fd;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;

    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);
    oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);

    atexit(term_exit);
    return MBJS_UNDEFINED;
}

#endif /* !_WIN32 && !__wasi__ */

static MBJSValue mbjs_os_remove(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    const char *filename;
    int ret;

    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        return MBJS_EXCEPTION;
#if defined(_WIN32)
    {
        struct stat st;
        if (stat(filename, &st) == 0 && (st.st_mode & _S_IFDIR)) {
            ret = rmdir(filename);
        } else {
            ret = unlink(filename);
        }
    }
#else
    ret = remove(filename);
#endif
    ret = mbjs_get_errno(ret);
    MBJS_FreeCString(ctx, filename);
    return MBJS_NewInt32(ctx, ret);
}

static MBJSValue mbjs_os_rename(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    const char *oldpath, *newpath;
    int ret;

    oldpath = MBJS_ToCString(ctx, argv[0]);
    if (!oldpath)
        return MBJS_EXCEPTION;
    newpath = MBJS_ToCString(ctx, argv[1]);
    if (!newpath) {
        MBJS_FreeCString(ctx, oldpath);
        return MBJS_EXCEPTION;
    }
    ret = mbjs_get_errno(rename(oldpath, newpath));
    MBJS_FreeCString(ctx, oldpath);
    MBJS_FreeCString(ctx, newpath);
    return MBJS_NewInt32(ctx, ret);
}

static bool is_main_thread(MBJSRuntime *rt)
{
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    return !ts->recv_pipe;
}

static MBJSOSRWHandler *find_rh(MBJSThreadState *ts, int fd)
{
    MBJSOSRWHandler *rh;
    struct list_head *el;

    list_for_each(el, &ts->os_rw_handlers) {
        rh = list_entry(el, MBJSOSRWHandler, link);
        if (rh->fd == fd)
            return rh;
    }
    return NULL;
}

static void free_rw_handler(MBJSRuntime *rt, MBJSOSRWHandler *rh)
{
    int i;
    list_del(&rh->link);
    for(i = 0; i < 2; i++) {
        MBJS_FreeValueRT(rt, rh->rw_func[i]);
    }
    mbjs_free_rt(rt, rh);
}

static MBJSValue mbjs_os_setReadHandler(MBJSContext *ctx, MBJSValueConst this_val,
                                    int argc, MBJSValueConst *argv, int magic)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSOSRWHandler *rh;
    int fd;
    MBJSValueConst func;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    func = argv[1];
    if (MBJS_IsNull(func)) {
        rh = find_rh(ts, fd);
        if (rh) {
            MBJS_FreeValue(ctx, rh->rw_func[magic]);
            rh->rw_func[magic] = MBJS_NULL;
            if (MBJS_IsNull(rh->rw_func[0]) &&
                MBJS_IsNull(rh->rw_func[1])) {
                /* remove the entry */
                free_rw_handler(MBJS_GetRuntime(ctx), rh);
            }
        }
    } else {
        if (!MBJS_IsFunction(ctx, func))
            return MBJS_ThrowTypeError(ctx, "not a function");
        rh = find_rh(ts, fd);
        if (!rh) {
            rh = mbjs_mallocz(ctx, sizeof(*rh));
            if (!rh)
                return MBJS_EXCEPTION;
            rh->fd = fd;
            rh->rw_func[0] = MBJS_NULL;
            rh->rw_func[1] = MBJS_NULL;
            list_add_tail(&rh->link, &ts->os_rw_handlers);
        }
        MBJS_FreeValue(ctx, rh->rw_func[magic]);
        rh->rw_func[magic] = MBJS_DupValue(ctx, func);
    }
    return MBJS_UNDEFINED;
}

static MBJSOSSignalHandler *find_sh(MBJSThreadState *ts, int sig_num)
{
    MBJSOSSignalHandler *sh;
    struct list_head *el;
    list_for_each(el, &ts->os_signal_handlers) {
        sh = list_entry(el, MBJSOSSignalHandler, link);
        if (sh->sig_num == sig_num)
            return sh;
    }
    return NULL;
}

static void free_sh(MBJSRuntime *rt, MBJSOSSignalHandler *sh)
{
    list_del(&sh->link);
    MBJS_FreeValueRT(rt, sh->func);
    mbjs_free_rt(rt, sh);
}

static void os_signal_handler(int sig_num)
{
    os_pending_signals |= ((uint64_t)1 << sig_num);
}

#if defined(_WIN32)
typedef void (*sighandler_t)(int sig_num);
#endif

static MBJSValue mbjs_os_signal(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSOSSignalHandler *sh;
    uint32_t sig_num;
    MBJSValueConst func;
    sighandler_t handler;

    if (!is_main_thread(rt))
        return MBJS_ThrowTypeError(ctx, "signal handler can only be set in the main thread");

    if (MBJS_ToUint32(ctx, &sig_num, argv[0]))
        return MBJS_EXCEPTION;
    if (sig_num >= 64)
        return MBJS_ThrowRangeError(ctx, "invalid signal number");
    func = argv[1];
    /* func = null: SIG_DFL, func = undefined, SIG_IGN */
    if (MBJS_IsNull(func) || MBJS_IsUndefined(func)) {
        sh = find_sh(ts, sig_num);
        if (sh) {
            free_sh(MBJS_GetRuntime(ctx), sh);
        }
        if (MBJS_IsNull(func))
            handler = SIG_DFL;
        else
            handler = SIG_IGN;
        signal(sig_num, handler);
    } else {
        if (!MBJS_IsFunction(ctx, func))
            return MBJS_ThrowTypeError(ctx, "not a function");
        sh = find_sh(ts, sig_num);
        if (!sh) {
            sh = mbjs_mallocz(ctx, sizeof(*sh));
            if (!sh)
                return MBJS_EXCEPTION;
            sh->sig_num = sig_num;
            list_add_tail(&sh->link, &ts->os_signal_handlers);
        }
        MBJS_FreeValue(ctx, sh->func);
        sh->func = MBJS_DupValue(ctx, func);
        signal(sig_num, os_signal_handler);
    }
    return MBJS_UNDEFINED;
}

#if !defined(_WIN32) && !defined(__wasi__)
static MBJSValue mbjs_os_cputime(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    struct rusage ru;
    int64_t cputime;

    cputime = 0;
    if (!getrusage(RUSAGE_SELF, &ru))
        cputime = (int64_t)ru.ru_utime.tv_sec * 1000000 + ru.ru_utime.tv_usec;

    return MBJS_NewInt64(ctx, cputime);
}
#endif

static MBJSValue mbjs_os_exepath(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    char buf[MBJS__PATH_MAX];
    size_t len = sizeof(buf);
    if (mbjs_exepath(buf, &len))
        return MBJS_UNDEFINED;
    return MBJS_NewStringLen(ctx, buf, len);
}

static MBJSValue mbjs_os_now(MBJSContext *ctx, MBJSValueConst this_val,
                         int argc, MBJSValueConst *argv)
{
    return MBJS_NewInt64(ctx, mbjs__hrtime_ns() / 1000);
}

/* Return process/system memory in raw bytes (C99/baremetal-friendly).
 * JS side formats for display. Fields are integers (bytes/counts), never strings.
 *   rss/private/peakRss/pageFaultCount: current process (Windows: K32GetProcessMemoryInfo)
 *   systemTotal/systemFree/systemUsed:   OS physical memory (Windows: GlobalMemoryStatusEx)
 */
static MBJSValue mbjs_os_memoryInfo(MBJSContext *ctx, MBJSValueConst this_val,
                                    int argc, MBJSValueConst *argv)
{
    MBJSValue obj = MBJS_NewObject(ctx);
    if (MBJS_IsException(obj))
        return obj;

    MBJS_DefinePropertyValueStr(ctx, obj, "rss", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "peakRss", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "privateBytes", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "pageFaultCount", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "systemTotal", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "systemFree", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "systemUsed", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "heapUsed", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "heapSize", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueStr(ctx, obj, "mallocCount", MBJS_NewInt64(ctx, 0), MBJS_PROP_C_W_E);

#if defined(_WIN32)
    {
        typedef struct { DWORD cb; DWORD PageFaultCount; SIZE_T PeakWorkingSetSize;
                         SIZE_T WorkingSetSize; SIZE_T QuotaPeakPagedPoolUsage;
                         SIZE_T QuotaPagedPoolUsage; SIZE_T QuotaPeakNonPagedPoolUsage;
                         SIZE_T QuotaNonPagedPoolUsage; SIZE_T PagefileUsage;
                         SIZE_T PeakPagefileUsage; SIZE_T PrivateUsage; } MBJS_PMC_EX;
        typedef BOOL (WINAPI *MBJS_K32GetProcessMemoryInfoFn)(HANDLE, MBJS_PMC_EX *, DWORD);
        typedef BOOL (WINAPI *MBJS_GlobalMemoryStatusExFn)(void *);
        typedef struct { DWORD dwLength; DWORD dwMemoryLoad; unsigned long long ullTotalPhys;
                         unsigned long long ullAvailPhys; unsigned long long ullTotalPageFile;
                         unsigned long long ullAvailPageFile; unsigned long long ullTotalVirtual;
                         unsigned long long ullAvailVirtual; unsigned long long ullAvailExtendedVirtual; } MBJS_MEMSTATEX;

        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        MBJS_K32GetProcessMemoryInfoFn k32Get = NULL;
        if (k32)
            k32Get = (MBJS_K32GetProcessMemoryInfoFn)GetProcAddress(k32, "K32GetProcessMemoryInfo");
        if (!k32Get) {
            HMODULE psapi = GetModuleHandleA("psapi.dll");
            if (!psapi) psapi = LoadLibraryA("psapi.dll");
            if (psapi)
                k32Get = (MBJS_K32GetProcessMemoryInfoFn)GetProcAddress(psapi, "GetProcessMemoryInfo");
        }
        if (k32Get) {
            MBJS_PMC_EX pmc;
            memset(&pmc, 0, sizeof(pmc));
            pmc.cb = sizeof(pmc);
            if (k32Get(GetCurrentProcess(), &pmc, (DWORD)sizeof(pmc))) {
                MBJS_DefinePropertyValueStr(ctx, obj, "rss", MBJS_NewInt64(ctx, (int64_t)pmc.WorkingSetSize), MBJS_PROP_C_W_E);
                MBJS_DefinePropertyValueStr(ctx, obj, "peakRss", MBJS_NewInt64(ctx, (int64_t)pmc.PeakWorkingSetSize), MBJS_PROP_C_W_E);
                MBJS_DefinePropertyValueStr(ctx, obj, "privateBytes", MBJS_NewInt64(ctx, (int64_t)pmc.PrivateUsage), MBJS_PROP_C_W_E);
                MBJS_DefinePropertyValueStr(ctx, obj, "pageFaultCount", MBJS_NewInt64(ctx, (int64_t)pmc.PageFaultCount), MBJS_PROP_C_W_E);
            }
        }

        if (k32) {
            MBJS_GlobalMemoryStatusExFn gms = (MBJS_GlobalMemoryStatusExFn)GetProcAddress(k32, "GlobalMemoryStatusEx");
            if (gms) {
                MBJS_MEMSTATEX ms;
                memset(&ms, 0, sizeof(ms));
                ms.dwLength = (DWORD)sizeof(ms);
                if (gms(&ms)) {
                    unsigned long long used = 0;
                    if (ms.ullTotalPhys >= ms.ullAvailPhys)
                        used = ms.ullTotalPhys - ms.ullAvailPhys;
                    MBJS_DefinePropertyValueStr(ctx, obj, "systemTotal", MBJS_NewInt64(ctx, (int64_t)ms.ullTotalPhys), MBJS_PROP_C_W_E);
                    MBJS_DefinePropertyValueStr(ctx, obj, "systemFree", MBJS_NewInt64(ctx, (int64_t)ms.ullAvailPhys), MBJS_PROP_C_W_E);
                    MBJS_DefinePropertyValueStr(ctx, obj, "systemUsed", MBJS_NewInt64(ctx, (int64_t)used), MBJS_PROP_C_W_E);
                }
            }
        }
    }
#elif !defined(__wasi__)
    {
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
#if defined(__APPLE__)
            int64_t rss = (int64_t)ru.ru_maxrss;
#else
            int64_t rss = (int64_t)ru.ru_maxrss * 1024;
#endif
            MBJS_DefinePropertyValueStr(ctx, obj, "rss", MBJS_NewInt64(ctx, rss), MBJS_PROP_C_W_E);
            MBJS_DefinePropertyValueStr(ctx, obj, "peakRss", MBJS_NewInt64(ctx, rss), MBJS_PROP_C_W_E);
        }
    }
#endif
    {
        MBJSMemoryUsage mu;
        MBJS_ComputeMemoryUsage(MBJS_GetRuntime(ctx), &mu);
        MBJS_DefinePropertyValueStr(ctx, obj, "heapUsed", MBJS_NewInt64(ctx, (int64_t)mu.memory_used_size), MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "heapSize", MBJS_NewInt64(ctx, (int64_t)mu.malloc_size), MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "mallocCount", MBJS_NewInt64(ctx, (int64_t)mu.malloc_count), MBJS_PROP_C_W_E);
    }

    return obj;
}

static MBJSValue mbjs_os_gc(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    MBJS_RunGC(MBJS_GetRuntime(ctx));
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_os_setGCThreshold(MBJSContext *ctx, MBJSValueConst this_val,
                                        int argc, MBJSValueConst *argv)
{
    int64_t v = 0;
    if (argc > 0 && MBJS_ToInt64(ctx, &v, argv[0]))
        return MBJS_EXCEPTION;
    if (v < 0) v = 0;
    MBJS_SetGCThreshold(MBJS_GetRuntime(ctx), (size_t)v);
    return MBJS_UNDEFINED;
}

static uint64_t mbjs__hrtime_ms(void)
{
    return mbjs__hrtime_ns() / (1000 * 1000);
}

static void free_timer(MBJSRuntime *rt, MBJSOSTimer *th)
{
    list_del(&th->link);
    MBJS_FreeValueRT(rt, th->func);
    mbjs_free_rt(rt, th);
}

// TODO(bnoordhuis) accept string as first arg and eval at timer expiry
// TODO(bnoordhuis) retain argv[2..] as args for callback if argc > 2
static MBJSValue mbjs_os_setTimeout(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv, int magic)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    int64_t delay;
    MBJSValueConst func;
    MBJSOSTimer *th;

    func = argv[0];
    if (!MBJS_IsFunction(ctx, func))
        return MBJS_ThrowTypeError(ctx, "not a function");
    if (MBJS_ToInt64(ctx, &delay, argv[1]))
        return MBJS_EXCEPTION;
    if (delay < 1)
        delay = 1;
    th = mbjs_mallocz(ctx, sizeof(*th));
    if (!th)
        return MBJS_EXCEPTION;
    th->timer_id = ts->next_timer_id++;
    if (ts->next_timer_id > MAX_SAFE_INTEGER)
        ts->next_timer_id = 1;
    th->repeats = (magic > 0);
    th->timeout = mbjs__hrtime_ms() + delay;
    th->delay = delay;
    th->func = MBJS_DupValue(ctx, func);
    list_add_tail(&th->link, &ts->os_timers);
    return MBJS_NewInt64(ctx, th->timer_id);
}

static MBJSOSTimer *find_timer_by_id(MBJSThreadState *ts, int timer_id)
{
    struct list_head *el;
    if (timer_id <= 0)
        return NULL;
    list_for_each(el, &ts->os_timers) {
        MBJSOSTimer *th = list_entry(el, MBJSOSTimer, link);
        if (th->timer_id == timer_id)
            return th;
    }
    return NULL;
}

static MBJSValue mbjs_os_clearTimeout(MBJSContext *ctx, MBJSValueConst this_val,
                                  int argc, MBJSValueConst *argv)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSOSTimer *th;
    int64_t timer_id;

    if (MBJS_ToInt64(ctx, &timer_id, argv[0]))
        return MBJS_EXCEPTION;
    th = find_timer_by_id(ts, timer_id);
    if (!th)
        return MBJS_UNDEFINED;
    free_timer(rt, th);
    return MBJS_UNDEFINED;
}

/* return a promise */
static MBJSValue mbjs_os_sleepAsync(MBJSContext *ctx, MBJSValueConst this_val,
                                int argc, MBJSValueConst *argv)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    int64_t delay;
    MBJSOSTimer *th;
    MBJSValue promise, resolving_funcs[2];

    if (MBJS_ToInt64(ctx, &delay, argv[0]))
        return MBJS_EXCEPTION;
    promise = MBJS_NewPromiseCapability(ctx, resolving_funcs);
    if (MBJS_IsException(promise))
        return MBJS_EXCEPTION;

    th = mbjs_mallocz(ctx, sizeof(*th));
    if (!th) {
        MBJS_FreeValue(ctx, promise);
        MBJS_FreeValue(ctx, resolving_funcs[0]);
        MBJS_FreeValue(ctx, resolving_funcs[1]);
        return MBJS_EXCEPTION;
    }
    th->timer_id = -1;
    th->timeout = mbjs__hrtime_ms() + delay;
    th->func = MBJS_DupValue(ctx, resolving_funcs[0]);
    list_add_tail(&th->link, &ts->os_timers);
    MBJS_FreeValue(ctx, resolving_funcs[0]);
    MBJS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

static int call_handler(MBJSContext *ctx, MBJSValue func)
{
    int r;
    MBJSValue ret, func1;
    /* 'func' might be destroyed when calling itself (if it frees the
       handler), so must take extra care */
    func1 = MBJS_DupValue(ctx, func);
    ret = MBJS_Call(ctx, func1, MBJS_UNDEFINED, 0, NULL);
    MBJS_FreeValue(ctx, func1);
    r = 0;
    if (MBJS_IsException(ret))
        r = -1;
    MBJS_FreeValue(ctx, ret);
    return r;
}

static int mbjs_os_run_timers(MBJSRuntime *rt, MBJSContext *ctx, MBJSThreadState *ts, int *min_delay)
{
    MBJSValue func;
    MBJSOSTimer *th;
    int64_t cur_time, delay;
    struct list_head *el;
    int r;

    if (list_empty(&ts->os_timers)) {
        *min_delay = -1;
        return 0;
    }

    cur_time = mbjs__hrtime_ms();
    *min_delay = INT32_MAX;

    list_for_each(el, &ts->os_timers) {
        th = list_entry(el, MBJSOSTimer, link);
        delay = th->timeout - cur_time;
        if (delay > 0) {
            *min_delay = min_int(*min_delay, delay);
        } else {
            *min_delay = 0;
            func = MBJS_DupValueRT(rt, th->func);
            if (th->repeats)
                th->timeout = cur_time + th->delay;
            else
                free_timer(rt, th);
            r = call_handler(ctx, func);
            MBJS_FreeValueRT(rt, func);
            return r;
        }
    }

    return 0;
}

#ifdef USE_WORKER

#ifdef _WIN32

static int mbjs_waker_init(MBJSWaker *w)
{
    w->handle = CreateEvent(NULL, TRUE, FALSE, NULL);
    return w->handle ? 0 : -1;
}

static void mbjs_waker_signal(MBJSWaker *w)
{
    SetEvent(w->handle);
}

static void mbjs_waker_clear(MBJSWaker *w)
{
    ResetEvent(w->handle);
}

static void mbjs_waker_close(MBJSWaker *w)
{
    CloseHandle(w->handle);
    w->handle = INVALID_HANDLE_VALUE;
}

#else // !_WIN32

static int mbjs_waker_init(MBJSWaker *w)
{
    int fds[2];

    if (pipe(fds) < 0)
        return -1;
    w->read_fd = fds[0];
    w->write_fd = fds[1];
    return 0;
}

static void mbjs_waker_signal(MBJSWaker *w)
{
    int ret;

    for(;;) {
        ret = write(w->write_fd, "", 1);
        if (ret == 1)
            break;
        if (ret < 0 && errno != EAGAIN && errno != EINTR)
            break;
    }
}

static void mbjs_waker_clear(MBJSWaker *w)
{
    uint8_t buf[16];
    int ret;

    for(;;) {
        ret = read(w->read_fd, buf, sizeof(buf));
        if (ret >= 0)
            break;
        if (errno != EAGAIN && errno != EINTR)
            break;
    }
}

static void mbjs_waker_close(MBJSWaker *w)
{
    close(w->read_fd);
    close(w->write_fd);
    w->read_fd = -1;
    w->write_fd = -1;
}

#endif // _WIN32

static void mbjs_free_message(MBJSWorkerMessage *msg);

/* return 1 if a message was handled, 0 if no message */
static int handle_posted_message(MBJSRuntime *rt, MBJSContext *ctx,
                                 MBJSWorkerMessageHandler *port)
{
    MBJSWorkerMessagePipe *ps = port->recv_pipe;
    int ret;
    struct list_head *el;
    MBJSWorkerMessage *msg;
    MBJSValue obj, data_obj, func, retval;

    mbjs_mutex_lock(&ps->mutex);
    if (!list_empty(&ps->msg_queue)) {
        el = ps->msg_queue.next;
        msg = list_entry(el, MBJSWorkerMessage, link);

        /* remove the message from the queue */
        list_del(&msg->link);

        // drain read end of pipe
        if (list_empty(&ps->msg_queue))
            mbjs_waker_clear(&ps->waker);

        mbjs_mutex_unlock(&ps->mutex);

        data_obj = MBJS_ReadObject(ctx, msg->data, msg->data_len,
                                 MBJS_READ_OBJ_SAB | MBJS_READ_OBJ_REFERENCE);

        mbjs_free_message(msg);

        if (MBJS_IsException(data_obj))
            goto fail;
        obj = MBJS_NewObject(ctx);
        if (MBJS_IsException(obj)) {
            MBJS_FreeValue(ctx, data_obj);
            goto fail;
        }
        MBJS_DefinePropertyValueStr(ctx, obj, "data", data_obj, MBJS_PROP_C_W_E);

        /* 'func' might be destroyed when calling itself (if it frees the
           handler), so must take extra care */
        func = MBJS_DupValue(ctx, port->on_message_func);
        retval = MBJS_Call(ctx, func, MBJS_UNDEFINED, 1, (MBJSValueConst *)&obj);
        MBJS_FreeValue(ctx, obj);
        MBJS_FreeValue(ctx, func);
        if (MBJS_IsException(retval)) {
        fail:
            mbjs_std_dump_error(ctx);
        } else {
            MBJS_FreeValue(ctx, retval);
        }
        ret = 1;
    } else {
        mbjs_mutex_unlock(&ps->mutex);
        ret = 0;
    }
    return ret;
}

#endif // USE_WORKER

/* flags for mbjs_os_poll_internal */
#define MBJS_OS_POLL_RUN_TIMERS  (1 << 0)
#define MBJS_OS_POLL_WORKERS     (1 << 1)
#define MBJS_OS_POLL_SIGNALS     (1 << 2)

#if defined(_WIN32)
static int mbjs_os_poll_internal(MBJSContext *ctx, int timeout_ms, int flags)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    int min_delay, count;
    MBJSOSRWHandler *rh;
    struct list_head *el;
    HANDLE handles[MAXIMUM_WAIT_OBJECTS]; // 64

    /* XXX: handle signals if useful */

    min_delay = timeout_ms;

    if (flags & MBJS_OS_POLL_RUN_TIMERS) {
        if (mbjs_os_run_timers(rt, ctx, ts, &min_delay))
            return -1;
        if (min_delay == 0)
            return 0; // expired timer
        if (min_delay < 0)
            if (list_empty(&ts->os_rw_handlers) && list_empty(&ts->port_list))
                return -1; /* no more events */
    }

    count = 0;
    list_for_each(el, &ts->os_rw_handlers) {
        rh = list_entry(el, MBJSOSRWHandler, link);
        if (rh->fd == 0 && !MBJS_IsNull(rh->rw_func[0]))
            handles[count++] = (HANDLE)_get_osfhandle(rh->fd); // stdin
        if (count == (int)countof(handles))
            break;
    }

    if (flags & MBJS_OS_POLL_WORKERS) {
        list_for_each(el, &ts->port_list) {
            MBJSWorkerMessageHandler *port = list_entry(el, MBJSWorkerMessageHandler, link);
            if (MBJS_IsNull(port->on_message_func))
                continue;
            handles[count++] = port->recv_pipe->waker.handle;
            if (count == (int)countof(handles))
                break;
        }
    }

    if (count > 0) {
        DWORD ret, timeout = INFINITE;
        if (min_delay != -1)
            timeout = min_delay;
        ret = WaitForMultipleObjects(count, handles, FALSE, timeout);
        if (ret < (DWORD)count) {
            list_for_each(el, &ts->os_rw_handlers) {
                rh = list_entry(el, MBJSOSRWHandler, link);
                if (rh->fd == 0 && !MBJS_IsNull(rh->rw_func[0])) {
                    return call_handler(ctx, rh->rw_func[0]);
                    /* must stop because the list may have been modified */
                }
            }

            if (flags & MBJS_OS_POLL_WORKERS) {
                list_for_each(el, &ts->port_list) {
                    MBJSWorkerMessageHandler *port = list_entry(el, MBJSWorkerMessageHandler, link);
                    if (!MBJS_IsNull(port->on_message_func)) {
                        MBJSWorkerMessagePipe *ps = port->recv_pipe;
                        if (ps->waker.handle == handles[ret]) {
                            if (handle_posted_message(rt, ctx, port))
                                goto done;
                        }
                    }
                }
            }
        }
    } else if (min_delay > 0) {
        Sleep(min_delay);
    }
done:
    return 0;
}

static int mbjs_os_poll(MBJSContext *ctx)
{
    return mbjs_os_poll_internal(ctx, -1,
        MBJS_OS_POLL_RUN_TIMERS | MBJS_OS_POLL_WORKERS | MBJS_OS_POLL_SIGNALS);
}

int mbjs_std_poll_io(MBJSContext *ctx, int timeout_ms)
{
    int ret = mbjs_os_poll_internal(ctx, timeout_ms, 0);
    /* map return codes: -1 on error stays -1, negative from handler becomes -2 */
    if (ret < -1)
        return -2;
    return ret;
}
#else // !defined(_WIN32)
static int mbjs_os_poll_internal(MBJSContext *ctx, int timeout_ms, int flags)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    int r, w, ret, nfds, min_delay;
    MBJSOSRWHandler *rh;
    struct list_head *el;
    struct pollfd *pfd, *pfds, pfds_local[64];

    min_delay = timeout_ms;

    /* only check signals in the main thread */
    if ((flags & MBJS_OS_POLL_SIGNALS) &&
        !ts->recv_pipe &&
        unlikely(os_pending_signals != 0)) {
        MBJSOSSignalHandler *sh;
        uint64_t mask;

        list_for_each(el, &ts->os_signal_handlers) {
            sh = list_entry(el, MBJSOSSignalHandler, link);
            mask = (uint64_t)1 << sh->sig_num;
            if (os_pending_signals & mask) {
                os_pending_signals &= ~mask;
                return call_handler(ctx, sh->func);
            }
        }
    }

    if (flags & MBJS_OS_POLL_RUN_TIMERS) {
        if (mbjs_os_run_timers(rt, ctx, ts, &min_delay))
            return -1;
        if (min_delay == 0)
            return 0; // expired timer
        if (min_delay < 0)
            if (list_empty(&ts->os_rw_handlers) && list_empty(&ts->port_list))
                return -1; /* no more events */
    }

    nfds = 0;
    list_for_each(el, &ts->os_rw_handlers) {
        rh = list_entry(el, MBJSOSRWHandler, link);
        nfds += (!MBJS_IsNull(rh->rw_func[0]) || !MBJS_IsNull(rh->rw_func[1]));
    }

#ifdef USE_WORKER
    if (flags & MBJS_OS_POLL_WORKERS) {
        list_for_each(el, &ts->port_list) {
            MBJSWorkerMessageHandler *port = list_entry(el, MBJSWorkerMessageHandler, link);
            nfds += !MBJS_IsNull(port->on_message_func);
        }
    }
#endif // USE_WORKER

    if (nfds == 0) {
        if (min_delay > 0) {
            struct timespec ts_sleep = {
                .tv_sec = min_delay / 1000,
                .tv_nsec = (min_delay % 1000) * 1000000L
            };
            uint64_t mask = os_pending_signals;
            while (nanosleep(&ts_sleep, &ts_sleep)
                && errno == EINTR
                && mask == os_pending_signals);
        }
        return 0;
    }

    pfd = pfds = pfds_local;
    if (nfds > (int)countof(pfds_local)) {
        pfd = pfds = mbjs_malloc(ctx, nfds * sizeof(*pfd));
        if (!pfd)
            return -1;
    }

    list_for_each(el, &ts->os_rw_handlers) {
        rh = list_entry(el, MBJSOSRWHandler, link);
        r = POLLIN * !MBJS_IsNull(rh->rw_func[0]);
        w = POLLOUT * !MBJS_IsNull(rh->rw_func[1]);
        if (r || w)
            *pfd++ = (struct pollfd){rh->fd, r|w, 0};
    }

#ifdef USE_WORKER
    if (flags & MBJS_OS_POLL_WORKERS) {
        list_for_each(el, &ts->port_list) {
            MBJSWorkerMessageHandler *port = list_entry(el, MBJSWorkerMessageHandler, link);
            if (!MBJS_IsNull(port->on_message_func)) {
                MBJSWorkerMessagePipe *ps = port->recv_pipe;
                *pfd++ = (struct pollfd){ps->waker.read_fd, POLLIN, 0};
            }
        }
    }
#endif // USE_WORKER

    // FIXME(bnoordhuis) the loop below is quadratic in theory but
    // linear-ish in practice because we bail out on the first hit,
    // i.e., it's probably good enough for now
    ret = 0;
    nfds = poll(pfds, nfds, min_delay);
    if (nfds < 0) {
        ret = -1;
        goto done;
    }
    for (pfd = pfds; nfds-- > 0; pfd++) {
        rh = find_rh(ts, pfd->fd);
        if (rh) {
            r = (POLLERR|POLLHUP|POLLNVAL|POLLIN) * !MBJS_IsNull(rh->rw_func[0]);
            w = (POLLERR|POLLHUP|POLLNVAL|POLLOUT) * !MBJS_IsNull(rh->rw_func[1]);
            if (r & pfd->revents) {
                ret = call_handler(ctx, rh->rw_func[0]);
                goto done;
                /* must stop because the list may have been modified */
            }
            if (w & pfd->revents) {
                ret = call_handler(ctx, rh->rw_func[1]);
                goto done;
                /* must stop because the list may have been modified */
            }
        }
#ifdef USE_WORKER
        else if (flags & MBJS_OS_POLL_WORKERS) {
            list_for_each(el, &ts->port_list) {
                MBJSWorkerMessageHandler *port = list_entry(el, MBJSWorkerMessageHandler, link);
                if (!MBJS_IsNull(port->on_message_func)) {
                    MBJSWorkerMessagePipe *ps = port->recv_pipe;
                    if (pfd->fd == ps->waker.read_fd) {
                        if (handle_posted_message(rt, ctx, port))
                            goto done;
                    }
                }
            }
        }
#endif // USE_WORKER
    }
done:
    if (pfds != pfds_local)
        mbjs_free(ctx, pfds);
    return ret;
}

static int mbjs_os_poll(MBJSContext *ctx)
{
    return mbjs_os_poll_internal(ctx, -1,
        MBJS_OS_POLL_RUN_TIMERS | MBJS_OS_POLL_WORKERS | MBJS_OS_POLL_SIGNALS);
}

int mbjs_std_poll_io(MBJSContext *ctx, int timeout_ms)
{
    int ret = mbjs_os_poll_internal(ctx, timeout_ms, 0);
    /* map return codes: -1 on error stays -1, negative from handler becomes -2 */
    if (ret < -1)
        return -2;
    return ret;
}
#endif // defined(_WIN32)

static MBJSValue make_obj_error(MBJSContext *ctx,
                              MBJSValue obj,
                              int err)
{
    MBJSValue vals[2];

    if (MBJS_IsException(obj))
        return obj;
    vals[0] = obj;
    vals[1] = MBJS_NewInt32(ctx, err);
    return MBJS_NewArrayFrom(ctx, countof(vals), vals);
}

static MBJSValue make_string_error(MBJSContext *ctx,
                                 const char *buf,
                                 int err)
{
    return make_obj_error(ctx, MBJS_NewString(ctx, buf), err);
}

/* return [cwd, errorcode] */
static MBJSValue mbjs_os_getcwd(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    char buf[MBJS__PATH_MAX];
    int err;

    if (!getcwd(buf, sizeof(buf))) {
        buf[0] = '\0';
        err = errno;
    } else {
        err = 0;
    }
    return make_string_error(ctx, buf, err);
}

static MBJSValue mbjs_os_chdir(MBJSContext *ctx, MBJSValueConst this_val,
                           int argc, MBJSValueConst *argv)
{
    const char *target;
    int err;

    target = MBJS_ToCString(ctx, argv[0]);
    if (!target)
        return MBJS_EXCEPTION;
    err = mbjs_get_errno(chdir(target));
    MBJS_FreeCString(ctx, target);
    return MBJS_NewInt32(ctx, err);
}

static MBJSValue mbjs_os_mkdir(MBJSContext *ctx, MBJSValueConst this_val,
                           int argc, MBJSValueConst *argv)
{
    int mode, ret;
    const char *path;

    if (argc >= 2) {
        if (MBJS_ToInt32(ctx, &mode, argv[1]))
            return MBJS_EXCEPTION;
    } else {
        mode = 0777;
    }
    path = MBJS_ToCString(ctx, argv[0]);
    if (!path)
        return MBJS_EXCEPTION;
#if defined(_WIN32)
    (void)mode;
    ret = mbjs_get_errno(mkdir(path));
#else
    ret = mbjs_get_errno(mkdir(path, mode));
#endif
    MBJS_FreeCString(ctx, path);
    return MBJS_NewInt32(ctx, ret);
}

/* return [array, errorcode] */
static MBJSValue mbjs_os_readdir(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
#ifdef _WIN32
    const char *path;
    MBJSValue obj;
    int err;
    uint32_t len;
    HANDLE h;
    WIN32_FIND_DATAA d;
    char s[1024];

    path = MBJS_ToCString(ctx, argv[0]);
    if (!path)
        return MBJS_EXCEPTION;
    obj = MBJS_NewArray(ctx);
    if (MBJS_IsException(obj)) {
        MBJS_FreeCString(ctx, path);
        return MBJS_EXCEPTION;
    }
    snprintf(s, sizeof(s), "%s/*", path);
    MBJS_FreeCString(ctx, path);
    err = 0;
    h = FindFirstFileA(s, &d);
    if (h == INVALID_HANDLE_VALUE)
        err = GetLastError();
    if (err)
        goto done;
    MBJS_DefinePropertyValueUint32(ctx, obj, 0, MBJS_NewString(ctx, "."),
                                 MBJS_PROP_C_W_E);
    for (len = 1; FindNextFileA(h, &d); len++) {
        MBJS_DefinePropertyValueUint32(ctx, obj, len,
                                     MBJS_NewString(ctx, d.cFileName),
                                     MBJS_PROP_C_W_E);
    }
    FindClose(h);
done:
    return make_obj_error(ctx, obj, err);
#else
    const char *path;
    DIR *f;
    struct dirent *d;
    MBJSValue obj;
    int err;
    uint32_t len;

    path = MBJS_ToCString(ctx, argv[0]);
    if (!path)
        return MBJS_EXCEPTION;
    obj = MBJS_NewArray(ctx);
    if (MBJS_IsException(obj)) {
        MBJS_FreeCString(ctx, path);
        return MBJS_EXCEPTION;
    }
    f = opendir(path);
    if (!f)
        err = errno;
    else
        err = 0;
    MBJS_FreeCString(ctx, path);
    if (!f)
        goto done;
    len = 0;
    for(;;) {
        errno = 0;
        d = readdir(f);
        if (!d) {
            err = errno;
            break;
        }
        MBJS_DefinePropertyValueUint32(ctx, obj, len++,
                                     MBJS_NewString(ctx, d->d_name),
                                     MBJS_PROP_C_W_E);
    }
    closedir(f);
 done:
    return make_obj_error(ctx, obj, err);
#endif
}

#if !defined(_WIN32) && !defined(__wasi__)
#define PAT "XXXXXX"
#define PSZ (sizeof(PAT)-1)
static MBJSValue mbjs_os_mkdstemp(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv, int magic)
{
    char pat_s[32] = "tmp"PAT, *pat = pat_s;
    const char *s;
    size_t k, n;
    MBJSValue val;
    int err;

    if (argc > 0) {
        s = MBJS_ToCStringLen(ctx, &n, argv[0]);
        if (!s)
            return MBJS_EXCEPTION;
        k = n;
        if (n < PSZ || memcmp(&s[n-PSZ], PAT, PSZ))
            k += PSZ;
        if (k >= sizeof(pat_s))
            pat = mbjs_malloc(ctx, k+1);
        if (pat) {
            memcpy(pat, s, n);
            if (n < k)
                memcpy(&pat[n], PAT, PSZ);
            pat[k] = '\0';
        }
        MBJS_FreeCString(ctx, s);
        if (!pat)
            return MBJS_EXCEPTION;
    }
    if (magic == 'd') {
        err = 0;
        if (!mkdtemp(pat))
            err = -errno;
    } else {
        err = mbjs_get_errno(mkstemp(pat));
    }
    val = MBJS_NewString(ctx, pat);
    if (pat != pat_s)
        mbjs_free(ctx, pat);
    return make_obj_error(ctx, val, err);
}
#undef PSZ
#undef PAT
#endif // !defined(_WIN32) && !defined(__wasi__)

#if !defined(_WIN32)
static int64_t timespec_to_ms(const struct timespec *tv)
{
    return (int64_t)tv->tv_sec * 1000 + (tv->tv_nsec / 1000000);
}
#endif

/* return [obj, errcode] */
static MBJSValue mbjs_os_stat(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv, int is_lstat)
{
    const char *path;
    int err, res;
    struct stat st;
    MBJSValue obj;

    path = MBJS_ToCString(ctx, argv[0]);
    if (!path)
        return MBJS_EXCEPTION;
#if defined(_WIN32)
    res = stat(path, &st);
#else
    if (is_lstat)
        res = lstat(path, &st);
    else
        res = stat(path, &st);
#endif
    err = (res < 0) ? errno : 0;
    MBJS_FreeCString(ctx, path);
    if (res < 0) {
        obj = MBJS_NULL;
    } else {
        obj = MBJS_NewObject(ctx);
        if (MBJS_IsException(obj))
            return MBJS_EXCEPTION;
        MBJS_DefinePropertyValueStr(ctx, obj, "dev",
                                  MBJS_NewInt64(ctx, st.st_dev),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "ino",
                                  MBJS_NewInt64(ctx, st.st_ino),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "mode",
                                  MBJS_NewInt32(ctx, st.st_mode),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "nlink",
                                  MBJS_NewInt64(ctx, st.st_nlink),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "uid",
                                  MBJS_NewInt64(ctx, st.st_uid),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "gid",
                                  MBJS_NewInt64(ctx, st.st_gid),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "rdev",
                                  MBJS_NewInt64(ctx, st.st_rdev),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "size",
                                  MBJS_NewInt64(ctx, st.st_size),
                                  MBJS_PROP_C_W_E);
#if !defined(_WIN32)
        MBJS_DefinePropertyValueStr(ctx, obj, "blocks",
                                  MBJS_NewInt64(ctx, st.st_blocks),
                                  MBJS_PROP_C_W_E);
#endif
#if defined(_WIN32)
        MBJS_DefinePropertyValueStr(ctx, obj, "atime",
                                  MBJS_NewInt64(ctx, (int64_t)st.st_atime * 1000),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "mtime",
                                  MBJS_NewInt64(ctx, (int64_t)st.st_mtime * 1000),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "ctime",
                                  MBJS_NewInt64(ctx, (int64_t)st.st_ctime * 1000),
                                  MBJS_PROP_C_W_E);
#elif defined(__APPLE__)
        MBJS_DefinePropertyValueStr(ctx, obj, "atime",
                                  MBJS_NewInt64(ctx, timespec_to_ms(&st.st_atimespec)),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "mtime",
                                  MBJS_NewInt64(ctx, timespec_to_ms(&st.st_mtimespec)),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "ctime",
                                  MBJS_NewInt64(ctx, timespec_to_ms(&st.st_ctimespec)),
                                  MBJS_PROP_C_W_E);
#else
        MBJS_DefinePropertyValueStr(ctx, obj, "atime",
                                  MBJS_NewInt64(ctx, timespec_to_ms(&st.st_atim)),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "mtime",
                                  MBJS_NewInt64(ctx, timespec_to_ms(&st.st_mtim)),
                                  MBJS_PROP_C_W_E);
        MBJS_DefinePropertyValueStr(ctx, obj, "ctime",
                                  MBJS_NewInt64(ctx, timespec_to_ms(&st.st_ctim)),
                                  MBJS_PROP_C_W_E);
#endif
    }
    return make_obj_error(ctx, obj, err);
}

#if !defined(_WIN32)
static void ms_to_timeval(struct timeval *tv, uint64_t v)
{
    tv->tv_sec = v / 1000;
    tv->tv_usec = (v % 1000) * 1000;
}
#endif

static MBJSValue mbjs_os_utimes(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    const char *path;
    int64_t atime, mtime;
    int ret;

    if (MBJS_ToInt64(ctx, &atime, argv[1]))
        return MBJS_EXCEPTION;
    if (MBJS_ToInt64(ctx, &mtime, argv[2]))
        return MBJS_EXCEPTION;
    path = MBJS_ToCString(ctx, argv[0]);
    if (!path)
        return MBJS_EXCEPTION;
#if defined(_WIN32)
    {
        struct _utimbuf times;
        times.actime = atime / 1000;
        times.modtime = mtime / 1000;
        ret = mbjs_get_errno(_utime(path, &times));
    }
#else
    {
        struct timeval times[2];
        ms_to_timeval(&times[0], atime);
        ms_to_timeval(&times[1], mtime);
        ret = mbjs_get_errno(utimes(path, times));
    }
#endif
    MBJS_FreeCString(ctx, path);
    return MBJS_NewInt32(ctx, ret);
}

/* sleep(delay_ms) */
static MBJSValue mbjs_os_sleep(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv)
{
    int64_t delay;
    int ret;

    if (MBJS_ToInt64(ctx, &delay, argv[0]))
        return MBJS_EXCEPTION;
    if (delay < 0)
        delay = 0;
#if defined(_WIN32)
    {
        if (delay > INT32_MAX)
            delay = INT32_MAX;
        Sleep(delay);
        ret = 0;
    }
#else
    {
        struct timespec ts;

        ts.tv_sec = delay / 1000;
        ts.tv_nsec = (delay % 1000) * 1000000;
        ret = mbjs_get_errno(nanosleep(&ts, NULL));
    }
#endif
    return MBJS_NewInt32(ctx, ret);
}

#if defined(_WIN32)
static char *realpath(const char *path, char *buf)
{
    if (!_fullpath(buf, path, MBJS__PATH_MAX)) {
        errno = ENOENT;
        return NULL;
    } else {
        return buf;
    }
}
#endif

#if !defined(__wasi__)
/* return [path, errorcode] */
static MBJSValue mbjs_os_realpath(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    const char *path;
    char buf[MBJS__PATH_MAX], *res;
    int err;

    path = MBJS_ToCString(ctx, argv[0]);
    if (!path)
        return MBJS_EXCEPTION;
    res = realpath(path, buf);
    MBJS_FreeCString(ctx, path);
    if (!res) {
        buf[0] = '\0';
        err = errno;
    } else {
        err = 0;
    }
    return make_string_error(ctx, buf, err);
}
#endif

#if !defined(_WIN32) && !defined(__wasi__) && !(defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH))
static MBJSValue mbjs_os_symlink(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    const char *target, *linkpath;
    int err;

    target = MBJS_ToCString(ctx, argv[0]);
    if (!target)
        return MBJS_EXCEPTION;
    linkpath = MBJS_ToCString(ctx, argv[1]);
    if (!linkpath) {
        MBJS_FreeCString(ctx, target);
        return MBJS_EXCEPTION;
    }
    err = mbjs_get_errno(symlink(target, linkpath));
    MBJS_FreeCString(ctx, target);
    MBJS_FreeCString(ctx, linkpath);
    return MBJS_NewInt32(ctx, err);
}

/* return [path, errorcode] */
static MBJSValue mbjs_os_readlink(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    const char *path;
    char buf[MBJS__PATH_MAX];
    int err;
    ssize_t res;

    path = MBJS_ToCString(ctx, argv[0]);
    if (!path)
        return MBJS_EXCEPTION;
    res = readlink(path, buf, sizeof(buf) - 1);
    if (res < 0) {
        buf[0] = '\0';
        err = errno;
    } else {
        buf[res] = '\0';
        err = 0;
    }
    MBJS_FreeCString(ctx, path);
    return make_string_error(ctx, buf, err);
}

static char **build_envp(MBJSContext *ctx, MBJSValue obj)
{
    uint32_t len, i;
    MBJSPropertyEnum *tab;
    char **envp, *pair;
    const char *key, *str;
    MBJSValue val;
    size_t key_len, str_len;

    if (MBJS_GetOwnPropertyNames(ctx, &tab, &len, obj,
                               MBJS_GPN_STRING_MASK | MBJS_GPN_ENUM_ONLY) < 0)
        return NULL;
    envp = mbjs_mallocz(ctx, sizeof(envp[0]) * ((size_t)len + 1));
    if (!envp)
        goto fail;
    for(i = 0; i < len; i++) {
        val = MBJS_GetProperty(ctx, obj, tab[i].atom);
        if (MBJS_IsException(val))
            goto fail;
        str = MBJS_ToCString(ctx, val);
        MBJS_FreeValue(ctx, val);
        if (!str)
            goto fail;
        key = MBJS_AtomToCString(ctx, tab[i].atom);
        if (!key) {
            MBJS_FreeCString(ctx, str);
            goto fail;
        }
        key_len = strlen(key);
        str_len = strlen(str);
        pair = mbjs_malloc(ctx, key_len + str_len + 2);
        if (!pair) {
            MBJS_FreeCString(ctx, key);
            MBJS_FreeCString(ctx, str);
            goto fail;
        }
        memcpy(pair, key, key_len);
        pair[key_len] = '=';
        memcpy(pair + key_len + 1, str, str_len);
        pair[key_len + 1 + str_len] = '\0';
        envp[i] = pair;
        MBJS_FreeCString(ctx, key);
        MBJS_FreeCString(ctx, str);
    }
 done:
    for(i = 0; i < len; i++)
        MBJS_FreeAtom(ctx, tab[i].atom);
    mbjs_free(ctx, tab);
    return envp;
 fail:
    if (envp) {
        for(i = 0; i < len; i++)
            mbjs_free(ctx, envp[i]);
        mbjs_free(ctx, envp);
        envp = NULL;
    }
    goto done;
}

/* execvpe is not available on non GNU systems */
static int my_execvpe(const char *filename, char **argv, char **envp)
{
    char *path, *p, *p_next, *p1;
    char buf[MBJS__PATH_MAX];
    size_t filename_len, path_len;
    bool eacces_error;

    filename_len = strlen(filename);
    if (filename_len == 0) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(filename, '/'))
        return execve(filename, argv, envp);

    path = getenv("PATH");
    if (!path)
        path = (char *)"/bin:/usr/bin";
    eacces_error = false;
    p = path;
    for(p = path; p != NULL; p = p_next) {
        p1 = strchr(p, ':');
        if (!p1) {
            p_next = NULL;
            path_len = strlen(p);
        } else {
            p_next = p1 + 1;
            path_len = p1 - p;
        }
        /* path too long */
        if ((path_len + 1 + filename_len + 1) > MBJS__PATH_MAX)
            continue;
        memcpy(buf, p, path_len);
        buf[path_len] = '/';
        memcpy(buf + path_len + 1, filename, filename_len);
        buf[path_len + 1 + filename_len] = '\0';

        execve(buf, argv, envp);

        switch(errno) {
        case EACCES:
            eacces_error = true;
            break;
        case ENOENT:
        case ENOTDIR:
            break;
        default:
            return -1;
        }
    }
    if (eacces_error)
        errno = EACCES;
    return -1;
}

static void (*mbjs_os_exec_closefrom)(int);

#if !defined(EMSCRIPTEN) && !defined(__wasi__)

static mbjs_once_t mbjs_os_exec_once = MBJS_ONCE_INIT;

static void mbjs_os_exec_once_init(void)
{
     *(void **) (&mbjs_os_exec_closefrom) = dlsym(RTLD_DEFAULT, "closefrom");
}

#endif

/* exec(args[, options]) -> exitcode */
static MBJSValue mbjs_os_exec(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv)
{
    MBJSValueConst options, args = argv[0];
    MBJSValue val, ret_val;
    const char **exec_argv, *file = NULL, *str, *cwd = NULL;
    char **envp = environ;
    uint32_t exec_argc, i;
    int ret, pid, status;
    bool block_flag = true, use_path = true;
    static const char *std_name[3] = { "stdin", "stdout", "stderr" };
    int std_fds[3];
    uint32_t uid = -1, gid = -1;
    int ngroups = -1;
    gid_t groups[64];

    val = MBJS_GetPropertyStr(ctx, args, "length");
    if (MBJS_IsException(val))
        return MBJS_EXCEPTION;
    ret = MBJS_ToUint32(ctx, &exec_argc, val);
    MBJS_FreeValue(ctx, val);
    if (ret)
        return MBJS_EXCEPTION;
    /* arbitrary limit to avoid overflow */
    if (exec_argc < 1 || exec_argc > 65535) {
        return MBJS_ThrowTypeError(ctx, "invalid number of arguments");
    }
    exec_argv = mbjs_mallocz(ctx, sizeof(exec_argv[0]) * (exec_argc + 1));
    if (!exec_argv)
        return MBJS_EXCEPTION;
    for(i = 0; i < exec_argc; i++) {
        val = MBJS_GetPropertyUint32(ctx, args, i);
        if (MBJS_IsException(val))
            goto exception;
        str = MBJS_ToCString(ctx, val);
        MBJS_FreeValue(ctx, val);
        if (!str)
            goto exception;
        exec_argv[i] = str;
    }
    exec_argv[exec_argc] = NULL;

    for(i = 0; i < 3; i++)
        std_fds[i] = i;

    /* get the options, if any */
    if (argc >= 2) {
        options = argv[1];

        if (get_bool_option(ctx, &block_flag, options, "block"))
            goto exception;
        if (get_bool_option(ctx, &use_path, options, "usePath"))
            goto exception;

        val = MBJS_GetPropertyStr(ctx, options, "file");
        if (MBJS_IsException(val))
            goto exception;
        if (!MBJS_IsUndefined(val)) {
            file = MBJS_ToCString(ctx, val);
            MBJS_FreeValue(ctx, val);
            if (!file)
                goto exception;
        }

        val = MBJS_GetPropertyStr(ctx, options, "cwd");
        if (MBJS_IsException(val))
            goto exception;
        if (!MBJS_IsUndefined(val)) {
            cwd = MBJS_ToCString(ctx, val);
            MBJS_FreeValue(ctx, val);
            if (!cwd)
                goto exception;
        }

        /* stdin/stdout/stderr handles */
        for(i = 0; i < 3; i++) {
            val = MBJS_GetPropertyStr(ctx, options, std_name[i]);
            if (MBJS_IsException(val))
                goto exception;
            if (!MBJS_IsUndefined(val)) {
                int fd;
                ret = MBJS_ToInt32(ctx, &fd, val);
                MBJS_FreeValue(ctx, val);
                if (ret)
                    goto exception;
                std_fds[i] = fd;
            }
        }

        val = MBJS_GetPropertyStr(ctx, options, "env");
        if (MBJS_IsException(val))
            goto exception;
        if (!MBJS_IsUndefined(val)) {
            envp = build_envp(ctx, val);
            MBJS_FreeValue(ctx, val);
            if (!envp)
                goto exception;
        }

        val = MBJS_GetPropertyStr(ctx, options, "uid");
        if (MBJS_IsException(val))
            goto exception;
        if (!MBJS_IsUndefined(val)) {
            ret = MBJS_ToUint32(ctx, &uid, val);
            MBJS_FreeValue(ctx, val);
            if (ret)
                goto exception;
        }

        val = MBJS_GetPropertyStr(ctx, options, "gid");
        if (MBJS_IsException(val))
            goto exception;
        if (!MBJS_IsUndefined(val)) {
            ret = MBJS_ToUint32(ctx, &gid, val);
            MBJS_FreeValue(ctx, val);
            if (ret)
                goto exception;
        }

        val = MBJS_GetPropertyStr(ctx, options, "groups");
        if (MBJS_IsException(val))
            goto exception;
        if (!MBJS_IsUndefined(val)) {
            int64_t idx, len;
            MBJSValue prop;
            uint32_t id;
            ngroups = 0;
            if (MBJS_GetLength(ctx, val, &len)) {
                MBJS_FreeValue(ctx, val);
                goto exception;
            }
            for (idx = 0; idx < len; idx++) {
                prop = MBJS_GetPropertyInt64(ctx, val, idx);
                if (MBJS_IsException(prop))
                    break;
                if (MBJS_IsUndefined(prop))
                    continue;
                ret = MBJS_ToUint32(ctx, &id, prop);
                MBJS_FreeValue(ctx, prop);
                if (ret)
                    break;
                if (ngroups == countof(groups)) {
                    MBJS_ThrowRangeError(ctx, "too many groups");
                    break;
                }
                groups[ngroups++] = id;
            }
            MBJS_FreeValue(ctx, val);
            if (idx < len)
                goto exception;
        }

    }

#if !defined(EMSCRIPTEN) && !defined(__wasi__)
    // should happen pre-fork because it calls dlsym()
    // and that's not an async-signal-safe function
    mbjs_once(&mbjs_os_exec_once, mbjs_os_exec_once_init);
#endif

    pid = fork();
    if (pid < 0) {
        MBJS_ThrowTypeError(ctx, "fork error");
        goto exception;
    }
    if (pid == 0) {
        /* child */
        /* remap the stdin/stdout/stderr handles if necessary */
        for(i = 0; i < 3; i++) {
            if (std_fds[i] != i) {
                if (dup2(std_fds[i], i) < 0)
                    _exit(127);
            }
        }

        if (mbjs_os_exec_closefrom) {
            mbjs_os_exec_closefrom(3);
        } else {
            int fd_max = sysconf(_SC_OPEN_MAX);
            for(i = 3; i < fd_max; i++)
                close(i);
        }

        if (cwd) {
            if (chdir(cwd) < 0)
                _exit(127);
        }
        if (ngroups != -1) {
            if (setgroups(ngroups, groups) < 0)
                _exit(127);
        }
        if (uid != -1) {
            if (setuid(uid) < 0)
                _exit(127);
        }
        if (gid != -1) {
            if (setgid(gid) < 0)
                _exit(127);
        }

        if (!file)
            file = exec_argv[0];
        if (use_path)
            ret = my_execvpe(file, (char **)exec_argv, envp);
        else
            ret = execve(file, (char **)exec_argv, envp);
        _exit(127);
    }
    /* parent */
    if (block_flag) {
        for(;;) {
            ret = waitpid(pid, &status, 0);
            if (ret == pid) {
                if (WIFEXITED(status)) {
                    ret = WEXITSTATUS(status);
                    break;
                } else if (WIFSIGNALED(status)) {
                    ret = -WTERMSIG(status);
                    break;
                }
            }
        }
    } else {
        ret = pid;
    }
    ret_val = MBJS_NewInt32(ctx, ret);
 done:
    MBJS_FreeCString(ctx, file);
    MBJS_FreeCString(ctx, cwd);
    for(i = 0; i < exec_argc; i++)
        MBJS_FreeCString(ctx, exec_argv[i]);
    mbjs_free(ctx, exec_argv);
    if (envp != environ) {
        char **p;
        p = envp;
        while (*p != NULL) {
            mbjs_free(ctx, *p);
            p++;
        }
        mbjs_free(ctx, envp);
    }
    return ret_val;
 exception:
    ret_val = MBJS_EXCEPTION;
    goto done;
}

/* getpid() -> pid */
static MBJSValue mbjs_os_getpid(MBJSContext *ctx, MBJSValueConst this_val,
                            int argc, MBJSValueConst *argv)
{
    return MBJS_NewInt32(ctx, getpid());
}

/* waitpid(pid, block) -> [pid, status] */
static MBJSValue mbjs_os_waitpid(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    int pid, status, options, ret;
    MBJSValue obj;

    if (MBJS_ToInt32(ctx, &pid, argv[0]))
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &options, argv[1]))
        return MBJS_EXCEPTION;

    ret = waitpid(pid, &status, options);
    if (ret < 0) {
        ret = -errno;
        status = 0;
    }

    obj = MBJS_NewArray(ctx);
    if (MBJS_IsException(obj))
        return obj;
    MBJS_DefinePropertyValueUint32(ctx, obj, 0, MBJS_NewInt32(ctx, ret),
                                 MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueUint32(ctx, obj, 1, MBJS_NewInt32(ctx, status),
                                 MBJS_PROP_C_W_E);
    return obj;
}

/* pipe() -> [read_fd, write_fd] or null if error */
static MBJSValue mbjs_os_pipe(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv)
{
    int pipe_fds[2], ret;
    MBJSValue obj;

    ret = pipe(pipe_fds);
    if (ret < 0)
        return MBJS_NULL;
    obj = MBJS_NewArray(ctx);
    if (MBJS_IsException(obj))
        return obj;
    MBJS_DefinePropertyValueUint32(ctx, obj, 0, MBJS_NewInt32(ctx, pipe_fds[0]),
                                 MBJS_PROP_C_W_E);
    MBJS_DefinePropertyValueUint32(ctx, obj, 1, MBJS_NewInt32(ctx, pipe_fds[1]),
                                 MBJS_PROP_C_W_E);
    return obj;
}

/* kill(pid, sig) */
static MBJSValue mbjs_os_kill(MBJSContext *ctx, MBJSValueConst this_val,
                          int argc, MBJSValueConst *argv)
{
    int pid, sig, ret;

    if (MBJS_ToInt32(ctx, &pid, argv[0]))
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &sig, argv[1]))
        return MBJS_EXCEPTION;
    ret = mbjs_get_errno(kill(pid, sig));
    return MBJS_NewInt32(ctx, ret);
}

/* dup(fd) */
static MBJSValue mbjs_os_dup(MBJSContext *ctx, MBJSValueConst this_val,
                         int argc, MBJSValueConst *argv)
{
    int fd, ret;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    ret = mbjs_get_errno(dup(fd));
    return MBJS_NewInt32(ctx, ret);
}

/* dup2(fd) */
static MBJSValue mbjs_os_dup2(MBJSContext *ctx, MBJSValueConst this_val,
                         int argc, MBJSValueConst *argv)
{
    int fd, fd2, ret;

    if (MBJS_ToInt32(ctx, &fd, argv[0]))
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &fd2, argv[1]))
        return MBJS_EXCEPTION;
    ret = mbjs_get_errno(dup2(fd, fd2));
    return MBJS_NewInt32(ctx, ret);
}

#endif /* !_WIN32 */

#ifdef USE_WORKER

/* Worker */

typedef struct {
    MBJSWorkerMessagePipe *recv_pipe;
    MBJSWorkerMessagePipe *send_pipe;
    MBJSWorkerMessageHandler *msg_handler;
} MBJSWorkerData;

typedef struct {
    char *filename; /* module filename */
    char *basename; /* module base name */
    MBJSWorkerMessagePipe *recv_pipe, *send_pipe;
} WorkerFuncArgs;

typedef struct {
    int ref_count;
    uint64_t buf[];
} MBJSSABHeader;

static MBJSRuntime *(*mbjs_worker_new_runtime_func)(void);
static MBJSContext *(*mbjs_worker_new_context_func)(MBJSRuntime *rt);

static int atomic_add_int(int *ptr, int v)
{
    return atomic_fetch_add((_Atomic uint32_t*)ptr, v) + v;
}

/* shared array buffer allocator */
static void *mbjs_sab_alloc(void *opaque, size_t size)
{
    MBJSSABHeader *sab;
    sab = malloc(sizeof(MBJSSABHeader) + size);
    if (!sab)
        return NULL;
    sab->ref_count = 1;
    return sab->buf;
}

static void mbjs_sab_free(void *opaque, void *ptr)
{
    MBJSSABHeader *sab;
    int ref_count;
    sab = (MBJSSABHeader *)((uint8_t *)ptr - sizeof(MBJSSABHeader));
    ref_count = atomic_add_int(&sab->ref_count, -1);
    assert(ref_count >= 0);
    if (ref_count == 0) {
        free(sab);
    }
}

static void mbjs_sab_dup(void *opaque, void *ptr)
{
    MBJSSABHeader *sab;
    sab = (MBJSSABHeader *)((uint8_t *)ptr - sizeof(MBJSSABHeader));
    atomic_add_int(&sab->ref_count, 1);
}

static MBJSWorkerMessagePipe *mbjs_new_message_pipe(void)
{
    MBJSWorkerMessagePipe *ps;

    ps = malloc(sizeof(*ps));
    if (!ps)
        return NULL;
    if (mbjs_waker_init(&ps->waker)) {
        free(ps);
        return NULL;
    }
    ps->ref_count = 1;
    init_list_head(&ps->msg_queue);
    mbjs_mutex_init(&ps->mutex);
    return ps;
}

static MBJSWorkerMessagePipe *mbjs_dup_message_pipe(MBJSWorkerMessagePipe *ps)
{
    atomic_add_int(&ps->ref_count, 1);
    return ps;
}

static void mbjs_free_message(MBJSWorkerMessage *msg)
{
    size_t i;
    /* free the SAB */
    for(i = 0; i < msg->sab_tab_len; i++) {
        mbjs_sab_free(NULL, msg->sab_tab[i]);
    }
    free(msg->sab_tab);
    free(msg->data);
    free(msg);
}

static void mbjs_free_message_pipe(MBJSWorkerMessagePipe *ps)
{
    struct list_head *el, *el1;
    MBJSWorkerMessage *msg;
    int ref_count;

    if (!ps)
        return;

    ref_count = atomic_add_int(&ps->ref_count, -1);
    assert(ref_count >= 0);
    if (ref_count == 0) {
        list_for_each_safe(el, el1, &ps->msg_queue) {
            msg = list_entry(el, MBJSWorkerMessage, link);
            mbjs_free_message(msg);
        }
        mbjs_mutex_destroy(&ps->mutex);
        mbjs_waker_close(&ps->waker);
        free(ps);
    }
}

static void mbjs_free_port(MBJSRuntime *rt, MBJSWorkerMessageHandler *port)
{
    if (port) {
        mbjs_free_message_pipe(port->recv_pipe);
        MBJS_FreeValueRT(rt, port->on_message_func);
        list_del(&port->link);
        mbjs_free_rt(rt, port);
    }
}

static void mbjs_worker_finalizer(MBJSRuntime *rt, MBJSValueConst val)
{
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSWorkerData *worker = MBJS_GetOpaque(val, ts->worker_class_id);
    if (worker) {
        mbjs_free_message_pipe(worker->recv_pipe);
        mbjs_free_message_pipe(worker->send_pipe);
        mbjs_free_port(rt, worker->msg_handler);
        mbjs_free_rt(rt, worker);
    }
}

static MBJSClassDef mbjs_worker_class = {
    "Worker",
    .finalizer = mbjs_worker_finalizer,
};

static void worker_func(void *opaque)
{
    MBJSRuntime *(*new_runtime_func)(void);
    MBJSContext *(*new_context_func)(MBJSRuntime *);
    WorkerFuncArgs *args = opaque;
    MBJSRuntime *rt;
    MBJSThreadState *ts;
    MBJSContext *ctx;
    MBJSValue val;

    new_runtime_func = mbjs_worker_new_runtime_func;
    if (!new_runtime_func)
        new_runtime_func = MBJS_NewRuntime;
    rt = new_runtime_func();
    if (rt == NULL) {
        fprintf(stderr, "MBJS_NewRuntime failure");
        exit(1);
    }
    mbjs_std_init_handlers(rt);

    MBJS_SetModuleLoaderFunc2(rt, NULL, mbjs_module_loader, mbjs_module_check_attributes, NULL);

    /* set the pipe to communicate with the parent */
    ts = mbjs_get_thread_state(rt);
    ts->recv_pipe = args->recv_pipe;
    ts->send_pipe = args->send_pipe;

    /* function pointer to avoid linking the whole MBJS_NewContext() if
       not needed */
    new_context_func = mbjs_worker_new_context_func;
    if (!new_context_func)
        new_context_func = MBJS_NewContext;
    ctx = new_context_func(rt);
    if (ctx == NULL) {
        fprintf(stderr, "MBJS_NewContext failure");
        exit(1);
    }

    MBJS_SetCanBlock(rt, true);

    mbjs_std_add_helpers(ctx, -1, NULL);

    val = MBJS_LoadModule(ctx, args->basename, args->filename, MBJS_UNDEFINED);
    free(args->filename);
    free(args->basename);
    free(args);
    val = mbjs_std_await(ctx, val);
    if (MBJS_IsException(val))
        mbjs_std_dump_error(ctx);
    MBJS_FreeValue(ctx, val);

    mbjs_std_loop(ctx);

    mbjs_std_free_handlers(rt);
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
}

static MBJSValue mbjs_worker_ctor_internal(MBJSContext *ctx, MBJSValueConst new_target,
                                       MBJSWorkerMessagePipe *recv_pipe,
                                       MBJSWorkerMessagePipe *send_pipe)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSValue obj = MBJS_UNDEFINED, proto;
    MBJSWorkerData *s;

    /* create the object */
    if (MBJS_IsUndefined(new_target)) {
        proto = MBJS_GetClassProto(ctx, ts->worker_class_id);
    } else {
        proto = MBJS_GetPropertyStr(ctx, new_target, "prototype");
        if (MBJS_IsException(proto))
            goto fail;
    }
    obj = MBJS_NewObjectProtoClass(ctx, proto, ts->worker_class_id);
    MBJS_FreeValue(ctx, proto);
    if (MBJS_IsException(obj))
        goto fail;
    s = mbjs_mallocz(ctx, sizeof(*s));
    if (!s)
        goto fail;
    s->recv_pipe = mbjs_dup_message_pipe(recv_pipe);
    s->send_pipe = mbjs_dup_message_pipe(send_pipe);

    MBJS_SetOpaque(obj, s);
    return obj;
 fail:
    MBJS_FreeValue(ctx, obj);
    return MBJS_EXCEPTION;
}

static MBJSValue mbjs_worker_ctor(MBJSContext *ctx, MBJSValueConst new_target,
                              int argc, MBJSValueConst *argv)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    WorkerFuncArgs *args = NULL;
    mbjs_thread_t thr;
    MBJSValue obj = MBJS_UNDEFINED;
    int ret;
    const char *filename = NULL, *basename;
    MBJSAtom basename_atom;

    /* XXX: in order to avoid problems with resource liberation, we
       don't support creating workers inside workers */
    if (!is_main_thread(rt))
        return MBJS_ThrowTypeError(ctx, "cannot create a worker inside a worker");

    /* base name, assuming the calling function is a normal JS
       function */
    basename_atom = MBJS_GetScriptOrModuleName(ctx, 1);
    if (basename_atom == MBJS_ATOM_NULL) {
        return MBJS_ThrowTypeError(ctx, "could not determine calling script or module name");
    }
    basename = MBJS_AtomToCString(ctx, basename_atom);
    MBJS_FreeAtom(ctx, basename_atom);
    if (!basename)
        goto fail;

    /* module name */
    filename = MBJS_ToCString(ctx, argv[0]);
    if (!filename)
        goto fail;

    args = malloc(sizeof(*args));
    if (!args)
        goto oom_fail;
    memset(args, 0, sizeof(*args));
    args->filename = strdup(filename);
    args->basename = strdup(basename);

    /* ports */
    args->recv_pipe = mbjs_new_message_pipe();
    if (!args->recv_pipe)
        goto oom_fail;
    args->send_pipe = mbjs_new_message_pipe();
    if (!args->send_pipe)
        goto oom_fail;

    obj = mbjs_worker_ctor_internal(ctx, new_target,
                                  args->send_pipe, args->recv_pipe);
    if (MBJS_IsException(obj))
        goto fail;

    ret = mbjs_thread_create(&thr, worker_func, args, MBJS_THREAD_CREATE_DETACHED);
    if (ret != 0) {
        MBJS_ThrowTypeError(ctx, "could not create worker");
        goto fail;
    }
    MBJS_FreeCString(ctx, basename);
    MBJS_FreeCString(ctx, filename);
    return obj;
 oom_fail:
    MBJS_ThrowOutOfMemory(ctx);
 fail:
    MBJS_FreeCString(ctx, basename);
    MBJS_FreeCString(ctx, filename);
    if (args) {
        free(args->filename);
        free(args->basename);
        mbjs_free_message_pipe(args->recv_pipe);
        mbjs_free_message_pipe(args->send_pipe);
        free(args);
    }
    MBJS_FreeValue(ctx, obj);
    return MBJS_EXCEPTION;
}

static MBJSValue mbjs_worker_postMessage(MBJSContext *ctx, MBJSValueConst this_val,
                                     int argc, MBJSValueConst *argv)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSWorkerData *worker = MBJS_GetOpaque2(ctx, this_val, ts->worker_class_id);
    MBJSWorkerMessagePipe *ps;
    size_t data_len, i;
    uint8_t *data;
    MBJSWorkerMessage *msg;
    MBJSSABTab sab_tab;

    if (!worker)
        return MBJS_EXCEPTION;

    data = MBJS_WriteObject2(ctx, &data_len, argv[0],
                           MBJS_WRITE_OBJ_SAB | MBJS_WRITE_OBJ_REFERENCE,
                           &sab_tab);
    if (!data)
        return MBJS_EXCEPTION;

    msg = malloc(sizeof(*msg));
    if (!msg)
        goto fail;
    msg->data = NULL;
    msg->sab_tab = NULL;

    /* must reallocate because the allocator may be different */
    msg->data = malloc(data_len);
    if (!msg->data)
        goto fail;
    memcpy(msg->data, data, data_len);
    msg->data_len = data_len;

    if (sab_tab.len > 0) {
        msg->sab_tab = malloc(sizeof(msg->sab_tab[0]) * sab_tab.len);
        if (!msg->sab_tab)
            goto fail;
        memcpy(msg->sab_tab, sab_tab.tab, sizeof(msg->sab_tab[0]) * sab_tab.len);
    }
    msg->sab_tab_len = sab_tab.len;

    mbjs_free(ctx, data);
    mbjs_free(ctx, sab_tab.tab);

    /* increment the SAB reference counts */
    for(i = 0; i < msg->sab_tab_len; i++) {
        mbjs_sab_dup(NULL, msg->sab_tab[i]);
    }

    ps = worker->send_pipe;
    mbjs_mutex_lock(&ps->mutex);
    /* indicate that data is present */
    if (list_empty(&ps->msg_queue))
        mbjs_waker_signal(&ps->waker);
    list_add_tail(&msg->link, &ps->msg_queue);
    mbjs_mutex_unlock(&ps->mutex);
    return MBJS_UNDEFINED;
 fail:
    if (msg) {
        free(msg->data);
        free(msg->sab_tab);
        free(msg);
    }
    mbjs_free(ctx, data);
    mbjs_free(ctx, sab_tab.tab);
    return MBJS_EXCEPTION;

}

static MBJSValue mbjs_worker_set_onmessage(MBJSContext *ctx, MBJSValueConst this_val,
                                       MBJSValueConst func)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSWorkerData *worker = MBJS_GetOpaque2(ctx, this_val, ts->worker_class_id);
    MBJSWorkerMessageHandler *port;

    if (!worker)
        return MBJS_EXCEPTION;

    port = worker->msg_handler;
    if (MBJS_IsNull(func)) {
        if (port) {
            mbjs_free_port(rt, port);
            worker->msg_handler = NULL;
        }
    } else {
        if (!MBJS_IsFunction(ctx, func))
            return MBJS_ThrowTypeError(ctx, "not a function");
        if (!port) {
            port = mbjs_mallocz(ctx, sizeof(*port));
            if (!port)
                return MBJS_EXCEPTION;
            port->recv_pipe = mbjs_dup_message_pipe(worker->recv_pipe);
            port->on_message_func = MBJS_NULL;
            list_add_tail(&port->link, &ts->port_list);
            worker->msg_handler = port;
        }
        MBJS_FreeValue(ctx, port->on_message_func);
        port->on_message_func = MBJS_DupValue(ctx, func);
    }
    return MBJS_UNDEFINED;
}

static MBJSValue mbjs_worker_get_onmessage(MBJSContext *ctx, MBJSValueConst this_val)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSWorkerData *worker = MBJS_GetOpaque2(ctx, this_val, ts->worker_class_id);
    MBJSWorkerMessageHandler *port;
    if (!worker)
        return MBJS_EXCEPTION;
    port = worker->msg_handler;
    if (port) {
        return MBJS_DupValue(ctx, port->on_message_func);
    } else {
        return MBJS_NULL;
    }
}

static const MBJSCFunctionListEntry mbjs_worker_proto_funcs[] = {
    MBJS_CFUNC_DEF("postMessage", 1, mbjs_worker_postMessage ),
    MBJS_CGETSET_DEF("onmessage", mbjs_worker_get_onmessage, mbjs_worker_set_onmessage ),
};

#endif /* USE_WORKER */

void mbjs_std_set_worker_new_runtime_func(MBJSRuntime *(*func)(void))
{
#ifdef USE_WORKER
    mbjs_worker_new_runtime_func = func;
#endif
}

void mbjs_std_set_worker_new_context_func(MBJSContext *(*func)(MBJSRuntime *rt))
{
#ifdef USE_WORKER
    mbjs_worker_new_context_func = func;
#endif
}

#if defined(_WIN32)
#define OS_PLATFORM "win32"
#elif defined(__APPLE__)
#define OS_PLATFORM "darwin"
#elif defined(EMSCRIPTEN)
#define OS_PLATFORM "js"
#elif defined(__CYGWIN__)
#define OS_PLATFORM "cygwin"
#elif defined(__linux__)
#define OS_PLATFORM "linux"
#elif defined(__OpenBSD__)
#define OS_PLATFORM "openbsd"
#elif defined(__NetBSD__)
#define OS_PLATFORM "netbsd"
#elif defined(__FreeBSD__)
#define OS_PLATFORM "freebsd"
#elif defined(__wasi__)
#define OS_PLATFORM "wasi"
#elif defined(__GNU__)
#define OS_PLATFORM "hurd"
#else
#define OS_PLATFORM "unknown"
#endif

#define OS_FLAG(x) MBJS_PROP_INT32_DEF(#x, x, MBJS_PROP_CONFIGURABLE )

static const MBJSCFunctionListEntry mbjs_os_funcs[] = {
    MBJS_CFUNC_DEF("open", 2, mbjs_os_open ),
    OS_FLAG(O_RDONLY),
    OS_FLAG(O_WRONLY),
    OS_FLAG(O_RDWR),
    OS_FLAG(O_APPEND),
    OS_FLAG(O_CREAT),
    OS_FLAG(O_EXCL),
    OS_FLAG(O_TRUNC),
#if defined(_WIN32)
    OS_FLAG(O_BINARY),
    OS_FLAG(O_TEXT),
#endif
    MBJS_CFUNC_DEF("close", 1, mbjs_os_close ),
    MBJS_CFUNC_DEF("seek", 3, mbjs_os_seek ),
    MBJS_CFUNC_MAGIC_DEF("read", 4, mbjs_os_read_write, 0 ),
    MBJS_CFUNC_MAGIC_DEF("write", 4, mbjs_os_read_write, 1 ),
    MBJS_CFUNC_DEF("isatty", 1, mbjs_os_isatty ),
#if !defined(__wasi__)
    MBJS_CFUNC_DEF("ttyGetWinSize", 1, mbjs_os_ttyGetWinSize ),
    MBJS_CFUNC_DEF("ttySetRaw", 1, mbjs_os_ttySetRaw ),
#endif
    MBJS_CFUNC_DEF("remove", 1, mbjs_os_remove ),
    MBJS_CFUNC_DEF("rename", 2, mbjs_os_rename ),
    MBJS_CFUNC_MAGIC_DEF("setReadHandler", 2, mbjs_os_setReadHandler, 0 ),
    MBJS_CFUNC_MAGIC_DEF("setWriteHandler", 2, mbjs_os_setReadHandler, 1 ),
    MBJS_CFUNC_DEF("signal", 2, mbjs_os_signal ),
    OS_FLAG(SIGINT),
    OS_FLAG(SIGABRT),
    OS_FLAG(SIGFPE),
    OS_FLAG(SIGILL),
    OS_FLAG(SIGSEGV),
    OS_FLAG(SIGTERM),
#if !defined(_WIN32) && !defined(__wasi__)
    OS_FLAG(SIGQUIT),
    OS_FLAG(SIGPIPE),
    OS_FLAG(SIGALRM),
    OS_FLAG(SIGUSR1),
    OS_FLAG(SIGUSR2),
    OS_FLAG(SIGCHLD),
    OS_FLAG(SIGCONT),
    OS_FLAG(SIGSTOP),
    OS_FLAG(SIGTSTP),
    OS_FLAG(SIGTTIN),
    OS_FLAG(SIGTTOU),
    MBJS_CFUNC_DEF("cputime", 0, mbjs_os_cputime ),
#endif
    MBJS_CFUNC_DEF("exePath", 0, mbjs_os_exepath ),
    MBJS_CFUNC_DEF("now", 0, mbjs_os_now ),
    MBJS_CFUNC_DEF("memoryInfo", 0, mbjs_os_memoryInfo ),
    MBJS_CFUNC_DEF("gc", 0, mbjs_os_gc ),
    MBJS_CFUNC_DEF("setGCThreshold", 1, mbjs_os_setGCThreshold ),
    MBJS_CFUNC_MAGIC_DEF("setTimeout", 2, mbjs_os_setTimeout, 0 ),
    MBJS_CFUNC_MAGIC_DEF("setInterval", 2, mbjs_os_setTimeout, 1 ),
    // per spec: both functions can cancel timeouts and intervals
    MBJS_CFUNC_DEF("clearTimeout", 1, mbjs_os_clearTimeout ),
    MBJS_CFUNC_DEF("clearInterval", 1, mbjs_os_clearTimeout ),
    MBJS_CFUNC_DEF("sleepAsync", 1, mbjs_os_sleepAsync ),
    MBJS_PROP_STRING_DEF("platform", OS_PLATFORM, 0 ),
    MBJS_CFUNC_DEF("getcwd", 0, mbjs_os_getcwd ),
    MBJS_CFUNC_DEF("chdir", 0, mbjs_os_chdir ),
    MBJS_CFUNC_DEF("mkdir", 1, mbjs_os_mkdir ),
    MBJS_CFUNC_DEF("readdir", 1, mbjs_os_readdir ),
#if !defined(_WIN32) && !defined(__wasi__)
    MBJS_CFUNC_MAGIC_DEF("mkdtemp", 0, mbjs_os_mkdstemp, 'd' ),
    MBJS_CFUNC_MAGIC_DEF("mkstemp", 0, mbjs_os_mkdstemp, 's' ),
#endif
    /* st_mode constants */
    OS_FLAG(S_IFMT),
    OS_FLAG(S_IFIFO),
    OS_FLAG(S_IFCHR),
    OS_FLAG(S_IFDIR),
    OS_FLAG(S_IFBLK),
    OS_FLAG(S_IFREG),
#if !defined(_WIN32)
    OS_FLAG(S_IFSOCK),
    OS_FLAG(S_IFLNK),
    OS_FLAG(S_ISGID),
    OS_FLAG(S_ISUID),
#endif
    MBJS_CFUNC_MAGIC_DEF("stat", 1, mbjs_os_stat, 0 ),
    MBJS_CFUNC_DEF("utimes", 3, mbjs_os_utimes ),
    MBJS_CFUNC_DEF("sleep", 1, mbjs_os_sleep ),
#if !defined(__wasi__)
    MBJS_CFUNC_DEF("realpath", 1, mbjs_os_realpath ),
#endif
#if !defined(_WIN32) && !defined(__wasi__) && !(defined(__APPLE__) && (TARGET_OS_TV || TARGET_OS_WATCH))
    MBJS_CFUNC_MAGIC_DEF("lstat", 1, mbjs_os_stat, 1 ),
    MBJS_CFUNC_DEF("symlink", 2, mbjs_os_symlink ),
    MBJS_CFUNC_DEF("readlink", 1, mbjs_os_readlink ),
    MBJS_CFUNC_DEF("exec", 1, mbjs_os_exec ),
    MBJS_CFUNC_DEF("getpid", 0, mbjs_os_getpid ),
    MBJS_CFUNC_DEF("waitpid", 2, mbjs_os_waitpid ),
    OS_FLAG(WNOHANG),
    MBJS_CFUNC_DEF("pipe", 0, mbjs_os_pipe ),
    MBJS_CFUNC_DEF("kill", 2, mbjs_os_kill ),
    MBJS_CFUNC_DEF("dup", 1, mbjs_os_dup ),
    MBJS_CFUNC_DEF("dup2", 2, mbjs_os_dup2 ),
#endif
};

static int mbjs_os_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);

    ts->can_js_os_poll = true;

#ifdef USE_WORKER
    {
        MBJSValue proto, obj;
        /* Worker class */
        MBJS_NewClassID(rt, &ts->worker_class_id);
        MBJS_NewClass(rt, ts->worker_class_id, &mbjs_worker_class);
        proto = MBJS_NewObject(ctx);
        MBJS_SetPropertyFunctionList(ctx, proto, mbjs_worker_proto_funcs, countof(mbjs_worker_proto_funcs));

        obj = MBJS_NewCFunction2(ctx, mbjs_worker_ctor, "Worker", 1,
                               MBJS_CFUNC_constructor, 0);
        MBJS_SetConstructor(ctx, obj, proto);

        MBJS_SetClassProto(ctx, ts->worker_class_id, proto);

        /* set 'Worker.parent' if necessary */
        if (ts->recv_pipe && ts->send_pipe) {
            MBJS_DefinePropertyValueStr(ctx, obj, "parent",
                                      mbjs_worker_ctor_internal(ctx, MBJS_UNDEFINED, ts->recv_pipe, ts->send_pipe),
                                      MBJS_PROP_C_W_E);
        }

        MBJS_SetModuleExport(ctx, m, "Worker", obj);
    }
#endif /* USE_WORKER */

    return MBJS_SetModuleExportList(ctx, m, mbjs_os_funcs, countof(mbjs_os_funcs));
}

MBJSModuleDef *mbjs_init_module_os(MBJSContext *ctx, const char *module_name)
{
    MBJSModuleDef *m;
    m = MBJS_NewCModule(ctx, module_name, mbjs_os_init);
    if (!m)
        return NULL;
    MBJS_AddModuleExportList(ctx, m, mbjs_os_funcs, countof(mbjs_os_funcs));
#ifdef USE_WORKER
    MBJS_AddModuleExport(ctx, m, "Worker");
#endif
    return m;
}

/**********************************************************/

static MBJSValue mbjs_print(MBJSContext *ctx, MBJSValueConst this_val,
                        int argc, MBJSValueConst *argv)
{
#ifdef _WIN32
    HANDLE handle;
    DWORD mode;
#endif
    const char *s;
    MBJSValueConst v;
    DynBuf b;
    int i;

    dbuf_init(&b);
    for(i = 0; i < argc; i++) {
        v = argv[i];
        s = MBJS_ToCString(ctx, v);
        if (!s && MBJS_IsObject(v)) {
            MBJS_FreeValue(ctx, MBJS_GetException(ctx));
            MBJSValue t = MBJS_ToObjectString(ctx, v);
            s = MBJS_ToCString(ctx, t);
            MBJS_FreeValue(ctx, t);
        }
        if (s) {
            dbuf_printf(&b, "%s%s", &" "[!i], s);
            MBJS_FreeCString(ctx, s);
        } else {
            dbuf_printf(&b, "%s<exception>", &" "[!i]);
            MBJS_FreeValue(ctx, MBJS_GetException(ctx));
        }
    }
    dbuf_putc(&b, '\n');
#ifdef _WIN32
    // use WriteConsoleA with CP_UTF8 for better Unicode handling vis-a-vis
    // the mangling that happens when going through msvcrt's stdio layer,
    // *except* when stdout is redirected to something that is not a console
    handle = (HANDLE)_get_osfhandle(/*STDOUT_FILENO*/1); // don't CloseHandle
    if (GetFileType(handle) != FILE_TYPE_CHAR)
        goto fallback;
    if (!GetConsoleMode(handle, &mode))
        goto fallback;
    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE)
        goto fallback;
    mode = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
    WriteConsoleA(handle, b.buf, b.size, NULL, NULL);
    SetConsoleOutputCP(mode);
    FlushFileBuffers(handle);
    goto done;
fallback:
#endif
    fwrite(b.buf, 1, b.size, stdout);
    fflush(stdout);
    goto done; // avoid unused label warning
done:
    dbuf_free(&b);
    return MBJS_UNDEFINED;
}


int mbjs_load_patches(MBJSContext *ctx, const char *dir)
{
    (void)ctx;
    (void)dir;
    return 0;
}
void mbjs_std_add_helpers(MBJSContext *ctx, int argc, char **argv)
{
    MBJSValue global_obj, console, args;
    int i;

    /* XXX: should these global definitions be enumerable? */
    global_obj = MBJS_GetGlobalObject(ctx);

    console = MBJS_NewObject(ctx);
    MBJS_SetPropertyStr(ctx, console, "log",
                      MBJS_NewCFunction(ctx, mbjs_print, "log", 1));
    MBJS_SetPropertyStr(ctx, global_obj, "console", console);

    /* same methods as the mozilla JS shell */
    if (argc >= 0) {
        args = MBJS_NewArray(ctx);
        for(i = 0; i < argc; i++) {
            MBJS_SetPropertyUint32(ctx, args, i, MBJS_NewString(ctx, argv[i]));
        }
        MBJS_SetPropertyStr(ctx, global_obj, "scriptArgs", args);
    }

    MBJS_SetPropertyStr(ctx, global_obj, "print",
                      MBJS_NewCFunction(ctx, mbjs_print, "print", 1));

    MBJS_FreeValue(ctx, global_obj);
}

static void mbjs_std_finalize(MBJSRuntime *rt, void *arg)
{
    MBJSThreadState *ts = arg;
    mbjs_set_thread_state(rt, NULL);
    mbjs_free_rt(rt, ts);
}

void mbjs_std_init_handlers(MBJSRuntime *rt)
{
    MBJSThreadState *ts;

    ts = mbjs_mallocz_rt(rt, sizeof(*ts));
    if (!ts) {
        fprintf(stderr, "Could not allocate memory for the worker");
        exit(1);
    }
    init_list_head(&ts->os_rw_handlers);
    init_list_head(&ts->os_signal_handlers);
    init_list_head(&ts->os_timers);
    init_list_head(&ts->port_list);
    init_list_head(&ts->rejected_promise_list);

    ts->next_timer_id = 1;

    mbjs_set_thread_state(rt, ts);
    MBJS_AddRuntimeFinalizer(rt, mbjs_std_finalize, ts);

#ifdef USE_WORKER
    /* set the SharedArrayBuffer memory handlers */
    {
        MBJSSharedArrayBufferFunctions sf;
        memset(&sf, 0, sizeof(sf));
        sf.sab_alloc = mbjs_sab_alloc;
        sf.sab_free = mbjs_sab_free;
        sf.sab_dup = mbjs_sab_dup;
        MBJS_SetSharedArrayBufferFunctions(rt, &sf);
    }
#endif
}

static void free_rp(MBJSRuntime *rt, MBJSRejectedPromiseEntry *rp)
{
    list_del(&rp->link);
    MBJS_FreeValueRT(rt, rp->promise);
    MBJS_FreeValueRT(rt, rp->reason);
    mbjs_free_rt(rt, rp);
}

void mbjs_std_free_handlers(MBJSRuntime *rt)
{
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    struct list_head *el, *el1;

    list_for_each_safe(el, el1, &ts->os_rw_handlers) {
        MBJSOSRWHandler *rh = list_entry(el, MBJSOSRWHandler, link);
        free_rw_handler(rt, rh);
    }

    list_for_each_safe(el, el1, &ts->os_signal_handlers) {
        MBJSOSSignalHandler *sh = list_entry(el, MBJSOSSignalHandler, link);
        free_sh(rt, sh);
    }

    list_for_each_safe(el, el1, &ts->os_timers) {
        MBJSOSTimer *th = list_entry(el, MBJSOSTimer, link);
        free_timer(rt, th);
    }

    list_for_each_safe(el, el1, &ts->rejected_promise_list) {
        MBJSRejectedPromiseEntry *rp = list_entry(el, MBJSRejectedPromiseEntry, link);
        free_rp(rt, rp);
    }

#ifdef USE_WORKER
    /* XXX: free port_list ? */
    mbjs_free_message_pipe(ts->recv_pipe);
    mbjs_free_message_pipe(ts->send_pipe);
#endif
}

static void mbjs_dump_obj(MBJSContext *ctx, FILE *f, MBJSValueConst val)
{
    const char *str;

    str = MBJS_ToCString(ctx, val);
    if (str) {
        fprintf(f, "%s\n", str);
        MBJS_FreeCString(ctx, str);
    } else {
        fprintf(f, "[exception]\n");
    }
}

#define MBJS_DUMP_ERROR_MAX_CAUSE_DEPTH 10

static void mbjs_std_dump_error1(MBJSContext *ctx, MBJSValueConst exception_val)
{
    MBJSValue val, current;
    bool is_error;
    int depth;

    current = MBJS_DupValue(ctx, exception_val);
    for (depth = 0; ; depth++) {
        is_error = MBJS_IsError(current);
        mbjs_dump_obj(ctx, stderr, current);
        if (is_error) {
            val = MBJS_GetPropertyStr(ctx, current, "stack");
        } else if (depth == 0) {
            mbjs_std_cmd(/*ErrorBackTrace*/2, ctx, &val);
        } else {
            val = MBJS_UNDEFINED;
        }
        if (!MBJS_IsUndefined(val)) {
            mbjs_dump_obj(ctx, stderr, val);
            MBJS_FreeValue(ctx, val);
        }
        if (!is_error || depth >= MBJS_DUMP_ERROR_MAX_CAUSE_DEPTH)
            break;
        val = MBJS_GetPropertyStr(ctx, current, "cause");
        if (MBJS_IsUndefined(val)) {
            MBJS_FreeValue(ctx, val);
            break;
        }
        fputs("Caused by: ", stderr);
        MBJS_FreeValue(ctx, current);
        current = val;
    }
    MBJS_FreeValue(ctx, current);
}

void mbjs_std_dump_error(MBJSContext *ctx)
{
    MBJSValue exception_val;

    exception_val = MBJS_GetException(ctx);
    mbjs_std_dump_error1(ctx, exception_val);
    MBJS_FreeValue(ctx, exception_val);
}

static MBJSRejectedPromiseEntry *find_rejected_promise(MBJSContext *ctx, MBJSThreadState *ts,
                                                     MBJSValueConst promise)
{
    struct list_head *el;

    list_for_each(el, &ts->rejected_promise_list) {
        MBJSRejectedPromiseEntry *rp = list_entry(el, MBJSRejectedPromiseEntry, link);
        if (MBJS_IsSameValue(ctx, rp->promise, promise))
            return rp;
    }
    return NULL;
}

void mbjs_std_promise_rejection_tracker(MBJSContext *ctx, MBJSValueConst promise,
                                      MBJSValueConst reason,
                                      bool is_handled, void *opaque)
{

    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSRejectedPromiseEntry *rp = find_rejected_promise(ctx, ts, promise);

    if (is_handled) {
        /* the rejection is handled, so the entry can be removed if present */
        if (rp)
            free_rp(rt, rp);
    } else {
        /* add a new entry if needed */
        if (!rp) {
            rp = mbjs_malloc_rt(rt, sizeof(*rp));
            if (rp) {
                rp->promise = MBJS_DupValue(ctx, promise);
                rp->reason = MBJS_DupValue(ctx, reason);
                list_add_tail(&rp->link, &ts->rejected_promise_list);
            }
        }
    }
}

/* check if there are pending promise rejections. It must be done
   asynchrously in case a rejected promise is handled later. Currently
   we do it once the application is about to sleep. It could be done
   more often if needed. */
static void mbjs_std_promise_rejection_check(MBJSContext *ctx)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    struct list_head *el;

    if (unlikely(!list_empty(&ts->rejected_promise_list))) {
        list_for_each(el, &ts->rejected_promise_list) {
            MBJSRejectedPromiseEntry *rp = list_entry(el, MBJSRejectedPromiseEntry, link);
            fprintf(stderr, "Possibly unhandled promise rejection: ");
            mbjs_std_dump_error1(ctx, rp->reason);
            fflush(stderr);
        }
        exit(1);
    }
}

/* main loop which calls the user JS callbacks */
int mbjs_std_loop(MBJSContext *ctx)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSContext *ctx1;
    int err;

    for(;;) {
        /* execute the pending jobs */
        for(;;) {
            err = MBJS_ExecutePendingJob(MBJS_GetRuntime(ctx), &ctx1);
            if (err <= 0) {
                if (err < 0)
                    goto done;
                break;
            }
        }

        mbjs_std_promise_rejection_check(ctx);

        if (!ts->can_js_os_poll || mbjs_os_poll(ctx))
            break;
    }
done:
    return MBJS_HasException(ctx);
}

int mbjs_std_loop_once(MBJSContext *ctx)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSContext *ctx1;
    int err, min_delay;

    /* execute all pending jobs */
    for(;;) {
        err = MBJS_ExecutePendingJob(rt, &ctx1);
        if (err < 0)
            return -2; /* error */
        if (err == 0)
            break;
    }

    /* run at most one expired timer */
    if (mbjs_os_run_timers(rt, ctx, ts, &min_delay) < 0)
        return -2; /* error in timer callback */

    /* check if more work is pending */
    if (MBJS_IsJobPending(rt))
        return 0; /* more microtasks pending */

    if (min_delay == 0)
        return 0; /* timer ready to fire immediately */

    if (min_delay > 0)
        return min_delay; /* next timer delay in ms */

    return -1; /* idle, no pending work */
}

/* Wait for a promise and execute pending jobs while waiting for
   it. Return the promise result or MBJS_EXCEPTION in case of promise
   rejection. */
MBJSValue mbjs_std_await(MBJSContext *ctx, MBJSValue obj)
{
    MBJSRuntime *rt = MBJS_GetRuntime(ctx);
    MBJSThreadState *ts = mbjs_get_thread_state(rt);
    MBJSValue ret;
    int state;

    for(;;) {
        state = MBJS_PromiseState(ctx, obj);
        if (state == MBJS_PROMISE_FULFILLED) {
            ret = MBJS_PromiseResult(ctx, obj);
            MBJS_FreeValue(ctx, obj);
            break;
        } else if (state == MBJS_PROMISE_REJECTED) {
            ret = MBJS_Throw(ctx, MBJS_PromiseResult(ctx, obj));
            MBJS_FreeValue(ctx, obj);
            break;
        } else if (state == MBJS_PROMISE_PENDING) {
            MBJSContext *ctx1;
            int err;
            err = MBJS_ExecutePendingJob(MBJS_GetRuntime(ctx), &ctx1);
            if (err < 0) {
                mbjs_std_dump_error(ctx1);
            }
            if (err == 0)
                mbjs_std_promise_rejection_check(ctx);
            if (ts->can_js_os_poll)
                mbjs_os_poll(ctx);
        } else {
            /* not a promise */
            ret = obj;
            break;
        }
    }
    return ret;
}

void mbjs_std_eval_binary(MBJSContext *ctx, const uint8_t *buf, size_t buf_len,
                        int load_only)
{
    MBJSValue obj, val;
    obj = MBJS_ReadObject(ctx, buf, buf_len, MBJS_READ_OBJ_BYTECODE);
    if (MBJS_IsException(obj))
        goto exception;
    if (load_only) {
        if (MBJS_VALUE_GET_TAG(obj) == MBJS_TAG_MODULE) {
            if (mbjs_module_set_import_meta(ctx, obj, false, false) < 0)
                goto exception;
        }
        MBJS_FreeValue(ctx, obj);
    } else {
        if (MBJS_VALUE_GET_TAG(obj) == MBJS_TAG_MODULE) {
            if (MBJS_ResolveModule(ctx, obj) < 0) {
                MBJS_FreeValue(ctx, obj);
                goto exception;
            }
            if (mbjs_module_set_import_meta(ctx, obj, false, true) < 0)
                goto exception;
            val = MBJS_EvalFunction(ctx, obj);
            val = mbjs_std_await(ctx, val);
        } else {
            val = MBJS_EvalFunction(ctx, obj);
        }
        if (MBJS_IsException(val)) {
        exception:
            mbjs_std_dump_error(ctx);
            exit(1);
        }
        MBJS_FreeValue(ctx, val);
    }
}

static MBJSValue mbjs_bjson_read(MBJSContext *ctx, MBJSValueConst this_val,
                             int argc, MBJSValueConst *argv)
{
    uint8_t *buf;
    uint64_t pos, len;
    MBJSValue obj;
    size_t size;
    int flags;

    if (MBJS_ToIndex(ctx, &pos, argv[1]))
        return MBJS_EXCEPTION;
    if (MBJS_ToIndex(ctx, &len, argv[2]))
        return MBJS_EXCEPTION;
    if (MBJS_ToInt32(ctx, &flags, argv[3]))
        return MBJS_EXCEPTION;
    flags &= ~MBJS_READ_OBJ_SAB;
    buf = MBJS_GetArrayBuffer(ctx, &size, argv[0]);
    if (!buf)
        return MBJS_EXCEPTION;
    if (pos + len > size)
        return MBJS_ThrowRangeError(ctx, "array buffer overflow");
    obj = MBJS_ReadObject(ctx, buf + pos, len, flags);
    return obj;
}

static MBJSValue mbjs_bjson_write(MBJSContext *ctx, MBJSValueConst this_val,
                              int argc, MBJSValueConst *argv)
{
    size_t len;
    uint8_t *buf;
    MBJSValue array;
    int flags;

    if (MBJS_ToInt32(ctx, &flags, argv[1]))
        return MBJS_EXCEPTION;
    flags &= ~MBJS_WRITE_OBJ_SAB;
    buf = MBJS_WriteObject(ctx, &len, argv[0], flags);
    if (!buf)
        return MBJS_EXCEPTION;
    array = MBJS_NewArrayBufferCopy(ctx, buf, len);
    mbjs_free(ctx, buf);
    return array;
}


static const MBJSCFunctionListEntry mbjs_bjson_funcs[] = {
    MBJS_CFUNC_DEF("read", 4, mbjs_bjson_read ),
    MBJS_CFUNC_DEF("write", 2, mbjs_bjson_write ),
#define DEF(x) MBJS_PROP_INT32_DEF(#x, MBJS_##x, MBJS_PROP_CONFIGURABLE)
    DEF(READ_OBJ_BYTECODE),
    DEF(READ_OBJ_REFERENCE),
    DEF(WRITE_OBJ_BYTECODE),
    DEF(WRITE_OBJ_REFERENCE),
    DEF(WRITE_OBJ_STRIP_DEBUG),
    DEF(WRITE_OBJ_STRIP_SOURCE),
#undef DEF
};

static int mbjs_bjson_init(MBJSContext *ctx, MBJSModuleDef *m)
{
    return MBJS_SetModuleExportList(ctx, m, mbjs_bjson_funcs,
                                  countof(mbjs_bjson_funcs));
}

MBJSModuleDef *mbjs_init_module_bjson(MBJSContext *ctx, const char *module_name)
{
    MBJSModuleDef *m;
    m = MBJS_NewCModule(ctx, module_name, mbjs_bjson_init);
    if (!m)
        return NULL;
    MBJS_AddModuleExportList(ctx, m, mbjs_bjson_funcs, countof(mbjs_bjson_funcs));
    return m;
}
