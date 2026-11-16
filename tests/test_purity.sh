#!/usr/bin/env bash
#
# test_purity.sh — regression test for the MBJS rename (purity gate).
#
# Verifies that the source tree contains no legacy QuickJS tokens
# (qjs, qjsc, QJS, JS_*, js_*, JS<Upper>, quickjs, JS_NAN_BOXING, ...).
#
# Usage:
#   sh tests/test_purity.sh
#
# Exit code 0 = tree is 100% MBJS-pure.
# Exit code 1 = at least one legacy token leaked back into the tree.
#
set -u

cd "$(dirname "$0")/.."

echo "== running MBJS purity audit =="
OUT="$(sh scripts/purify.sh --check 2>&1)"
STATUS=$?

echo "$OUT"

if [ "$STATUS" -ne 0 ]; then
  echo
  echo "test_purity: FAILED — legacy QuickJS tokens are still present."
  exit 1
fi

SCORE="$(printf '%s\n' "$OUT" | grep -E 'purity score' | grep -oE '[0-9]+\.[0-9]+%' | head -1)"
echo
echo "test_purity: PASSED — purity ${SCORE:-100.00%}"
exit 0
