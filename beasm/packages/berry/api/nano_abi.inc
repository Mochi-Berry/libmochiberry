; ═══════════════════════════════════════════════════════════════════════════
; nano_abi.inc — Nano Kernel ABI (100% syscall table)
; ═══════════════════════════════════════════════════════════════════════════
;
; Sumber kebenaran: kernel/include/syscall.h — JANGAN duplikasi nomor di
; tempat lain.  Makro nano_call menerjemahkan syscall ID ke instruksi yang
; benar sesuai TARGET_ARCH (int $0x80 / syscall / svc #0 / ecall).
;
; Pemrogram cukup menulis:
;   nano_call SYS_BERRY_SOW, rdi=str, rsi=len
; beasm menangani register syscall per arsitektur.
;
; ═══════════════════════════════════════════════════════════════════════════

; ═══════════════════════════════════════════════════════════════════════════
; STANDARD I/O & PROCESS
; ═══════════════════════════════════════════════════════════════════════════
SYS_BERRY_HARVEST         equ 0x01  ; read() - read data from fd/buffer
SYS_BERRY_SOW             equ 0x02  ; write() - write data to fd/buffer
SYS_BERRY_WITHER          equ 0x05  ; exit() - terminate process
SYS_BERRY_SLEEP           equ 0x06  ; sleep/yield - pause execution

; ═══════════════════════════════════════════════════════════════════════════
; FILE SYSTEM
; ═══════════════════════════════════════════════════════════════════════════
SYS_BERRY_FS_OPEN         equ 0x10  ; open file
SYS_BERRY_FS_CLOSE        equ 0x11  ; close file
SYS_BERRY_FS_READ         equ 0x12  ; read from file
SYS_BERRY_FS_WRITE        equ 0x13  ; write to file
SYS_BERRY_FS_STAT         equ 0x14  ; get file stats
SYS_BERRY_FS_LIST         equ 0x15  ; list directory
SYS_BERRY_FS_UNLINK       equ 0x16  ; remove file (RAM overlay)
SYS_BERRY_FS_RENAME       equ 0x17  ; move/rename file (RAM overlay)
SYS_BERRY_FS_PERM         equ 0x18  ; set permission/cloak per path
SYS_BERRY_FS_EXEC         equ 0x19  ; exec file by path (load + exec)
SYS_BERRY_FS_SYNC         equ 0x1A  ; flush persist commit untuk fd

; ── fs_open flags ──
FS_O_CREATE               equ 0x01  ; create in RAM overlay if absent
FS_O_PERSIST              equ 0x02  ; write-through ke BFS disk (persistent)
FS_O_SYNC                 equ 0x04  ; flush segera setelah setiap write
FS_O_ASYNC                equ 0x08  ; async: persist ditunda ke FS_SYNC

; ── Subop SYS_BERRY_FS_PERM ──
FS_PERM_SET               equ 0     ; path, mode → atur mode permission
FS_PERM_CLOAK_ON          equ 1     ; path → sembunyikan (cloak)
FS_PERM_CLOAK_OFF         equ 2     ; path → tampilkan kembali
FS_PERM_QUERY             equ 3     ; path → kembalikan mode

; ── Bit permission (FS_PERM_SET / FS_PERM_QUERY) ──
FS_PERM_READ              equ 0x01
FS_PERM_WRITE             equ 0x02
FS_PERM_EXEC              equ 0x04
FS_PERM_HIDDEN            equ 0x08  ; cloaked: tak terlihat di list

; ═══════════════════════════════════════════════════════════════════════════
; MEMORY & SHARED MEMORY (Mycelium)
; ═══════════════════════════════════════════════════════════════════════════
SYS_BERRY_MEM_ALLOC       equ 0x20  ; allocate physical RAM (ikut lifecycle exec)
SYS_BERRY_MEM_FREE        equ 0x21  ; free physical RAM
SYS_BERRY_MEM_ALLOC_P     equ 0x22  ; allocate physical RAM PERSISTEN (.bdl)
SYS_BERRY_ISA_DMA         equ 0x23  ; buffer DMA ISA 64 KB (SB16 channel 5)

; ═══════════════════════════════════════════════════════════════════════════
; TTY & INPUT
; ═══════════════════════════════════════════════════════════════════════════
SYS_BERRY_TUI_RENDER      equ 0x40  ; render colored text to VGA
SYS_BERRY_TTY_CTL         equ 0x41  ; cursor control, clear screen
SYS_BERRY_POLL_INPUT      equ 0x42  ; poll keyboard scancodes

; ═══════════════════════════════════════════════════════════════════════════
; GRAPHICS (GUI / .befb) — BDDM (Berry Direct Display Model)
; ═══════════════════════════════════════════════════════════════════════════
SYS_BERRY_GUI_CANVAS      equ 0x50  ; isi bb_fb_info_t di rdi (LFB/mode aktif)
SYS_BERRY_GUI_FLIP        equ 0x51  ; flip: rdi=backbuffer, rsi=size → vsync+blit
SYS_BERRY_GUI_MODE        equ 0x55  ; BDDM mode-set: query/set/text/vsync
SYS_BERRY_RESIN_SCROLL    equ 0x56  ; scrollback konsol (ring buffer)
SYS_BERRY_LBRT_DISPATCH   equ 0x58  ; daftarkan dispatcher BerryCall LBRT

; ── Sub-mode SYS_BERRY_GUI_MODE (rdi) ──
GUI_MODE_QUERY            equ 0     ; query → rsi = bb_fb_info_t*
GUI_MODE_SET              equ 1     ; set → rsi=w, rdx=h, rcx=bpp, r8=hz
GUI_MODE_TEXT             equ 2     ; text → kembali ke VGA text mode
GUI_MODE_VSYNC            equ 3     ; vsync → tunggu vertical retrace
GUI_MODE_SET_KEEP         equ 4     ; setkeep → set + konsol teks GOP tetap aktif

; ═══════════════════════════════════════════════════════════════════════════
; SYSTEM CONTROL
; ═══════════════════════════════════════════════════════════════════════════
SYS_BERRY_CHRONOS         equ 0x60  ; get RTC time (monotonic)
SYS_BERRY_HALT            equ 0x61  ; halt CPU (hlt)
SYS_BERRY_RESTART         equ 0x63  ; hardware reset
SYS_BERRY_DEBUGGER        equ 0x66  ; enable debugger port
SYS_BERRY_LOCALE_GET      equ 0x69  ; get active locale code string
SYS_BERRY_EXEC_BEX        equ 0x6A  ; execute BEX image from user buffer
SYS_BERRY_LOCALE_STR      equ 0x6B  ; get locale string by key
SYS_BERRY_LOCALE_SET      equ 0x6C  ; set active locale by code
SYS_BERRY_LOCALE_LOAD     equ 0x6D  ; load locale from file path
SYS_BERRY_DMESG           equ 0x6E  ; fetch kernel dmesg ring buffer
SYS_BERRY_ACTIVITY        equ 0x6F  ; ABI UBOSIX aktivitas: single vs multi
SYS_BERRY_CORE_OVERRIDE   equ 0x72  ; SEBerry bypass (butuh kunci FNV-1a)
SYS_BERRY_PROC_LIST       equ 0x73  ; list Berry program registry + proc table
SYS_BERRY_PROC_KILL       equ 0x74  ; kill by kernel pid
SYS_BERRY_BKILL           equ 0x75  ; kill by Berry token (BEX_/BEFB_/BDL_)
SYS_BERRY_NBKILL          equ 0x76  ; nano berry kill: KoID0/KoID1 + escalation
SYS_BERRY_SHELL_NESTED    equ 0x77  ; berapa lapis shell tersarang
SYS_BERRY_SYSINFO         equ 0x78  ; isi bb_sysinfo_t

; ── IPC / Inter-Process Notification (BTP — zero-IPC token) ──
SYS_BERRY_BTP             equ 0x7B  ; BTP registry: mint/validate/claim/...
SYS_BERRY_TIMEINFO        equ 0x7C  ; akumulator waktu per-proses
SYS_BERRY_MOUSE           equ 0x7D  ; baca posisi/klik mouse PS/2
SYS_BERRY_BERRYCALL       equ 0x7E  ; gateway BerryCall ABI (kernel/include/berrycall.h)
SYS_BERRY_EXC_HOOK        equ 0x7F  ; daftar/lepas hook exception LBRT
SYS_BERRY_KLOG            equ 0x80  ; append satu baris ke ring dmesg kernel
SYS_BERRY_SMP_INFO        equ 0x81  ; isi bb_smp_info_t (cores/bsp_id/apic_base)
SYS_BERRY_LOCALE_PLURAL   equ 0x82  ; terjemah key dgn bentuk jamak CLDR

; ── Sub-op SYS_BERRY_ACTIVITY (rdi) — ABI UBOSIX aktivitas ──
ACTIVITY_REG              equ 0     ; rsi=&name, rdx=mode (0 single, 1 multi)
ACTIVITY_UNREG            equ 1     ; rsi=&name → 0
ACTIVITY_QUERY            equ 2     ; rsi=&name, rdx=&bb_activity_info_t
ACTIVITY_BUMP             equ 3     ; rsi=&name: naikkan count
ACTIVITY_DROP             equ 4     ; rsi=&name: turunkan count
ACTIVITY_LIST             equ 5     ; rsi=buf, rdx=max: "name mode count\n"

; ── Mode SYS_BERRY_MOUSE (rdi) ──
MOUSE_MODE_READ           equ 0     ; baca state saat ini (x/y/buttons + dirty)

; ── Mode SYS_BERRY_CHRONOS (rdi) ──
CHRONOS_MODE_MS           equ 0     ; ms (ticks)
CHRONOS_MODE_US           equ 1     ; µs (bb_clock_us, resolusi 1 ms)
CHRONOS_MODE_TSC_HZ       equ 2     ; frekuensi TSC (siklus/detik)

; ── Deadline exec (arg ke-5 / r8 dari SYS_BERRY_EXEC_BEX) ──
BERRY_ETIMEDOUT           equ 124   ; -ETIMEDOUT saat deadline lewat

; ── Mode SYS_BERRY_BTP (rdi) — jagad BTP + token sharing ──
BTP_MODE_MINT             equ 0     ; rsi=prefix, rdx=buf, rcx=max → token baru
BTP_MODE_VALIDATE         equ 1     ; rsi=token → 0 valid / -1 invalid
BTP_MODE_CLAIM            equ 2     ; rsi=token, rdx=kind, rcx=pid → catat jagad
BTP_MODE_ACTIVITY         equ 3     ; rsi=token, rdx=activity → set entrypoint
BTP_MODE_SHARE            equ 4     ; rsi=owner, rdx=target, rcx=caps → grant
BTP_MODE_UNSHARE          equ 5     ; rsi=owner, rdx=target → cabut grant
BTP_MODE_SWEEP            equ 6     ; rsi=buf, rdx=max → sapu jagad

; ── Sinyal standar antar-proses ──
BERRY_SIG_NONE            equ 0x00
BERRY_SIG_TERM            equ 0x01
BERRY_SIG_RELOAD          equ 0x02
BERRY_SIG_LANG            equ 0x03
BERRY_SIG_SWAP            equ 0x04

; ── Kode error SEBerry (permission layer) ──
SEBERRY_OK                equ 0
SEBERRY_EUNKNOWN          equ -1
SEBERRY_EVIOLATION        equ -2
SEBERRY_EKEY              equ -4

; ── Process Classes ──
PROC_CLASS_ANY            equ 0xFFFFFFFF
PROC_CLASS_INIT           equ 0x00000001
PROC_CLASS_SHELL          equ 0x00000002
PROC_CLASS_SYSTEM         equ 0x00000004
PROC_CLASS_GUI            equ 0x00000008
PROC_CLASS_USER           equ 0x00000010

; ═══════════════════════════════════════════════════════════════════════════
; STRUCT LAYOUTS (identik kernel & user — packed)
; ═══════════════════════════════════════════════════════════════════════════

; ── bb_mouse_info_t (20 bytes packed) ──
; offset  0: x       (uint32)
; offset  4: y       (uint32)
; offset  8: buttons (uint32: bit0 LMB, bit1 RMB, bit2 MMB)
; offset 12: dirty   (uint32: 1 = paket baru)
; offset 16: wheel   (int32: delta roda terakumulasi)
MOUSE_INFO_X       equ 0
MOUSE_INFO_Y       equ 4
MOUSE_INFO_BUTTONS equ 8
MOUSE_INFO_DIRTY   equ 12
MOUSE_INFO_WHEEL   equ 16

; ── bb_timeinfo_t (40 bytes packed) ──
; offset  0: real_ms  (uint64)
; offset  8: bcall_cnt(uint64)
; offset 16: sys_us   (uint64)
; offset 24: bcall_us (uint64)
; offset 32: kern_us  (uint64)
TIMEINFO_REAL_MS   equ 0
TIMEINFO_BCALL_CNT equ 8
TIMEINFO_SYS_US    equ 16
TIMEINFO_BCALL_US  equ 24
TIMEINFO_KERN_US   equ 32

; ═══════════════════════════════════════════════════════════════════════════
; nano_call — Universal Syscall Macro
; ═══════════════════════════════════════════════════════════════════════════
;
; Memuat nomor syscall ke register syscall arsitektur, lalu memicu trap.
; Argumen program TETAP mengikuti ABI arsitektur (System V / AAPCS / RISC-V),
; jadi pemrogram meletakkan arg sebelum macro ini.
;
; Contoh (x86_64):
;   mov rdi, msg
;   mov rsi, len
;   nano_call SYS_BERRY_SOW

macro nano_call syscall_id {
    ; ── x86_16 (Real Mode — BIOS era) ──
    match =x86_16, TARGET_ARCH \{
        mov ax, syscall_id
        int 0x80
    \}

    ; ── i386 (Protected Mode) ──
    match =i386, TARGET_ARCH \{
        mov eax, syscall_id
        int 0x80
    \}

    ; ── x86_64 (Long Mode) ──
    match =x86_64, TARGET_ARCH \{
        mov rax, syscall_id
        syscall
    \}

    ; ── ARM32 (AArch32) ──
    match =arm32, TARGET_ARCH \{
        mov r7, syscall_id
        svc #0
    \}

    ; ── ARM64 (AArch64) ──
    match =arm64, TARGET_ARCH \{
        mov x8, syscall_id
        svc #0
    \}

    ; ── RISC-V 32/64 ──
    match =riscv32, TARGET_ARCH \{
        li a7, syscall_id
        ecall
    \}
    match =riscv64, TARGET_ARCH \{
        li a7, syscall_id
        ecall
    \}
}

; ═══════════════════════════════════════════════════════════════════════════
; Convenience Wrappers (100% syscall coverage)
; ═══════════════════════════════════════════════════════════════════════════

; ── I/O ──
; Tulis ke stdout/stderr: a1=fd(1|2), a2=buf, a3=len
macro berry_write fd, buf, len {
    match =x86_64, TARGET_ARCH \{
        mov rdi, fd
        mov rsi, buf
        mov rdx, len
    \}
    match =i386, TARGET_ARCH \{
        mov edi, fd
        mov esi, buf
        mov edx, len
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, fd
        mov r1, buf
        mov r2, len
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, fd
        mov x1, buf
        mov x2, len
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, fd
        mv a1, buf
        mv a2, len
    \}
    nano_call SYS_BERRY_SOW
}

; ── Sleep / yield: a1=ms ──
macro berry_sleep ms {
    match =x86_64, TARGET_ARCH \{
        mov rdi, ms
    \}
    match =i386, TARGET_ARCH \{
        mov edi, ms
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, ms
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, ms
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, ms
    \}
    nano_call SYS_BERRY_SLEEP
}

; ── Exit / wither: a1=code ──
macro berry_exit code=0 {
    match =x86_64, TARGET_ARCH \{
        mov rdi, code
    \}
    match =i386, TARGET_ARCH \{
        mov edi, code
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, code
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, code
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, code
    \}
    nano_call SYS_BERRY_WITHER
}

; ── Jam monotonic ──
; Return: RAX/eax/x0/a0 = waktu (mode ms/us/tsc_hz)
macro berry_chronos mode=CHRONOS_MODE_US {
    match =x86_64, TARGET_ARCH \{
        mov rdi, mode
    \}
    match =i386, TARGET_ARCH \{
        mov edi, mode
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, mode
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, mode
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, mode
    \}
    nano_call SYS_BERRY_CHRONOS
}

; ── Alokasi memori fisik: a1=jumlah halaman (4 KB) ──
; Return: RAX/eax/x0/a0 = alamat fisik (0 = gagal)
macro berry_mem_alloc pages {
    match =x86_64, TARGET_ARCH \{
        mov rdi, pages
    \}
    match =i386, TARGET_ARCH \{
        mov edi, pages
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, pages
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, pages
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, pages
    \}
    nano_call SYS_BERRY_MEM_ALLOC
}

; ── Alokasi memori PERSISTEN (.bdl): a1=jumlah halaman ──
macro berry_mem_alloc_p pages {
    match =x86_64, TARGET_ARCH \{
        mov rdi, pages
    \}
    match =i386, TARGET_ARCH \{
        mov edi, pages
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, pages
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, pages
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, pages
    \}
    nano_call SYS_BERRY_MEM_ALLOC_P
}

; ── Bebaskan memori: a1=alamat, a2=jumlah halaman ──
macro berry_mem_free addr, pages {
    match =x86_64, TARGET_ARCH \{
        mov rdi, addr
        mov rsi, pages
    \}
    match =i386, TARGET_ARCH \{
        mov edi, addr
        mov esi, pages
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, addr
        mov r1, pages
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, addr
        mov x1, pages
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, addr
        mv a1, pages
    \}
    nano_call SYS_BERRY_MEM_FREE
}

; ── Poll keyboard: a1=&bb_key_info_t (atau buffer scancode) ──
macro berry_poll_input out_buf {
    match =x86_64, TARGET_ARCH \{
        mov rdi, out_buf
    \}
    match =i386, TARGET_ARCH \{
        mov edi, out_buf
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, out_buf
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, out_buf
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, out_buf
    \}
    nano_call SYS_BERRY_POLL_INPUT
}

; ── Baca mouse: a1=mode (MOUSE_MODE_READ), a2=&bb_mouse_info_t ──
macro berry_mouse mode, out_info {
    match =x86_64, TARGET_ARCH \{
        mov rdi, mode
        mov rsi, out_info
    \}
    match =i386, TARGET_ARCH \{
        mov edi, mode
        mov esi, out_info
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, mode
        mov r1, out_info
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, mode
        mov x1, out_info
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, mode
        mv a1, out_info
    \}
    nano_call SYS_BERRY_MOUSE
}

; ── Query canvas BDDM: a1=&bb_fb_info_t ──
macro berry_gui_canvas out_info {
    match =x86_64, TARGET_ARCH \{
        mov rdi, out_info
    \}
    match =i386, TARGET_ARCH \{
        mov edi, out_info
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, out_info
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, out_info
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, out_info
    \}
    nano_call SYS_BERRY_GUI_CANVAS
}

; ── Flip BDDM: a1=backbuffer, a2=size ──
macro berry_gui_flip backbuf, size {
    match =x86_64, TARGET_ARCH \{
        mov rdi, backbuf
        mov rsi, size
    \}
    match =i386, TARGET_ARCH \{
        mov edi, backbuf
        mov esi, size
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, backbuf
        mov r1, size
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, backbuf
        mov x1, size
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, backbuf
        mv a1, size
    \}
    nano_call SYS_BERRY_GUI_FLIP
}

; ── Mode-set BDDM: a1=submode, a2..=arg sesuai submode ──
macro berry_gui_mode submode, a2=0, a3=0, a4=0, a5=0 {
    match =x86_64, TARGET_ARCH \{
        mov rdi, submode
        mov rsi, a2
        mov rdx, a3
        mov rcx, a4
        mov r8,  a5
    \}
    match =i386, TARGET_ARCH \{
        mov edi, submode
        mov esi, a2
        mov edx, a3
        mov ecx, a4
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, submode
        mov r1, a2
        mov r2, a3
        mov r3, a4
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, submode
        mov x1, a2
        mov x2, a3
        mov x3, a4
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, submode
        mv a1, a2
        mv a2, a3
        mv a3, a4
    \}
    nano_call SYS_BERRY_GUI_MODE
}

; ── BerryCall gateway: a1=nomor, a2..=arg ──
macro berry_berrycall num, a1=0, a2=0, a3=0, a4=0 {
    match =x86_64, TARGET_ARCH \{
        mov rdi, num
        mov rsi, a1
        mov rdx, a2
        mov rcx, a3
        mov r8,  a4
    \}
    match =i386, TARGET_ARCH \{
        mov edi, num
        mov esi, a1
        mov edx, a2
        mov ecx, a3
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, num
        mov r1, a1
        mov r2, a2
        mov r3, a3
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, num
        mov x1, a1
        mov x2, a2
        mov x3, a3
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, num
        mv a1, a1
        mv a2, a2
        mv a3, a3
    \}
    nano_call SYS_BERRY_BERRYCALL
}

; ── BTP registry: a1=submode, a2..=arg ──
macro berry_btp submode, a2=0, a3=0, a4=0 {
    match =x86_64, TARGET_ARCH \{
        mov rdi, submode
        mov rsi, a2
        mov rdx, a3
        mov rcx, a4
    \}
    match =i386, TARGET_ARCH \{
        mov edi, submode
        mov esi, a2
        mov edx, a3
        mov ecx, a4
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, submode
        mov r1, a2
        mov r2, a3
        mov r3, a4
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, submode
        mov x1, a2
        mov x2, a3
        mov x3, a4
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, submode
        mv a1, a2
        mv a2, a3
        mv a3, a4
    \}
    nano_call SYS_BERRY_BTP
}

; ── Locale string: a1=key, a2=buf, a3=maxlen ──
macro berry_locale_str key, buf, maxlen {
    match =x86_64, TARGET_ARCH \{
        mov rdi, key
        mov rsi, buf
        mov rdx, maxlen
    \}
    match =i386, TARGET_ARCH \{
        mov edi, key
        mov esi, buf
        mov edx, maxlen
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, key
        mov r1, buf
        mov r2, maxlen
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, key
        mov x1, buf
        mov x2, maxlen
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, key
        mv a1, buf
        mv a2, maxlen
    \}
    nano_call SYS_BERRY_LOCALE_STR
}

; ── Dmesg fetch: a1=buf, a2=maxlen ──
macro berry_dmesg buf, maxlen {
    match =x86_64, TARGET_ARCH \{
        mov rdi, buf
        mov rsi, maxlen
    \}
    match =i386, TARGET_ARCH \{
        mov edi, buf
        mov esi, maxlen
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, buf
        mov r1, maxlen
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, buf
        mov x1, maxlen
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, buf
        mv a1, maxlen
    \}
    nano_call SYS_BERRY_DMESG
}

; ── Sysinfo: a1=&bb_sysinfo_t ──
macro berry_sysinfo out_info {
    match =x86_64, TARGET_ARCH \{
        mov rdi, out_info
    \}
    match =i386, TARGET_ARCH \{
        mov edi, out_info
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, out_info
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, out_info
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, out_info
    \}
    nano_call SYS_BERRY_SYSINFO
}

; ── SMP info: a1=&bb_smp_info_t ──
macro berry_smp_info out_info {
    match =x86_64, TARGET_ARCH \{
        mov rdi, out_info
    \}
    match =i386, TARGET_ARCH \{
        mov edi, out_info
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, out_info
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, out_info
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, out_info
    \}
    nano_call SYS_BERRY_SMP_INFO
}

; ── Activity (UBOSIX): a1=subop, a2=name, a3=arg ──
macro berry_activity subop, a2=0, a3=0 {
    match =x86_64, TARGET_ARCH \{
        mov rdi, subop
        mov rsi, a2
        mov rdx, a3
    \}
    match =i386, TARGET_ARCH \{
        mov edi, subop
        mov esi, a2
        mov edx, a3
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, subop
        mov r1, a2
        mov r2, a3
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, subop
        mov x1, a2
        mov x2, a3
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, subop
        mv a1, a2
        mv a2, a3
    \}
    nano_call SYS_BERRY_ACTIVITY
}

; ── Klog: a1=buf, a2=len ──
macro berry_klog buf, len {
    match =x86_64, TARGET_ARCH \{
        mov rdi, buf
        mov rsi, len
    \}
    match =i386, TARGET_ARCH \{
        mov edi, buf
        mov esi, len
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, buf
        mov r1, len
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, buf
        mov x1, len
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, buf
        mv a1, len
    \}
    nano_call SYS_BERRY_KLOG
}

; ── Exec BEX dari buffer: a1=buf, a2=size, a3=lang, a4=deadline_ms ──
macro berry_exec_bex buf, size, lang=0, deadline=0 {
    match =x86_64, TARGET_ARCH \{
        mov rdi, buf
        mov rsi, size
        mov rdx, lang
        mov rcx, deadline
    \}
    match =i386, TARGET_ARCH \{
        mov edi, buf
        mov esi, size
        mov edx, lang
        mov ecx, deadline
    \}
    match =arm32, TARGET_ARCH \{
        mov r0, buf
        mov r1, size
        mov r2, lang
        mov r3, deadline
    \}
    match =arm64, TARGET_ARCH \{
        mov x0, buf
        mov x1, size
        mov x2, lang
        mov x3, deadline
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        mv a0, buf
        mv a1, size
        mv a2, lang
        mv a3, deadline
    \}
    nano_call SYS_BERRY_EXEC_BEX
}

; ── Halt CPU ──
macro berry_halt {
    nano_call SYS_BERRY_HALT
}

; ── Restart / reset hardware ──
macro berry_restart {
    nano_call SYS_BERRY_RESTART
}
