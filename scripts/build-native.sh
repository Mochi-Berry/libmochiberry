#!/usr/bin/env sh
#
# build-native.sh — compile a JavaScript app to a native executable (AOT),
# Bun `bun build --compile` / Clang style: JS -> bytecode -> C -> machine code.
#
# Pipeline:
#   build/mbjsc -e app.js   ->  app.c   (bytecode embedded, self-contained main)
#   cc -O2 app.c + libmochiberry  ->  native binary
#
# Usage:
#   sh scripts/build-native.sh <app.js> [out] [options]
#
#   out       default dist/<app-name-without-.js>
#
#   options:
#     --static        link libmochiberry.a statically (no .so needed at runtime;
#                     single-file binary). This is the default for the Makefile
#                     `native` target.
#     --fully-static  --static plus a fully static libc (-static).
#     --shared        link against libmochiberry.so (small binary; the .so must
#                     be findable at runtime via rpath/LD_LIBRARY_PATH).
#     --strip         strip debug symbols.
#     --lto           enable link-time optimization (-flto).
#     --o3            optimize hard (-O3) instead of the default -O2.
#     --native        also tune for this machine (-march=native -mtune=native).
#     --verbose       print the compiler commands.
#
# The produced binary is named <out> on Unix (ELF) and <out>.exe when the
# host is Windows (see scripts/cross-build.sh for cross-target binaries).
#
set -u

cd "$(dirname "$0")/.."

APP="${1:-examples/app_mochiberry.js}"
OUT=""
MODE="static"
STRIP=0
LTO=0
O3=0
NATIVE_TUNE=0
VERBOSE=0

for arg in "$@"; do
    case "$arg" in
        --static) MODE="static" ;;
        --fully-static) MODE="fully-static" ;;
        --shared) MODE="shared" ;;
        --strip) STRIP=1 ;;
        --lto) LTO=1 ;;
        --o3) O3=1 ;;
        --native) NATIVE_TUNE=1 ;;
        --verbose) VERBOSE=1 ;;
        -*) echo "build-native: unknown option: $arg" >&2; exit 2 ;;
        *)
            if [ -z "$OUT" ] && [ "$arg" != "$APP" ]; then
                OUT="$arg"
            fi
            ;;
    esac
done

if [ "$OUT" = "" ]; then
    BASE="$(basename "$APP" .js)"
    OUT="dist/${BASE}-native"
fi

if [ ! -x build/mbjsc ] || [ ! -x build/mbjs ]; then
    echo "building engine first (make)..."
    make -j4 || exit 1
fi

if [ ! -f dist/libmochiberry.a ] || [ ! -f dist/libmochiberry.so ]; then
    echo "building libmochiberry first (make libmochiberry)..."
    make libmochiberry || exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== AOT native build: $APP -> $OUT =="

echo "  [1/3] compiling JS to embedded-bytecode C (mbjsc -e)..."
build/mbjsc -e -o "$TMP/app.c" "$APP" || exit 1

CFLAGS="-O2"
if [ "$O3" -eq 1 ]; then
    CFLAGS="-O3"
fi
if [ "$NATIVE_TUNE" -eq 1 ]; then
    CFLAGS="$CFLAGS -march=native -mtune=native"
fi
LDFLAGS=""
if [ "$LTO" -eq 1 ]; then
    CFLAGS="$CFLAGS -flto"
fi
case "$MODE" in
    static)
        LDFLAGS="-I. -Idist/include $TMP/app.c dist/libmochiberry.a -lm"
        ;;
    fully-static)
        LDFLAGS="-I. -Idist/include $TMP/app.c dist/libmochiberry.a -lm -static"
        ;;
    shared)
        LDFLAGS="-I. -Idist/include $TMP/app.c -Ldist -lmochiberry -Wl,-rpath,'\$ORIGIN' -lm"
        ;;
esac

echo "  [2/3] compiling + linking machine code (cc $CFLAGS)..."
if [ "$VERBOSE" -eq 1 ]; then
    echo "      cc $CFLAGS $LDFLAGS -o $OUT"
fi
cc $CFLAGS $LDFLAGS -o "$OUT" || exit 1

if [ "$STRIP" -eq 1 ]; then
    echo "  [3/3] stripping symbols..."
    strip "$OUT" || exit 1
else
    echo "  [3/3] done."
fi

echo
echo "== native binary ready =="
ls -la "$OUT"
file "$OUT" 2>/dev/null || true
echo "run it with: $OUT"
echo "done."
