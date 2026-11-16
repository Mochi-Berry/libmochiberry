#!/usr/bin/env sh
#
# build-sunat.sh — build the "Sunat GC" variant of libmochiberry.
#
# The Sunat variant is a runtime in which the garbage collector is
# circumcised out (mbjs_trigger_gc / MBJS_RunGC are no-ops) and every byte
# the engine allocates lives in one deterministic mbjs-mem arena (bump
# allocator, no per-object free). Memory usage is a monotone bump that is
# fully reclaimed in bulk at teardown — the "memory management kosongan"
# model for bare-metal / custom-OS runtimes and OTA-style firmware that must
# avoid GC pauses at any cost.
#
# The engine is compiled from the real sources (not the amalgam, which only
# carries the core headers): mbjs.c sees mbjs-mem.h directly under
# -DMBJS_SUNAT_GC.
#
# Produces under dist/sunat/:
#   libmochiberry.so       sunat shared engine library (-DMBJS_SUNAT_GC)
#   libmochiberry.a        sunat static engine library
#   libmochiberry.o        relocatable engine object
#   mbjs-sunat             CLI: runs JS, prints arena stats + reclaim audit
#   mbjs.h mbjs-mem.h      headers (mbjs.h declares the sunat API under
#                          -DMBJS_SUNAT_GC)
#
# Usage:
#   sh scripts/build-sunat.sh
#
set -u

cd "$(dirname "$0")/.."

echo "== sunat GC build (libmochiberry) =="

if [ ! -x build/mbjs ]; then
    echo "building engine first (make)..."
    make -j4 || exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p dist/sunat

ENGINE_SRCS="mbjs.c mbjs-libc.c mbjs-mem.c dtoa.c libregexp.c libunicode.c"
CFLAGS="-O2 -D_GNU_SOURCE -DMBJS_SUNAT_GC -DMBJS_BUILD_LIBC -I."

echo "compiling dist/sunat/libmochiberry.so (GC circumcised, arena-backed)..."
cc -fPIC -shared $CFLAGS -o dist/sunat/libmochiberry.so $ENGINE_SRCS -lm || exit 1

echo "compiling dist/sunat/libmochiberry.a + libmochiberry.o..."
cc $CFLAGS -c mbjs.c -o "$TMP/mbjs.o" || exit 1
cc $CFLAGS -c mbjs-libc.c -o "$TMP/mbjs-libc.o" || exit 1
cc $CFLAGS -c mbjs-mem.c -o "$TMP/mbjs-mem.o" || exit 1
cc $CFLAGS -c dtoa.c -o "$TMP/dtoa.o" || exit 1
cc $CFLAGS -c libregexp.c -o "$TMP/libregexp.o" || exit 1
cc $CFLAGS -c libunicode.c -o "$TMP/libunicode.o" || exit 1
ar rcs dist/sunat/libmochiberry.a \
    "$TMP/mbjs.o" "$TMP/mbjs-libc.o" "$TMP/mbjs-mem.o" \
    "$TMP/dtoa.o" "$TMP/libregexp.o" "$TMP/libunicode.o" || exit 1
cp "$TMP/mbjs.o" dist/sunat/libmochiberry.o

echo "building sunat CLI (mbjs-sunat)..."
cc -O2 -DMBJS_SUNAT_GC -I. \
    platform/sunat/mbjs-sunat-cli.c \
    -Ldist/sunat -lmochiberry -Wl,-rpath,'$ORIGIN' \
    -o dist/sunat/mbjs-sunat -lm || exit 1

cp mbjs.h mbjs-version.h mbjs-mem.h dist/sunat/ 2>/dev/null

echo
echo "== sunat artifacts =="
ls -la dist/sunat
echo
echo "engine version: $(build/mbjs --version | head -1)"
echo "done."
