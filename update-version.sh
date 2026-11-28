#!/bin/sh

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <major.minor.patch>"
    exit 1
fi

VERSION="$1"
BASE="${VERSION%%_*}"
SUFFIX=""
if [ "$VERSION" != "$BASE" ]; then
    SUFFIX="_${VERSION#*_}"
fi
MAJOR=$(echo "$BASE" | cut -d. -f1)
MINOR=$(echo "$BASE" | cut -d. -f2)
PATCH=$(echo "$BASE" | cut -d. -f3)

if [ -z "$MAJOR" ] || [ -z "$MINOR" ] || [ -z "$PATCH" ]; then
    echo "Error: version must be in major.minor.patch[_suffix] format"
    exit 1
fi

# Source of truth: mbjs-version.h (MOCHIBERRY_VERSION_*). The quickjs-ng base
# provenance (QUICKJS_NG_BASE_*) is intentionally NOT touched here.
sed -i.bak \
    -e "s/^#define MOCHIBERRY_VERSION_MAJOR .*/#define MOCHIBERRY_VERSION_MAJOR $MAJOR/" \
    -e "s/^#define MOCHIBERRY_VERSION_MINOR .*/#define MOCHIBERRY_VERSION_MINOR $MINOR/" \
    -e "s/^#define MOCHIBERRY_VERSION_PATCH .*/#define MOCHIBERRY_VERSION_PATCH $PATCH/" \
    -e "s/^#define MOCHIBERRY_VERSION_SUFFIX .*/#define MOCHIBERRY_VERSION_SUFFIX \"$SUFFIX\"/" \
    mbjs-version.h

sed -i.bak \
    -e "s/^  version: '.*'/  version: '$VERSION'/" \
    meson.build

sed -i.bak \
    -e "s/\"version\": \"[^\"]*\"/\"version\": \"$VERSION\"/" \
    package.json

rm -f mbjs-version.h.bak meson.build.bak package.json.bak

echo "MochiBerry version updated to $VERSION"
