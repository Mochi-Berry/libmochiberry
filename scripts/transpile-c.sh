#!/usr/bin/env sh
#
# transpile-c.sh — transpile a JavaScript app to *freestanding C*.
#
# Pipeline:
#   build/mbjsc -N mbjsc_app -o app.c app.js
#     -> app.c contains only bytecode data tables (const uint8_t[] /
#        const uint32_t), freestanding-safe: it compiles with
#        `-ffreestanding -nostdlib` and is assembler-backend agnostic
#        (gcc, clang, and their gas/llvm integrated assemblers).
#
# Then either:
#   --check  verify the C compiles freestanding (no libc, no runtime),
#   --build  additionally link it with examples/embed_bytecode.c against
#            libmochiberry into a runnable native binary (AOT).
#
# Usage:
#   sh scripts/transpile-c.sh <app.js> [out.c] [--check|--build]
#
set -u

cd "$(dirname "$0")/.."

APP="${1:-examples/app_mochiberry.js}"
OUT_C="${2:-dist/transpile/app.c}"
MODE="${3:-build}"

if [ ! -x build/mbjsc ]; then
    echo "building engine first (make)..."
    make -j4 || exit 1
fi
if [ ! -f dist/libmochiberry.so ]; then
    echo "building libmochiberry first (make libmochiberry)..."
    make libmochiberry || exit 1
fi

echo "== transpile to freestanding C: $APP -> $OUT_C =="
mkdir -p "$(dirname "$OUT_C")"

# -N fixes the generated symbol names so examples/embed_bytecode.c can find
# them: mbjsc_app / mbjsc_app_size.
build/mbjsc -N mbjsc_app -o "$OUT_C" "$APP" || exit 1

echo "freestanding check (-ffreestanding -nostdlib, data tables only)..."
cc -ffreestanding -nostdlib -I. -c "$OUT_C" -o /tmp/transpile-c-check.o || {
    echo "FAIL: $OUT_C is not freestanding-safe"
    exit 1
}
echo "PASS: $OUT_C compiles freestanding (gcc/clang/gas/llvm clean)"

if [ "$MODE" = "check" ]; then
    echo "done (check only)."
    exit 0
fi

echo "linking native binary with examples/embed_bytecode.c..."
OUT_BIN="$(dirname "$OUT_C")/$(basename "$OUT_C" .c)-native"
cc -O2 -I. -Idist/include "$OUT_C" examples/embed_bytecode.c mbjs-mem.c \
    -Ldist -lmochiberry -Wl,-rpath,"$PWD/dist" -o "$OUT_BIN" || exit 1
echo
echo "== transpiled C binary ready =="
ls -la "$OUT_BIN"
echo "run it with: $OUT_BIN"
echo "done."
