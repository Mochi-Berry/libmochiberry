#!/usr/bin/env bash
#
# brand-mbjs.sh — MBJS rebranding tool for the libmochiberry fork.
#
# Idempotent, deterministic rename of the whole tree from the upstream
# QuickJS-NG naming to the MBJS naming:
#
#   qjsc         -> mbjsc     (bytecode compiler: binary, symbols, targets)
#   qjs          -> mbjs      (CLI binary, internal identifiers, module specifiers)
#   QJS_*        -> MBJS_*    (CMake/Meson build options, version macros)
#   JS_*         -> MBJS_*    (public + internal C API)
#   js_*         -> mbjs_*    (internal helper functions, e.g. js_malloc -> mbjs_malloc)
#   JS<Upper>    -> MBJS<U>   (types: JSValue -> MBJSValue, JSContext -> MBJSContext, …)
#   quickjs      -> mbjs      (file names, include guards, display strings; case-insensitive)
#
# JSON* names are deliberately left alone (JSON is the ECMAScript global, not the
# engine's prefix). Generated files (gen/*.c, builtin-*.h) are excluded here:
# they must be regenerated with `make codegen` after the rename so the embedded
# bytecode carries the new module specifiers (mbjs:std / mbjs:os / mbjs:bjson).
#
# File renames handled by this script (idempotent, skipped if already done):
#   quickjs.c -> mbjs.c          quickjs.h  -> mbjs.h
#   qjs.c     -> mbjs-cli.c      qjsc.c     -> mbjsc.c
#   quickjs-libc.c -> mbjs-libc.c   quickjs-libc.h -> mbjs-libc.h
#   quickjs-atom.h -> mbjs-atom.h   quickjs-c-atomics.h -> mbjs-c-atomics.h
#   quickjs-opcode.h -> mbjs-opcode.h
#   qjs-wasi-reactor.c -> mbjs-wasi-reactor.c
#   qjs.man -> mbjs.man          qjsc.man -> mbjsc.man
#
# Usage:
#   ./scripts/brand-mbjs.sh
#
set -euo pipefail

cd "$(dirname "$0")/.."

# --- File renames (idempotent) -------------------------------------------------
RENAMES=(
  'quickjs.c mbjs.c'
  'quickjs.h mbjs.h'
  'qjs.c mbjs-cli.c'
  'qjsc.c mbjsc.c'
  'quickjs-libc.c mbjs-libc.c'
  'quickjs-libc.h mbjs-libc.h'
  'quickjs-atom.h mbjs-atom.h'
  'quickjs-c-atomics.h mbjs-c-atomics.h'
  'quickjs-opcode.h mbjs-opcode.h'
  'qjs-wasi-reactor.c mbjs-wasi-reactor.c'
  'qjs.man mbjs.man'
  'qjsc.man mbjsc.man'
)
renamed=0
for pair in "${RENAMES[@]}"; do
  set -- $pair
  if [ -f "$1" ] && [ ! -f "$2" ]; then
    git mv "$1" "$2"
    renamed=$((renamed + 1))
  fi
done
[ "$renamed" -gt 0 ] && echo "brand-mbjs: renamed $renamed files."

# --- Content renames -----------------------------------------------------------
FILES="$(git ls-files -co --exclude-standard \
  | grep -vE '(^|/)(\.git|build/|build-.*/|test262/|gen/|\.github/)(/|$)' \
  | grep -E '(\.(c|h|cc|js|mjs|md|yml|yaml|txt|sh|json|in|man|conf|toml|html|ts|cfg|cmake|options|mk)$|(^|/)(Makefile|meson\.build)$)' || true)"

if [ -z "$FILES" ]; then
  echo "brand-mbjs: no files to process"
  exit 0
fi

PERL_RULES=(
  's/\bqjsc/mbjsc/g'                 # qjsc -> mbjsc (must run before mbjs)
  's/\bqjs/mbjs/g'                   # qjs  -> mbjs
  's/\bQJS_/MBJS_/g'                 # QJS_ -> MBJS_
  's/\bJS_/MBJS_/g'                  # JS_  -> MBJS_
  's/\bjs_/mbjs_/g'                  # js_  -> mbjs_
  's/\bJS(?!ON)(?=[A-Z])/MBJS/g'     # JSValue -> MBJSValue, JSON* untouched
  's/\bQUICKJS\b/MBJS/g'             # QUICKJS -> MBJS
  's/\bquickjs/mbjs/gi'              # quickjs -> mbjs (case-insensitive)
)

count=0
for f in $FILES; do
  [ -f "$f" ] || continue
  RULES=$(printf '%s;' "${PERL_RULES[@]}")
  perl -pi -e "$RULES" "$f"
  count=$((count + 1))
done

echo "brand-mbjs: rebranded $count files."
echo
echo "Remaining legacy tokens (should be empty):"
git grep -nE '\b(MBJS_|MBJS_|mbjs_|mbjs|mbjsc|mbjs)' -- ':!gen' ':!builtin-*.h' ':!docs' ':!test262' ':!build*' 2>/dev/null | grep -v 'JSON' | head -20 || true
git grep -nE '\bJS[A-Z]' -- ':!*.js' ':!docs' 2>/dev/null | grep -vE '\b(JSON|MBJS)' | head -10 || true
echo "done."
