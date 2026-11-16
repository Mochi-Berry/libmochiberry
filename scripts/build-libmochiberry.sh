#!/usr/bin/env sh
#
# build-libmochiberry.sh — build libmochiberry (shared + static + object) and
# the "simple machine" host that links it.
#
# Produces under dist/:
#   libmochiberry.so       shared engine library (amalgamated, MBJS_BUILD_LIBC)
#   libmochiberry.a        static engine library
#   libmochiberry.o        relocatable engine object (for custom linkers, OTA,
#                          baremetal boots and non-standard build systems)
#   libmochiberry.pc       pkg-config file
#   include/mbjs.h         engine header
#   include/mbjs-libc.h    std/os/bjson helper header
#   include/mbjs-mem.h     non-GC memory foundation header
#   include/mbjs-jit.h     JIT foundation header
#   embed_libmochiberry    minimal C host that runs a JS app against the .so
#
# On Windows the same amalgam is built as libmochiberry.dll (plus an import
# library); see scripts/cross-build.sh for the MinGW target.
#
# Usage:
#   sh scripts/build-libmochiberry.sh
#
set -u

cd "$(dirname "$0")/.."

echo "== libmochiberry build =="

# The amalgam generator runs on the freshly built engine.
if [ ! -x build/mbjs ]; then
    echo "building engine first (make)..."
    make -j4 || exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "generating amalgamated source..."
build/mbjs amalgam.js "$TMP/mbjs-amalgam.c" || exit 1

mkdir -p dist dist/include

echo "compiling libmochiberry.so..."
cc -O2 -fPIC -shared -DMBJS_BUILD_LIBC -I. \
    -o dist/libmochiberry.so "$TMP/mbjs-amalgam.c" -lm || exit 1

echo "compiling libmochiberry.a + libmochiberry.o..."
cc -O2 -DMBJS_BUILD_LIBC -I. \
    -c "$TMP/mbjs-amalgam.c" -o "$TMP/mbjs-amalgam.o" || exit 1
ar rcs dist/libmochiberry.a "$TMP/mbjs-amalgam.o" || exit 1
cp "$TMP/mbjs-amalgam.o" dist/libmochiberry.o

echo "writing libmochiberry.pc..."
{
    echo 'prefix=${pcfiledir}/..'
    echo 'exec_prefix=${prefix}'
    echo 'libdir=${prefix}'
    echo 'includedir=${prefix}/include'
    echo ''
    echo 'Name: libmochiberry'
    echo 'Description: MBJS Javascript engine (package my.berrylabfoundation.mochiberry)'
    echo 'Version: 0.16.1'
    echo 'Libs: -L${libdir} -lmochiberry'
    echo 'Libs.private: -lm'
    echo 'Cflags: -I${includedir}'
} > dist/libmochiberry.pc

echo "copying headers..."
cp mbjs.h mbjs-version.h mbjs-libc.h mbjs-mem.h mbjs-jit.h dist/include/ 2>/dev/null || \
    cp mbjs.h mbjs-version.h mbjs-libc.h mbjs-mem.h dist/include/

echo "building embed host (simple machine)..."
cc -O2 -I. -Idist/include \
    examples/embed_libmochiberry.c mbjs-mem.c \
    -Ldist -lmochiberry -Wl,-rpath,'$ORIGIN' \
    -o dist/embed_libmochiberry -lm || exit 1

echo
echo "== built artifacts =="
ls -la dist/libmochiberry.so dist/libmochiberry.a dist/libmochiberry.o dist/libmochiberry.pc dist/embed_libmochiberry
echo
echo "engine version: $(build/mbjs --version | head -1)"
echo "done."
