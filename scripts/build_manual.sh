#!/usr/bin/env bash
# Full manual build of mbjs + run-test262 with the patched sources.
# Flags per skill mochiberry-windows-build-test262 (deterministic, same for every object).
set -eu
# Repo root = the directory containing scripts/. Works from any mount.
REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$REPO_ROOT"
export TMPDIR="$REPO_ROOT/.tmp"
export TEMP=$TMPDIR
export TMP=$TMPDIR
mkdir -p "$TMPDIR" build_manual
CFLAGS="-O2 -g -std=gnu11 -D_GNU_SOURCE -I."
for f in dtoa libregexp libunicode mbjs mbjs-superset mbjs-libc mbjs-mem mbjs-mulch mbts mbjs-cli mbjs-repl mbjs-jit mbjs-llvm; do
  echo "== compile $f.c"
  gcc $CFLAGS -c "$f.c" -o "build_manual/$f.o"
done
echo "== compile gen/standalone.c"
gcc $CFLAGS -c gen/standalone.c -o build_manual/standalone.o
echo "== archive"
ar rcs build_manual/libmbjs.a build_manual/dtoa.o build_manual/libregexp.o build_manual/libunicode.o \
  build_manual/mbjs.o build_manual/mbjs-superset.o build_manual/mbjs-libc.o build_manual/mbjs-mem.o build_manual/mbts.o \
  build_manual/mbjs-mulch.o build_manual/mbjs-jit.o build_manual/mbjs-llvm.o
echo "== link mbjs.exe"
gcc -o build_manual/mbjs.exe build_manual/mbjs-cli.o build_manual/mbjs-repl.o build_manual/standalone.o \
  build_manual/libmbjs.a -lm -lpthread
echo "== link run-test262.exe"
gcc $CFLAGS -o build_manual/run-test262.exe run-test262.c build_manual/libmbjs.a -lm -lpthread
echo "== link mbjsc.exe (MochiBerry compiler)"
for u in mbjsc mbjsc-namelist mbjsc-emit mbjsc-pseudo mbjsc-module mbjsc-compile; do
  gcc $CFLAGS -c "$u.c" -o "build_manual/$u.o"
done
gcc -o build_manual/mbjsc.exe build_manual/mbjsc.o build_manual/mbjsc-namelist.o build_manual/mbjsc-emit.o \
  build_manual/mbjsc-pseudo.o build_manual/mbjsc-module.o build_manual/mbjsc-compile.o \
  build_manual/libmbjs.a -lm -lpthread
echo "== link mbvm.exe (MochiVM runner)"
gcc $CFLAGS -c mbvm.c -o build_manual/mbvm.o
gcc -o build_manual/mbvm.exe build_manual/mbvm.o build_manual/libmbjs.a -lm -lpthread \
  -Wl,--export-all-symbols
echo "== link takegawa.exe (TGPM package manager launcher)"
gcc $CFLAGS -o build_manual/takegawa.exe takegawa.c build_manual/libmbjs.a -lm -lpthread
echo "== TGPM JS tests"
build_manual/mbjs.exe -m tests/test_takegawa.js
echo "== build test-mulch.exe (libmulchberry tests)"
gcc $CFLAGS -o build_manual/test-mulch.exe tests/test_mulch.c build_manual/mbjs-mulch.o \
  build_manual/libmbjs.a -lm -lpthread

echo "== build test-routine.exe (MochiRoutine fiber tests)"
gcc $CFLAGS -c mbjs-routine.c -o build_manual/mbjs-routine.o
gcc $CFLAGS -o build_manual/test-routine.exe tests/test_routine.c build_manual/mbjs-routine.o \
  build_manual/libmbjs.a -lm -lpthread

echo "== build test-superset.exe (Takegawa TGPM superset registry)"
gcc $CFLAGS -o build_manual/test-superset.exe tests/test_superset.c \
  build_manual/mbjs-superset.o build_manual/libmbjs.a -lm -lpthread
echo "== build test-jit.exe (JIT foundation; --export-all-symbols hosts JIT shims)"
gcc $CFLAGS -o build_manual/test-jit.exe tests/test_jit.c \
  build_manual/libmbjs.a -lm -lpthread -Wl,--export-all-symbols
echo "== stage headers for JIT self-containment"
cp -f mbjs.h mbjs-version.h mbts.h build_manual/
echo "== BUILD OK"
ls -la build_manual/mbjs.exe build_manual/run-test262.exe build_manual/mbjsc.exe build_manual/mbvm.exe build_manual/test-mulch.exe build_manual/test-routine.exe build_manual/test-superset.exe
