#!/bin/bash
cd "$(dirname "$0")" || exit 1
DIRS="${@:-intl402/ListFormat/prototype intl402/RelativeTimeFormat/prototype intl402/Segmenter/prototype intl402/Segmenter/constructor intl402/NumberFormat/prototype intl402/Locale/prototype intl402/DurationFormat/prototype intl402/Intl/getCanonicalLocales intl402/Intl/supportedValuesOf intl402/DurationFormat/supportedLocalesOf intl402/DisplayNames/prototype}"
for d in $DIRS; do
  rm -f test262_prelude_intl.js.binc
  out=$(./build/run-test262.exe -c test262_full.conf -P test262_prelude_intl.js -d test262/test/$d 2>&1 >/dev/null | grep -E '^Result:')
  echo "$d $out"
done
