#!/bin/bash
# Measure Intl test262 directories with PASS/FAIL/SKIP counts.
cd /f/bcf/mochiberry || exit 1
DIRS="$@"
if [ -z "$DIRS" ]; then
  DIRS="intl402/ListFormat/prototype intl402/RelativeTimeFormat/prototype intl402/Segmenter/prototype intl402/Segmenter/constructor intl402/Intl/getCanonicalLocales intl402/Intl/supportedValuesOf"
fi
for d in $DIRS; do
  rm -f test262_prelude_intl.js.binc
  out=$(./build/run-test262.exe -c test262_full.conf -P test262_prelude_intl.js -d "test262/test/$d" -v 2>&1)
  res=$(printf '%s\n' "$out" | grep -E '^Result:' | tail -1)
  p=$(printf '%s\n' "$out" | grep -c '###R PASS')
  f=$(printf '%s\n' "$out" | grep -c '###R FAIL')
  s=$(printf '%s\n' "$out" | grep -c '###R SKIP')
  echo "$d | $res | PASS=$p FAIL=$f SKIP=$s"
  if printf '%s\n' "$out" | grep -qi 'error evaluating prelude'; then
    echo "  !! PRELUDE ERROR"
    printf '%s\n' "$out" | grep -i 'prelude' | head -5
  fi
done
