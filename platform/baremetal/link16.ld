/* link16.ld — 16-bit real-mode linker script for gcc -m16 output.
 *
 * gcc's -m16 mode emits 32-bit instructions with 16-bit operand-size
 * prefixes. On a 386+ CPU running in real mode those instructions execute
 * with full 32-bit registers and address-size prefixes, so a flat 4 GiB
 * address space is reachable without segments being switched. That makes
 * -m16 output suitable for:
 *
 *   - a real-mode boot environment (386+), loaded at a fixed physical
 *     address (origin below),
 *   - a DOS extender-style loader (the extender switches nothing; the
 *     real-mode 386+ CPU handles the 32-bit prefixed code directly),
 *   - any 16-bit-boot firmware that drops the CPU in real mode.
 *
 * For genuine 8086/8088/286 (16-bit segments, 64 KiB code segment, no
 * 32-bit prefixes) use the ia16-elf-gcc port instead — this script does
 * NOT produce 8086-compatible images.
 *
 * Build (see scripts/cross-build.sh, target x86_16):
 *   cc -m16 -march=i386 -ffreestanding -nostdlib -c mbjs.c -o mbjs16.o
 *   cc -m16 -march=i386 -nostdlib -Wl,-T,link16.ld mbjs16.o -o out.elf
 *   objcopy -O binary out.elf out.bin      # flat image for the loader
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)

/* Where the firmware/loader places the image (physical address). Adjust to
 * the target's real-mode load point. 0x7C00 is the classic boot sector
 * address; most custom OS loaders use something in 0x1000..0x100000. */
ORIGIN = 0x7C00;

ENTRY(_start16)

SECTIONS
{
    . = ORIGIN;

    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
        *(.rodata*.*)
    }

    /* Real-mode code is usually mixed with data in one image; keep data
     * right after code so the loader can place a single contiguous blob. */
    .data : {
        *(.data*)
        *(.sdata*)
    }

    .bss (NOLOAD) : {
        *(.bss*)
        *(.sbss*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
    }
}
