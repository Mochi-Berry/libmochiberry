/*
 * takegawa.c — native launcher for TGPM, the Takegawa package manager.
 *
 * TGPM is the MochiBerry package manager and lives as a separate tool: the
 * implementation is takegawa.js (a module that runs on the engine).  This
 * launcher embeds the engine, registers the system modules, locates
 * takegawa.js and evaluates it with the command line forwarded to it.
 *
 * Script lookup order:
 *   $TAKEGAWA_SRC
 *   $TAKEGAWA_HOME/takegawa.js
 *   <exe-dir>/takegawa.js
 *   <exe-dir>/../takegawa.js
 *   <exe-dir>/../share/takegawa/takegawa.js
 *   $PWD/takegawa.js
 *
 * The same JS file can be run directly with `mbjs -m takegawa.js ...`; this
 * binary just makes `takegawa` a first-class command.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "mbjs.h"
#include "mbjs-libc.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

#define PROG_NAME "takegawa"
#define PATH_MAX_LEN 4096

static int get_exe_path(char *buf, size_t size)
{
#if defined(_WIN32)
    DWORD n = GetModuleFileNameA(NULL, buf, (DWORD)size);
    if (n == 0 || n >= size)
        return -1;
    return 0;
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t)size;
    if (_NSGetExecutablePath(buf, &sz) != 0)
        return -1;
    return 0;
#else
    ssize_t n = readlink("/proc/self/exe", buf, size - 1);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return 0;
#endif
}

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static void path_dirname(char *p)
{
    char *a = strrchr(p, '/');
    char *b = strrchr(p, '\\');
    char *s = a > b ? a : b;
    if (!s)
        return;
    if (s == p)
        s[1] = '\0';
    else
        *s = '\0';
}

static void path_join(char *out, size_t n, const char *a, const char *b)
{
    size_t la = strlen(a);
    const char *sep = (la > 0 && (a[la - 1] == '/' || a[la - 1] == '\\')) ? "" : "/";
    snprintf(out, n, "%s%s%s", a, sep, b);
}

static const char *find_script(char *candidate, size_t n)
{
    const char *env;

    env = getenv("TAKEGAWA_SRC");
    if (env && file_exists(env))
        return env;

    env = getenv("TAKEGAWA_HOME");
    if (env && *env) {
        path_join(candidate, n, env, "takegawa.js");
        if (file_exists(candidate))
            return candidate;
    }

    if (get_exe_path(candidate, n) == 0) {
        char dir[PATH_MAX_LEN];
        snprintf(dir, sizeof(dir), "%s", candidate);
        path_dirname(dir);

        path_join(candidate, n, dir, "takegawa.js");
        if (file_exists(candidate))
            return candidate;

        char up[PATH_MAX_LEN];
        snprintf(up, sizeof(up), "%s", dir);
        path_dirname(up);

        path_join(candidate, n, up, "takegawa.js");
        if (file_exists(candidate))
            return candidate;

        char share[PATH_MAX_LEN];
        path_join(share, sizeof(share), up, "share/takegawa/takegawa.js");
        if (file_exists(share)) {
            snprintf(candidate, n, "%s", share);
            return candidate;
        }
    }

    env = getenv("PWD");
    if (env && *env) {
        path_join(candidate, n, env, "takegawa.js");
        if (file_exists(candidate))
            return candidate;
    }
    if (file_exists("takegawa.js"))
        return "takegawa.js";

    return NULL;
}

int main(int argc, char **argv)
{
    char script_buf[PATH_MAX_LEN];
    const char *script;
    MBJSRuntime *rt;
    MBJSContext *ctx;
    MBJSValue val;
    MBJSValue global;
    int32_t exit_code = 0;
    int ret = 0;

    script = find_script(script_buf, sizeof(script_buf));
    if (!script) {
        fprintf(stderr,
                PROG_NAME ": cannot find takegawa.js\n"
                "  set TAKEGAWA_SRC or TAKEGAWA_HOME, or place takegawa.js next to the executable\n");
        return 1;
    }

    rt = MBJS_NewRuntime();
    if (!rt) {
        fprintf(stderr, PROG_NAME ": failed to create runtime\n");
        return 1;
    }
    mbjs_std_init_handlers(rt);
    MBJS_SetModuleLoaderFunc2(rt, NULL, mbjs_module_loader,
                              mbjs_module_check_attributes, NULL);

    ctx = MBJS_NewContext(rt);
    if (!ctx) {
        fprintf(stderr, PROG_NAME ": failed to create context\n");
        MBJS_FreeRuntime(rt);
        return 1;
    }
    mbjs_init_module_std(ctx, "mbjs:std");
    mbjs_init_module_os(ctx, "mbjs:os");
    mbjs_init_module_bjson(ctx, "mbjs:bjson");

    /* scriptArgs = [script, user args...] */
    {
        int i;
        char **sargv = (char **)calloc((size_t)argc + 1, sizeof(char *));
        if (!sargv) {
            fprintf(stderr, PROG_NAME ": out of memory\n");
            ret = 1;
            goto done_ctx;
        }
        sargv[0] = (char *)script;
        for (i = 1; i < argc; i++)
            sargv[i] = argv[i];
        mbjs_std_add_helpers(ctx, argc, sargv);
        free(sargv);
    }

    global = MBJS_GetGlobalObject(ctx);
    MBJS_SetPropertyStr(ctx, global, "__TGPM_MAIN__", MBJS_TRUE);
    {
        int i;
        MBJSValue args = MBJS_NewArray(ctx);
        for (i = 1; i < argc; i++)
            MBJS_SetPropertyUint32(ctx, args, i - 1, MBJS_NewString(ctx, argv[i]));
        MBJS_SetPropertyStr(ctx, global, "__TGPM_ARGV__", args);
    }
    MBJS_FreeValue(ctx, global);

    val = MBJS_LoadModule(ctx, "takegawa.js", script);
    val = mbjs_std_await(ctx, val);
    if (MBJS_IsException(val)) {
        mbjs_std_dump_error(ctx);
        ret = 1;
    } else {
        MBJS_FreeValue(ctx, val);
        global = MBJS_GetGlobalObject(ctx);
        val = MBJS_GetPropertyStr(ctx, global, "__TGPM_EXIT_CODE__");
        if (!MBJS_IsUndefined(val) && !MBJS_IsException(val)) {
            if (MBJS_ToInt32(ctx, &exit_code, val) == 0)
                ret = (int)exit_code;
        }
        MBJS_FreeValue(ctx, val);
        MBJS_FreeValue(ctx, global);
    }

done_ctx:
    MBJS_FreeContext(ctx);
    MBJS_FreeRuntime(rt);
    return ret;
}
