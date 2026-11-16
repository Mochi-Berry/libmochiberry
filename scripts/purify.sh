#!/usr/bin/env sh
#
# purify.sh — MBJS purity audit for the libmochiberry fork.
#
# The libmochiberry fork renames every legacy QuickJS symbol to the MBJS
# namespace:
#
#   qjs / qjsc   -> mbjs / mbjsc      (binaries, files, docs)
#   QJS / QJS_   -> MBJS / MBJS_      (build options, env vars)
#   JS_ / js_    -> MBJS_ / mbjs_     (C API + internal helpers)
#   JS<Upper>    -> MBJS<U>           (types: JSValue -> MBJSValue, ...)
#   quickjs      -> mbjs              (case-insensitive)
#
# This tool measures how pure the tree still is: any legacy token found is
# reported as an impurity. JSON is deliberately not an impurity (it is the
# ECMAScript global, not an engine prefix) and neither are "mbjs*" tokens or
# URLs that legitimately point at the upstream QuickJS project.
#
# Usage:
#   sh scripts/purify.sh --check   # audit; exit 1 if any impurity found
#   sh scripts/purify.sh --report  # same, but always exit 0 (CI-friendly report)
#   sh scripts/purify.sh --fix     # apply the deterministic rename rules in place
#
set -u

cd "$(dirname "$0")/.."

MODE="${1:---check}"

# Directories that are never audited. patches/ is excluded on purpose: the
# pure-JS feature patches follow the fork's JS-side convention (js_ helpers,
# JS_/js* naming per the product spec) which is distinct from the engine C
# namespace this gate protects.
EXCLUDE_DIRS='(^|/)(\.git|build|build-.*|dist|gen|node_modules|patches|test262)(/|$)'

# Files that are allowed to mention legacy tokens because they document or
# orchestrate the rename itself.
ALLOWED_FILES='scripts/brand-mbjs\.sh|scripts/purify\.sh|tests/test_purity\.sh|README\.md|mbjs-version\.h'

FILES="$(git ls-files -co --exclude-standard 2>/dev/null \
  | grep -vE "$EXCLUDE_DIRS" \
  | grep -vE "$ALLOWED_FILES" \
  | grep -E '\.(c|h|cc|cpp|js|mjs|md|yml|yaml|txt|sh|man|conf|in|options|cmake|toml|json|html|ts|py|rs)$|(^|/)(Makefile|meson\.build)$' || true)"

if [ -z "$FILES" ]; then
  echo "purify: no files to audit"
  exit 0
fi

# The audit runs perl over every line. It first blanks out MBJS-namespaced
# identifiers and URLs so they can never be mistaken for legacy tokens, then
# matches the legacy QuickJS vocabulary. JSON is excluded with a negative
# lookahead. The proper noun "QuickJS" (referring to the original upstream
# project) is allowed in prose; the identifier forms quickjs / QUICKJS / qjs /
# QJS / JS_ / js_ are not.
AUDIT_PROGRAM='
  my $line = $_;
  my $pat = "\\bqjsc\\b|\\bqjs\\b|\\bQJS\\b|\\bJS_|\\bJS(?!ON)(?=[A-Z])|\\bquickjs\\b|\\bQUICKJS\\b|JS_NAN_BOXING|JS_CHECK_JSVALUE";
  # The legacy C helper prefix js_ is enforced only in engine C/C++ files;
  # the pure-JS feature patches (patches/*.js) use js_ identifiers by design.
  if ($ARGV =~ /\.(?:c|h|cc|cpp)$/) { $pat .= "|\\bjs_"; }
  $line =~ s{https?://[^\s)>\]]+}{}g;      # URLs may point at upstream QuickJS
  $line =~ s/MBJS[A-Za-z0-9_]*//g;         # MBJSValue, MBJS_*, ...
  $line =~ s/mbjs[a-z0-9_]*//g;            # mbjs_malloc, mbjs-libc, ...
  if ($line =~ /$pat/) {
    $count++;
    if ($ENV{PURIFY_VERBOSE}) { print "$ARGV:$.:$line"; }
  }
  $lines++;
  if (eof) { close ARGV; $. = 0; }
  END { print "$count\n$lines\n"; }
'

# Fix mode: deterministic, idempotent rename rules (kept as one perl program).
# URLs are stashed away first so the rules never rewrite an upstream link.
FIX_PROGRAM='
  my @urls;
  s{(https?://[^\s)>\]]+)}{push @urls, $1; "\x00URL" . ($#urls) . "\x00"}ge;
  s/\\fBqjsc\\fR/\\fBmbjsc\\fR/g;      # troff bold tokens (no \b before them)
  s/\\fBqjs\\fR/\\fBmbjs\\fR/g;
  s/\\fBQJS_DUMP_FLAGS\\fR/\\fBMBJS_DUMP_FLAGS\\fR/g;
  s/\bqjsc/mbjsc/g;
  s/\bqjs/mbjs/g;
  s/\bQJS_/MBJS_/g;
  s/\bQJS\b/MBJS/g;
  s/\bJS_/MBJS_/g;
  s/\bjs_/mbjs_/g;
  s/\bJS(?!ON)(?=[A-Z])/MBJS/g;
  s/\bQUICKJS\b/MBJS/g;
  s/\bquickjs/mbjs/gi;
  s/JS_NAN_BOXING/MBJS_NAN_BOXING/g;
  s/JS_CHECK_JSVALUE/MBJS_CHECK_JSVALUE/g;
  s{\x00URL(\d+)\x00}{$urls[$1]}ge;
'

if [ "$MODE" = "--fix" ]; then
  count=0
  for f in $FILES; do
    [ -f "$f" ] || continue
    perl -pi -e "$FIX_PROGRAM" "$f"
    count=$((count + 1))
  done
  echo "purify: applied rename rules to $count files."
  echo "purify: re-running audit..."
  MODE=--check
fi

if [ "$MODE" != "--check" ] && [ "$MODE" != "--report" ]; then
  echo "purify: unknown mode '$MODE' (use --check, --report or --fix)" >&2
  exit 2
fi

OUT="$(printf '%s\n' "$FILES" | xargs perl -ne "$AUDIT_PROGRAM" 2>/dev/null)"
LEGACY="$(printf '%s\n' "$OUT" | sed -n '1p')"
TOTAL="$(printf '%s\n' "$OUT" | sed -n '2p')"
[ -z "$TOTAL" ] && TOTAL=0
[ -z "$LEGACY" ] && LEGACY=0

if [ "$TOTAL" -gt 0 ] 2>/dev/null; then
  SCORE=$(awk -v l="$LEGACY" -v t="$TOTAL" 'BEGIN { printf "%.2f", 100.0 * (1 - l / t) }')
else
  SCORE=100.00
fi

NFILES="$(printf '%s\n' "$FILES" | grep -c .)"

echo "======================================================"
echo "  MBJS purity report (libmochiberry)"
echo "======================================================"
echo "  files scanned : $NFILES"
echo "  lines scanned : $TOTAL"
echo "  legacy lines  : $LEGACY"
echo "  purity score  : $SCORE%"
echo "======================================================"

if [ "$LEGACY" -gt 0 ] 2>/dev/null; then
  echo
  echo "Legacy QuickJS tokens detected (run 'sh scripts/purify.sh --fix' to clean):"
  printf '%s\n' "$FILES" | while IFS= read -r f; do
    [ -f "$f" ] || continue
    perl -ne '
      my $line = $_;
      my $pat = "\\bqjsc\\b|\\bqjs\\b|\\bQJS\\b|\\bJS_|\\bJS(?!ON)(?=[A-Z])|\\bquickjs\\b|\\bQUICKJS\\b|JS_NAN_BOXING|JS_CHECK_JSVALUE";
      if ($ARGV =~ /\.(?:c|h|cc|cpp)$/) { $pat .= "|\\bjs_"; }
      $line =~ s{https?://[^\s)>\]]+}{}g;
      $line =~ s/MBJS[A-Za-z0-9_]*//g;
      $line =~ s/mbjs[a-z0-9_]*//g;
      if ($line =~ /$pat/) {
        print "  $ARGV:$.: " . $line;
      }
      if (eof) { close ARGV; $. = 0; }
    ' "$f"
  done
  if [ "$MODE" = "--check" ]; then
    echo
    echo "purify: FAILED - tree is not 100% MBJS-pure."
    exit 1
  fi
else
  echo "purify: PASSED - tree is 100% MBJS-pure."
fi
exit 0
