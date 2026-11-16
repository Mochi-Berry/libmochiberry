/*
 * mbjs-version.h — MochiBerry (libmochiberry) versioning, single source of truth.
 *
 * Two numbers are kept deliberately separate:
 *
 *   MOCHIBERRY_RELEASE — the MochiBerry product version (what `mbjs --version`,
 *                        MBJS_GetVersion(), the package/ABI version report).
 *                        This is the 0.0.x_alpha pre-release line.
 *
 *   QUICKJS_NG_BASE    — the upstream quickjs-ng release this tree was
 *                        forked/taken from, and the date it was taken. The fork
 *                        keeps holding quickjs-ng's versioning as provenance
 *                        without claiming to BE quickjs-ng. This is what
 *                        "kapan diambil + versi quickjs-ng" means.
 *
 * Every version-bearing consumer (mbjs.c, mbjs-cli.c, mbjsc.c, mbjs-jit.c,
 * CMakeLists.txt, meson.build, package.json, update-version.sh) reads the
 * MochiBerry release from MBJS_VERSION_* below; do not hardcode a version
 * anywhere else.
 */

#ifndef MBJS_VERSION_H
#define MBJS_VERSION_H

/* ── MochiBerry release (product version) ─────────────────────────────── */
#define MOCHIBERRY_VERSION_MAJOR 0
#define MOCHIBERRY_VERSION_MINOR 0
#define MOCHIBERRY_VERSION_PATCH 3
#define MOCHIBERRY_VERSION_SUFFIX "_alpha"

/* Version immediately before this one (line history, documented). */
#define MOCHIBERRY_VERSION_PREVIOUS "0.0.2_alpha"

/* ── quickjs-ng base provenance ───────────────────────────────────────── */
/* Version of the upstream quickjs-ng this tree was last synced from.
   QuickJS-NG latest (checked 2026-11-06): v0.18.0.
   See: https://github.com/quickjs-ng/quickjs/releases/tag/v0.18.0 */
#define QUICKJS_NG_BASE_VERSION "0.18.0"
/* Date this provenance reference was last verified/updated. */
#define QUICKJS_NG_BASE_DATE    "2026-11-16"
/* Base name, kept here (and only here) so purify.sh can allow one site. */
#define QUICKJS_NG_BASE_NAME    "quickjs-ng"

/* ── Back-compat aliases: MBJS_VERSION_* == MochiBerry release ──────────
 * mbjs-jit.c (cache key), CMakeLists.txt (VERSION/SOVERSION), meson and
 * run-test262 read these names. */
#define MBJS_VERSION_MAJOR MOCHIBERRY_VERSION_MAJOR
#define MBJS_VERSION_MINOR MOCHIBERRY_VERSION_MINOR
#define MBJS_VERSION_PATCH MOCHIBERRY_VERSION_PATCH
#define MBJS_VERSION_SUFFIX MOCHIBERRY_VERSION_SUFFIX

/* ── Composition ──────────────────────────────────────────────────────── */
/* Numeric-string helpers; kept visible for the macros below. */
#define MBJS_VER_STR2_(x) #x
#define MBJS_VER_STR_(x)  MBJS_VER_STR2_(x)

/* "0.0.3_alpha" — stringifies the numeric MBJS_VERSION_* at expansion site. */
#define MBJS_VERSION_STRING \
    MBJS_VER_STR_(MBJS_VERSION_MAJOR) "." MBJS_VER_STR_(MBJS_VERSION_MINOR) \
    "." MBJS_VER_STR_(MBJS_VERSION_PATCH) MBJS_VERSION_SUFFIX

/* "quickjs-ng 0.16.1" (base name + base version). */
#define QUICKJS_NG_BASE_STRING \
    QUICKJS_NG_BASE_NAME " " QUICKJS_NG_BASE_VERSION

/* NOTE: MBJS_VER_STR_/MBJS_VER_STR2_ are intentionally NOT #undef'd here:
 * MBJS_VERSION_STRING stringifies its operands at EXPANSION time (in mbjs.c,
 * mbjs-cli.c, ...), long after this header is included. Keeping the helpers
 * visible is the only correct arrangement for a token-pasting stringify. */

#endif /* MBJS_VERSION_H */
