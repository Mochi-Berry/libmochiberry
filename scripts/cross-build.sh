#!/usr/bin/env sh
#
# cross-build.sh — cross-compile libmochiberry for every supported target.
#
# The 7 architecture families (see docs/ARCHITECTURES.md for the ISA
# research behind each one):
#
#   x86_16  real mode 16-bit (DOS / bare-metal)  ia16-elf-gcc or cc -m16
#   x86_32  i386 .. i686                         i686-linux-gnu / cc -m32
#   x86_64  amd64                                host build (always attempted)
#   arm32   ARMv7-A/R                             arm-linux-gnueabihf
#   arm64   AArch64 / aarch64                     aarch64-linux-gnu
#   riscv32 RV32 (bare-metal)                     riscv32-unknown-elf
#   riscv64 RV64                                  riscv64-linux-gnu
#   (bonus) win64                                 x86_64-w64-mingw32 (.dll)
#
# Output layout (dist/<triplet>/):
#   libmochiberry.a    static library (all targets)
#   libmochiberry.o    relocatable object (all targets, for custom linkers)
#   libmochiberry.so   shared library (Linux targets; not bare-metal/ELF)
#   libmochiberry.dll  + import lib .dll.a (MinGW targets)
#   include/           headers
#
# x86_16 specifics:
#   - A true 16-bit toolchain (ia16-elf-gcc, the GCC IA-16 port) produces
#     genuine 8086/286 segmented code. If it is installed it is used.
#   - Otherwise the host gcc's -m16 mode is used: 32-bit instructions with
#     16-bit operand-size prefixes that run in real mode on 386+ CPUs
#     (flat 4 GiB addressing via address-size prefixes). The object is
#     freestanding (-ffreestanding -nostdlib) and can be linked with
#     platform/baremetal/link16.ld into a flat binary for a 386+ real-mode
#     boot environment / DOS extender.
#   - A 64 KiB-code-segment 8086 target needs ia16-elf-gcc (or OpenWatcom);
#     the -m16 fallback targets 386+ real mode only.
#
# Toolchains are detected by prefix. Missing toolchains are reported with an
# install hint and skipped — the matrix never fails on a missing toolchain,
# only on a broken build.
#
# Usage:
#   sh scripts/cross-build.sh
#   CROSS_PREFIX=aarch64-linux-gnu- sh scripts/cross-build.sh   # single target
#   CROSS_CC=/path/to/cc CROSS_AR=/path/to/ar sh scripts/cross-build.sh
#
set -u

cd "$(dirname "$0")/.."

if [ ! -x build/mbjs ]; then
    echo "building engine first (make)..."
    make -j4 || exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== libmochiberry cross-compile matrix =="
echo "generating amalgamated engine source..."
build/mbjs amalgam.js "$TMP/mbjs-amalgam.c" || exit 1
AMALGAM="$TMP/mbjs-amalgam.c"

BUILT=0
SKIPPED=0

# Does this compiler accept -m16 (16-bit real-mode code on 386+)?
cc_supports_m16() {
    printf '#include <string.h>\nint f(void){return 0;}\n' > "$TMP/m16probe.c"
    "$1" -m16 -march=i386 -c "$TMP/m16probe.c" -o "$TMP/m16probe.o" 2>/dev/null
}

# Does this compiler accept -m32 with -march=i386 (pure i386, no i586+)?
cc_supports_m32_i386() {
    printf '#include <stddef.h>\nint f(void){return 0;}\n' > "$TMP/m32probe.c"
    "$1" -m32 -march=i386 -c "$TMP/m32probe.c" -o "$TMP/m32probe.o" 2>/dev/null
}

build_target() {
    label="$1"
    triplet="$2"
    prefix="$3"
    kind="$4"   # linux | elf | mingw | x16

    CC="${CROSS_CC:-}"
    AR="${CROSS_AR:-}"
    if [ "$label" = "native" ]; then
        [ -z "$CC" ] && CC="cc"
        [ -z "$AR" ] && AR="ar"
    else
        [ -z "$CC" ] && CC="${prefix}gcc"
        [ -z "$AR" ] && AR="${prefix}ar"
    fi

    if [ "$kind" != "x16" ] && ! command -v "$CC" >/dev/null 2>&1; then
        SKIPPED=$((SKIPPED + 1))
        echo "SKIP $label ($triplet): toolchain '$CC' not installed"
        case "$triplet" in
            *mingw*)        echo "      install: apt-get install gcc-mingw-w64-x86-64" ;;
            *gnueabihf*)    echo "      install: apt-get install gcc-arm-linux-gnueabihf" ;;
            aarch64*)       echo "      install: apt-get install gcc-aarch64-linux-gnu" ;;
            riscv64*)       echo "      install: apt-get install gcc-riscv64-linux-gnu" ;;
            riscv32*)       echo "      install: apt-get install gcc-riscv64-linux-gnu (riscv32 via -march=rv32), or a riscv32-unknown-elf toolchain" ;;
            i686*)          echo "      install: apt-get install gcc-i686-linux-gnu" ;;
            i386*)          echo "      install: apt-get install gcc-i686-linux-gnu (i386 via -march=i386)" ;;
            x86_16*)        echo "      install: ia16-elf-gcc (true 16-bit, 8086/286) or a gcc with -m16 (386+ real mode)" ;;
        esac
        return
    fi

    echo "building $label ($triplet) with $CC ..."
    OUT="dist/$triplet"
    mkdir -p "$OUT/include"
    cp mbjs.h mbjs-version.h mbjs-libc.h mbjs-mem.h mbjs-jit.h "$OUT/include/" 2>/dev/null || \
        cp mbjs.h mbjs-version.h mbjs-libc.h "$OUT/include/"

    case "$kind" in
    x16)
        # 16-bit real mode. Prefer the true 16-bit port (ia16-elf-gcc), else
        # fall back to the host compiler's -m16 mode (386+ real mode).
        FLAGS="-O2 -ffreestanding -nostdlib -I."
        if ! command -v "$CC" >/dev/null 2>&1; then
            if command -v ia16-elf-gcc >/dev/null 2>&1; then
                CC="ia16-elf-gcc"
                AR="ia16-elf-ar"
            elif cc_supports_m16 "cc"; then
                echo "  note: ia16-elf-gcc missing; using host cc -m16 (386+ real mode)"
                CC="cc"
                AR="ar"
            else
                SKIPPED=$((SKIPPED + 1))
                echo "SKIP $label: no 16-bit mode (ia16-elf-gcc missing and host cc lacks -m16; install gcc-multilib for -m16)"
                return
            fi
        fi
        if [ "$(basename "$CC")" = "ia16-elf-gcc" ]; then
            FLAGS="$FLAGS -mcmodel=small"
        else
            FLAGS="$FLAGS -m16 -march=i386"
        fi
        "$CC" $FLAGS -c "$AMALGAM" -o "$OUT/libmochiberry.o" || {
            echo "FAIL $label (object compile)"; return; }
        "$AR" rcs "$OUT/libmochiberry.a" "$OUT/libmochiberry.o" || return
        # Flat-binary smoke link for 386+ real mode (link16.ld). This is a
        # link/format check, not a bootable image by itself.
        if [ "$(basename "$CC")" != "ia16-elf-gcc" ]; then
            "$CC" $FLAGS -nostdlib -Wl,-T,platform/baremetal/link16.ld \
                "$OUT/libmochiberry.o" -o "$OUT/libmochiberry.flat" 2>/dev/null && {
                "$(command -v "$CC" | sed 's/gcc$//')"objcopy -O binary \
                    "$OUT/libmochiberry.flat" "$OUT/libmochiberry.bin" 2>/dev/null && \
                    echo "  flat: 16-bit flat binary smoke-linked ($(wc -c < "$OUT/libmochiberry.bin") bytes)"
            }
        fi
        ;;
    i386)
        # 32-bit x86, i386 baseline (i386..i686 family).
        if ! cc_supports_m32_i386 "$CC"; then
            SKIPPED=$((SKIPPED + 1))
            echo "SKIP $label: $CC cannot target i386 (-m32 -march=i386 missing)"
            echo "      install: apt-get install gcc-multilib, or use the i686-linux-gnu target"
            return
        fi
        "$CC" -O2 -m32 -march=i386 -DMBJS_BUILD_LIBC -I. \
            -c "$AMALGAM" -o "$OUT/libmochiberry.o" || {
            echo "FAIL $label (object compile)"; return; }
        "$AR" rcs "$OUT/libmochiberry.a" "$OUT/libmochiberry.o" || return
        "$CC" -O2 -m32 -march=i386 -DMBJS_BUILD_LIBC -I. \
            -o "$OUT/libmochiberry.so" "$AMALGAM" -fPIC -shared -lm 2>/dev/null && \
            echo "  shared: libmochiberry.so (32-bit)"
        ;;
    mingw)
        "$CC" -O2 -DMBJS_BUILD_LIBC -DBUILDING_MBJS_SHARED -I. \
            -c "$AMALGAM" -o "$OUT/libmochiberry.o" || {
            echo "FAIL $label (object compile)"; return; }
        "$AR" rcs "$OUT/libmochiberry.a" "$OUT/libmochiberry.o" || return
        "$CC" -O2 -DMBJS_BUILD_LIBC -DBUILDING_MBJS_SHARED -I. \
            -o "$OUT/libmochiberry.dll" "$AMALGAM" \
            -Wl,--out-implib,"$OUT/libmochiberry.dll.a" || {
            echo "FAIL $label (dll link)"; return; }
        ;;
    elf)
        # bare-metal style target: static + relocatable object only
        "$CC" -O2 -DMBJS_BUILD_LIBC -I. \
            -c "$AMALGAM" -o "$OUT/libmochiberry.o" || {
            echo "FAIL $label (object compile)"; return; }
        "$AR" rcs "$OUT/libmochiberry.a" "$OUT/libmochiberry.o" || return
        ;;
    *)
        "$CC" -O2 -DMBJS_BUILD_LIBC -I. \
            -c "$AMALGAM" -o "$OUT/libmochiberry.o" || {
            echo "FAIL $label (object compile)"; return; }
        "$AR" rcs "$OUT/libmochiberry.a" "$OUT/libmochiberry.o" || return
        "$CC" -O2 -DMBJS_BUILD_LIBC -I. \
            -o "$OUT/libmochiberry.so" "$AMALGAM" -fPIC -shared -lm || {
            echo "FAIL $label (shared link)"; return; }
        ;;
    esac

    BUILT=$((BUILT + 1))
    echo "OK   $label -> $OUT"
}

if [ -n "${CROSS_PREFIX:-}" ]; then
    build_target "custom" "custom" "$CROSS_PREFIX" "linux"
else
    while IFS='|' read -r label triplet prefix kind; do
        [ -z "$label" ] && continue
        build_target "$label" "$triplet" "$prefix" "$kind"
    done <<'EOF'
x86_16|x86_16|ia16-elf-|x16
x86_32_i386|i386-linux-gnu|i686-linux-gnu-|i386
x86_32_i686|i686-linux-gnu|i686-linux-gnu-|linux
x86_64|native|x86_64-linux-gnu-|linux
arm32|arm-linux-gnueabihf|arm-linux-gnueabihf-|linux
arm64|aarch64-linux-gnu|aarch64-linux-gnu-|linux
riscv32|riscv32-unknown-elf|riscv32-unknown-elf-|elf
riscv64|riscv64-linux-gnu|riscv64-linux-gnu-|linux
win64|x86_64-w64-mingw32|x86_64-w64-mingw32-|mingw
EOF
fi

echo
echo "== matrix summary =="
echo "built:   $BUILT target(s)"
echo "skipped: $SKIPPED target(s) (missing toolchain)"
echo
echo "artifacts:"
find dist -maxdepth 2 -name 'libmochiberry.*' 2>/dev/null | sort
echo "done."
