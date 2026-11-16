#!/usr/bin/env sh
#
# make-ota.sh — build an OTA bundle for libmochiberry.
#
# Produces dist/ota/<version>/ containing the engine shared library, the app
# compiled to bytecode, the pure-JS feature patches, a versioned manifest and
# checksums, plus a single zip of the bundle.
#
# Usage:
#   sh scripts/make-ota.sh [version] [app.js]
#
#   version  default 0.1.0     bundle version (also the output dir name)
#   app.js   default examples/app_mochiberry.js
#
set -u

cd "$(dirname "$0")/.."

VERSION="${1:-0.1.0}"
APP_SRC="${2:-examples/app_mochiberry.js}"

case "$VERSION" in
    *[!0-9A-Za-z._-]*) echo "make-ota: invalid version '$VERSION'" >&2; exit 1 ;;
esac

if [ ! -x build/mbjsc ] || [ ! -x build/mbjs ]; then
    echo "building engine first (make)..."
    make -j4 || exit 1
fi

echo "== OTA bundle: $VERSION =="

echo "building libmochiberry..."
sh scripts/build-libmochiberry.sh || exit 1

OUT="dist/ota/$VERSION"
rm -rf "$OUT"
mkdir -p "$OUT/patches"

echo "compiling app bytecode..."
build/mbjsc -b -o "$OUT/app.mbc" "$APP_SRC" || exit 1
cp "$APP_SRC" "$OUT/app.js"

echo "staging engine + patches..."
cp dist/libmochiberry.so "$OUT/libmochiberry.so"
for p in patches/patch_*.js; do
    [ -f "$p" ] && cp "$p" "$OUT/patches/"
done

ENGINE_VERSION="$(build/mbjs --version 2>/dev/null | head -1)"
ENGINE_VERSION="${ENGINE_VERSION:-unknown}"

sha() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }
sz() { wc -c < "$1"; }

emit_file() {
    # $1 = relative path inside $OUT, $2 = role
    f="$OUT/$1"
    printf '    "%s": { "sha256": "%s", "size": %s, "role": "%s" }' \
        "$1" "$(sha "$f")" "$(sz "$f")" "$2"
}

echo "writing manifest.json..."
{
    echo '{'
    echo '  "format": 1,'
    echo '  "package": "my.berrylabfoundation.mochiberry",'
    echo '  "engine": "libmochiberry",'
    echo "  \"version\": \"$VERSION\","
    echo "  \"engine_version\": \"$ENGINE_VERSION\","
    echo '  "abi": "mbjs-bytecode-1",'
    echo '  "upgrade": { "type": "swap", "no_recode": true, "rollback": "previous" },'
    echo '  "files": {'
    FIRST=1
    for entry in "app.mbc:app-bytecode" "app.js:app-source" "libmochiberry.so:engine"; do
        rel="${entry%%:*}"
        role="${entry##*:}"
        [ "$FIRST" -eq 1 ] || echo ','
        emit_file "$rel" "$role"
        FIRST=0
    done
    for p in "$OUT"/patches/*.js; do
        [ -f "$p" ] || continue
        echo ','
        rel="patches/$(basename "$p")"
        emit_file "$rel" "patch"
    done
    echo
    echo '  }'
    echo '}'
} > "$OUT/manifest.json"

echo "writing SHA256SUMS..."
(cd "$OUT" && sha256sum app.mbc app.js libmochiberry.so manifest.json patches/*.js) > "$OUT/SHA256SUMS"

echo "packing zip..."
(cd dist/ota && zip -q -r "libmochiberry-$VERSION-ota.zip" "$VERSION") || {
    echo "make-ota: zip unavailable; bundle left unpacked in dist/ota/$VERSION" >&2
}

echo
echo "== OTA bundle ready =="
find "$OUT" -type f | sort
echo
echo "bundle zip: dist/ota/libmochiberry-$VERSION-ota.zip"
echo "done."
