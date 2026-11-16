#!/usr/bin/env bash
#
# build-profiles.sh — build the MochiBerry size tiers (slim / core / full).
#
# Usage:
#   sh scripts/build-profiles.sh              # build all three tiers
#   sh scripts/build-profiles.sh slim         # build one tier
#   sh scripts/build-profiles.sh slim core    # build some tiers
#
# Output per tier (under build/tier-<name>/):
#   mbjs.exe         unstripped CLI+REPL
#   mbjs.strip.exe   stripped binary (what you ship)
#   libmbjs.a        static engine library
#
# ---------------------------------------------------------------------------
# WHY -fno-asynchronous-unwind-tables IS MANDATORY HERE
# ---------------------------------------------------------------------------
# On PE/COFF (Windows) every function with unwind info gets a
# .pdata$NAME / .xdata$NAME section pair whose relocations point BACK at
# .text$NAME.  The linker must keep .pdata (it becomes the PE exception
# directory), so .text$NAME always stays reachable and --gc-sections can
# never drop a single function.  With that flag the .pdata/.xdata pairs are
# not emitted at all, the linker is free to garbage-collect whole features,
# and the slim tier shrinks by ~50% (together with -fno-jump-tables,
# see the note below -- without it the GC still cannot drop feature code).
#
# Measured on this host (MinGW gcc 15.1.0, GNU ld 2.44, x86_64-windows):
#
#   tier   flags                                   stripped size
#   -----  -------------------------------------   --------------
#   full   -O2, no feature guards                  1,356,814 B
#   core   -O2 + 3 guards (no TA/Perf/WeakRef)     1,295,886 B
#   slim   -Os + 10 guards (bare language core)      710,670 B
#
# The same build with unwind tables enabled produced three byte-identical
# binaries (~1,410,574 B each) -- that was the first bug. The second one was
# the shared jump-table .rdata blob (see -fno-jump-tables below).
#
# ---------------------------------------------------------------------------
# FEATURE MATRIX
# ---------------------------------------------------------------------------
#   feature           slim   core   full
#   ----------------  -----  -----  ----
#   language core     yes    yes    yes    (Object/Function/Array/String/
#                                         Number/Boolean/Symbol/Error/Math/
#                                         Reflect/parser/VM/Promise/eval)
#   Date              no     yes    yes
#   RegExp            no     yes    yes
#   JSON              no     yes    yes
#   Proxy             no     yes    yes
#   Map/Set           no     yes    yes
#   TypedArrays       no     no     yes
#   WeakRef           no     no     yes
#   Performance       no     no     yes
#   AToB/BAt          no     yes    yes
#   BigInt            no     yes    yes
#
# Verify a tier with:
#   build/tier-<name>/mbjs.strip.exe <script.js>
#
set -u

cd "$(dirname "$0")/.."
ROOT="$(pwd)"

# Native toolchain (gcc / ld / ar / strip) needs WINDOWS paths: a MinGW gcc
# cannot open "/e/bcf/..." — it only understands "E:/...". `pwd -W` gives the
# native form of the current directory in MSYS bash. Always convert paths that
# are handed to a native tool, and keep the POSIX form for bash-internal use.
ROOT_WIN="$(cd "$ROOT" && pwd -W 2>/dev/null || echo "$ROOT")"

# ---- toolchain -----------------------------------------------------------
# Prefer $CC if set; otherwise look for gcc in the usual places.
if [ -n "${CC:-}" ]; then
    :
elif command -v gcc >/dev/null 2>&1; then
    CC=gcc
elif command -v cc >/dev/null 2>&1; then
    CC=cc
else
    echo "error: no C compiler found (set CC=... to override)" >&2
    exit 1
fi
AR="${AR:-ar}"
STRIP="${STRIP:-strip}"

export TMPDIR="${TMPDIR:-/tmp}"
mkdir -p "$TMPDIR"

BUILD_ROOT="$ROOT/build"
TIERS="slim core full"
[ $# -gt 0 ] && TIERS="$*"

# ---- engine sources ------------------------------------------------------
# mbjs.c is the engine (single translation unit, ~65k lines).
# mbjs-inline.c (the mbjs.compile() inline-C tier) is REQUIRED by every tier:
# mbjs-libc.c's std boot calls mbjs_inline_init(), so the symbol has to
# resolve even in slim -- the tier only drops the *registration*, not the TU.
#
# mbjs-jit.c / mbjs-llvm.c are the two code-generation tiers. They are
# embedder-facing: the CLI never calls them, so linking them into the CLI
# costs nothing (--gc-sections drops them). They belong in libmbjs.a
# instead, which is what a host app links against:
#     core -> mbjs-jit.o    (CC backend: JS -> C shim -> dlopen)
#     full -> mbjs-llvm.o   (MochiLLVM backend: JIT via MCJIT, AOT via obj)
ENGINE_COMMON="dtoa libregexp libunicode mbjs mbjs-mem mbjs-mulch mbjs-superset mbjs-inline"
CLI_SRC="mbjs-cli mbjs-repl"
CLI_EXTRA="mbts mbjs-libc"

# ---------------------------------------------------------------------------
build_tier() {
    tier="$1"
    out="$BUILD_ROOT/tier-$tier"
    out_win="$ROOT_WIN/build/tier-$tier"   # native path for gcc/ar/strip

    case "$tier" in
        slim)
            defs="-DMBJS_NO_DATE -DMBJS_NO_REGEXP -DMBJS_NO_JSON -DMBJS_NO_PROXY \
                  -DMBJS_NO_MAPSET -DMBJS_NO_TYPEDARRAYS -DMBJS_NO_WEAKREF \
                  -DMBJS_NO_ATOB -DMBJS_NO_PERFORMANCE -DMBJS_NO_BIGINT"
            opt="-Os"
            engine="$ENGINE_COMMON"
            ;;
        core)
            defs="-DMBJS_NO_TYPEDARRAYS -DMBJS_NO_PERFORMANCE -DMBJS_NO_WEAKREF"
            opt="-O2"
            engine="$ENGINE_COMMON mbjs-jit"
            ;;
        full)
            defs=""
            opt="-O2"
            engine="$ENGINE_COMMON mbjs-jit mbjs-llvm"
            ;;
        *)
            echo "unknown tier '$tier' (expected: slim core full)" >&2
            return 1
            ;;
    esac

    # -ffunction-sections/-fdata-sections make every function/data its own
    # section so --gc-sections can drop the unused ones.
    cflags="$opt -std=gnu11 -D_GNU_SOURCE -I. \
            -ffunction-sections -fdata-sections \
            -fno-asynchronous-unwind-tables -fno-ident"
    ldflags="-Wl,--gc-sections"

    # -fno-jump-tables: two independent wins, no measured downside.
    #  (1) GCC puts every switch jump table in ONE shared anonymous .rdata
    #      section together with the string literals. That blob is
    #      referenced by any surviving code so it can never be discarded --
    #      and because the jump tables hold relative addresses to the case
    #      blocks, every function with a switch statement stays rooted
    #      (json_next_token, mbjs_array_every, mbjs_atomics_op, ...). That
    #      silently pinned the JSON parser, the RegExp engine and the whole
    #      TypedArray layer in the slim binary. Removing the jump tables
    #      removes those roots, so --gc-sections can collect the features.
    #  (2) The comparison-chain code is actually smaller than the table
    #      version here (-Os/-O2): mbjs.c shrinks by ~38 KB (2.6%) in every
    #      tier. Speed is unchanged (measured on a CPU-bound workload).
    cflags="$cflags -fno-jump-tables"

    echo "== tier $tier =="
    echo "   compiler: $($CC -dumpversion 2>/dev/null || echo '?')   opt: $opt"

    mkdir -p "$out"
    rm -f "$out"/*.o "$out"/*.a "$out"/*.exe "$out"/*.err "$out"/strip.err

    objs=""
    pids=""
    for f in $engine $CLI_SRC $CLI_EXTRA; do
        $CC $cflags $defs -c "$f.c" -o "$out_win/$f.o" 2>"$out/$f.err" &
        pids="$pids $!"
        objs="$objs $f"
    done
    $CC $cflags $defs -c gen/standalone.c -o "$out_win/standalone.o" 2>"$out/standalone.err" &
    pids="$pids $!"
    for p in $pids; do wait "$p" || echo "   FAIL: compile (pid $p)"; done

    failed=0
    for e in "$out"/*.err; do
        if grep -q "error:" "$e" 2>/dev/null; then
            echo "--- $e"
            grep -m4 "error:" "$e"
            failed=1
        fi
    done
    [ "$failed" -eq 0 ] || { echo "   COMPILE ERRORS"; return 1; }

    # static engine library
    libobjs=""
    for f in $engine; do libobjs="$libobjs $out_win/$f.o"; done
    rm -f "$out/libmbjs.a"
    $AR rcs "$out_win/libmbjs.a" $libobjs || { echo "   FAIL: ar"; return 1; }

    # Publish the headers the JIT shim and the embedders compile against.
    # mbjs.h #includes "mbjs-version.h", so a dist/include without that
    # file makes every JIT shim fail to compile with a bare
    # "mbjs.h: No such file or directory" (the include error names the
    # outer header, not the missing one).
    mkdir -p dist/include
    cp -f mbjs.h mbjs-version.h mbjs-libc.h mbjs-mem.h mbjs-jit.h \
          dist/include/ 2>/dev/null || true
    if [ "$tier" = "full" ]; then
        cp -f mbjs-llvm.h mbjs-opt.h dist/include/ 2>/dev/null || true
    fi

    # CLI + REPL
    linkobjs="$out_win/mbjs-cli.o $out_win/mbjs-repl.o $out_win/standalone.o $out_win/mbts.o $out_win/mbjs-libc.o $out_win/libmbjs.a"
    $CC $opt $ldflags -o "$out_win/mbjs.exe" $linkobjs -lm -lpthread 2>"$out/link.err" || {
        echo "   FAIL: link"; head -20 "$out/link.err"; return 1; }

    cp "$out/mbjs.exe" "$out/mbjs.strip.exe"
    # native path for strip.exe; also fail loudly -- a silent no-op here
    # once shipped three "stripped" binaries that were not stripped at all.
    if ! $STRIP -s "$out_win/mbjs.strip.exe" 2>"$out/strip.err"; then
        echo "   FAIL: strip"; head -3 "$out/strip.err"; return 1
    fi

    printf '   %-16s %10s\n' "mbjs.strip.exe" "$(wc -c < "$out/mbjs.strip.exe")"
    printf '   %-16s %10s\n' "libmbjs.a" "$(wc -c < "$out/libmbjs.a")"

    # full tier also ships the compiler (mbjsc), the AOT/JIT front end:
    #   mbjsc -e app.js -o app.c   -> bytecode-embedded native app
    #   mbjsc -j app.js -o app.c   -> JIT launcher
    #   mbjsc -o-llvm --native ...  -> MochiLLVM native codegen
    if [ "$tier" = "full" ]; then
        for u in mbjsc mbjsc-namelist mbjsc-emit mbjsc-pseudo mbjsc-module mbjsc-compile; do
            $CC $cflags -c "$u.c" -o "$out_win/$u.o" 2>"$out/$u.err" || {
                echo "   FAIL: compile $u"; head -10 "$out/$u.err"; return 1; }
        done
        $CC $cflags -c mbjs-opt.c -o "$out_win/mbjs-opt.o" 2>"$out/mbjs-opt.err" || {
            echo "   FAIL: compile mbjs-opt"; head -10 "$out/mbjs-opt.err"; return 1; }
        $CC $opt $ldflags -o "$out_win/mbjsc.exe" \
            "$out_win/mbjsc.o" "$out_win/mbjsc-namelist.o" "$out_win/mbjsc-emit.o" \
            "$out_win/mbjsc-pseudo.o" "$out_win/mbjsc-module.o" "$out_win/mbjsc-compile.o" \
            "$out_win/mbjs-opt.o" \
            "$out_win/mbjs-libc.o" "$out_win/mbjs-inline.o" "$out_win/libmbjs.a" \
            -lm -lpthread 2>"$out/link-mbjsc.err" || {
            echo "   FAIL: link mbjsc"; head -20 "$out/link-mbjsc.err"; return 1; }
        cp "$out/mbjsc.exe" "$out/mbjsc.strip.exe"
        $STRIP -s "$out_win/mbjsc.strip.exe" 2>/dev/null || true
        printf '   %-16s %10s\n' "mbjsc.strip.exe" "$(wc -c < "$out/mbjsc.strip.exe")"
    fi

    # smoke test
    if "$out/mbjs.strip.exe" -e 'print("ok "+(1+2))' >/dev/null 2>&1; then
        echo "   smoke: OK"
    else
        echo "   smoke: FAIL"
        return 1
    fi
    return 0
}

rc=0
for t in $TIERS; do
    build_tier "$t" || rc=1
done

echo
echo "== tier size report =="
printf '%-10s %12s %12s\n' tier stripped libmbjs.a
for t in slim core full; do
    e="$BUILD_ROOT/tier-$t/mbjs.strip.exe"
    a="$BUILD_ROOT/tier-$t/libmbjs.a"
    [ -f "$e" ] || continue
    printf '%-10s %12s %12s\n' "$t" "$(wc -c < "$e")" "$(wc -c < "$a" 2>/dev/null || echo -)"
done

exit $rc
