#!/bin/bash
cd "$(dirname "$0")" || exit 1
rm -f test262_prelude_intl.js.binc
d="$1"
./build/run-test262.exe -c test262_full.conf -P test262_prelude_intl.js -d test262/test/$d 2>/dev/null | grep '###R FAIL' | sed 's/###R FAIL //'
