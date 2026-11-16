; ═══════════════════════════════════════════════════════════════════════════
; lbrt_api.inc — LBRT API (100% BerryCall contract)
; ═══════════════════════════════════════════════════════════════════════════
;
; Sumber kebenaran: kernel/include/berrycall.h — kontrak BerryCall ABI v1
; (nano kernel + LBRT + komponen).  DUA LAPIS:
;
;   Lapis LBRT : lbrt_berrycall()  — layani lokal (COMP_*/DSP/WIN/STOR)
;   Lapis KERNEL: SYS_BERRY_BERRYCALL (0x7E) → sys_berry_berrycall()
;
; Konvensi: berrycall(nomor, a1, a2, a3, a4) — 5 arg.  Kembalian >= 0
; sukses; -1..-9 error (SEBERRY_*).
;
; Makro berry_invoke memuat nomor + arg ke register ABI arsitektur lalu
; memicu SYS_BERRY_BERRYCALL (0x7E).  lbrt_resolve menghitung hash
; FNV-1a compile-time dan memanggil COMP_SYM untuk alamat fungsi.
;
; ═══════════════════════════════════════════════════════════════════════════

; ── Kontrak ABI ──
BERRYCALL_ABI              equ 0x00010000
BERRYCALL_KERNEL_GATEWAY   equ 0x7E

; ── Lapisan pelayan ──
BC_LAYER_KERNEL            equ 0
BC_LAYER_LBRT              equ 1
BC_LAYER_BOTH              equ 2

; ═══════════════════════════════════════════════════════════════════════════
; BERRYIO & FILE (0x100..0x10F)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_IO_READ          equ 0x100  ; a1=fd, a2=buf, a3=len
BERRYCALL_IO_WRITE         equ 0x101  ; a1=fd(1|2), a2=buf, a3=len
BERRYCALL_IO_OPEN          equ 0x102  ; a1=path, a2=flags (FS_O_*)
BERRYCALL_IO_CLOSE         equ 0x103  ; a1=fd
BERRYCALL_IO_CTL           equ 0x104  ; a1=subop, a2=arg
BERRYCALL_IO_STAT          equ 0x105  ; a1=path, a2=buf, a3=len
BERRYCALL_IO_EXEC          equ 0x106  ; a1=path, a2=lang, a3=deadline_ms
BERRYCALL_FS_PERM          equ 0x107  ; a1=subop (FS_PERM_*), a2=path, a3=mode
BERRYCALL_FS_SYNC          equ 0x108  ; a1=fd — commit persist tertunda

; ── Subop BERRYCALL_IO_CTL ──
BC_IOCTL_QUERY_BACKEND     equ 0   ; kembalikan id lapis output aktif
BC_IOCTL_CLEAR             equ 1   ; bersihkan layar (TTY)
BC_IOCTL_CURSOR            equ 2   ; a2 = posisi kursor
BC_IOCTL_QUERY_ROLE        equ 3   ; kembalikan peran output (BC_ROLE_*)
BC_IOCTL_ATTACH_DISPLAY    equ 4   ; bergabung sesi GUI: a2=&bb_display_info_t

; ── Id backend output ──
BC_BACKEND_VGA_SERIAL      equ 1
BC_BACKEND_UEFI_CONOUT     equ 2

; ── Peran output proses ──
BC_ROLE_TERMINAL           equ 0   ; output teks → TTY (VGA+COM1)
BC_ROLE_GUI                equ 1   ; output tampilan → display server/WM

; ═══════════════════════════════════════════════════════════════════════════
; PROSES (0x110..0x11F)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_PROC_GETPID      equ 0x110  ; kembalikan pid proses aktif
BERRYCALL_PROC_LIST        equ 0x111  ; a1=buf, a2=maxlen

; ═══════════════════════════════════════════════════════════════════════════
; WAKTU (0x120..0x12F)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_TIME_TICK        equ 0x120  ; ms monotonic
BERRYCALL_SLEEP            equ 0x121  ; a1=ms

; ═══════════════════════════════════════════════════════════════════════════
; MEMORI (0x130..0x13F)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_MEM_ALLOC        equ 0x130  ; a1=JUMLAH HALAMAN (4 KB)
BERRYCALL_MEM_FREE         equ 0x131  ; a1=alamat, a2=JUMLAH HALAMAN

; ═══════════════════════════════════════════════════════════════════════════
; REGISTRY KOMPONEN — KHUSUS LBRT (0x140..0x14F)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_COMP_REG         equ 0x140  ; jumlah simbol BDL di registry
BERRYCALL_COMP_CALL        equ 0x141  ; a1=hash, a2=argc(0..2), a3/a4=arg
BERRYCALL_BPP_LOAD         equ 0x142  ; a1=&bb_bpp_load_req_t — load .bpp app
BERRYCALL_BPP_MINT         equ 0x143  ; a1=&bb_bpp_mint_req_t — mint token BPP
BERRYCALL_COMP_SYM         equ 0x145  ; a1=hash → alamat ekspor (0 bila tak ada)

; ═══════════════════════════════════════════════════════════════════════════
; LAYANAN TAMPILAN — DISPLAY (0x150..0x15F) — pelayan NullDS (LBRT)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_DSP_QUERY        equ 0x150  ; a1=&bb_display_info_t
BERRYCALL_DSP_MODES        equ 0x151  ; a1=buf, a2=max: "WxH@bpp\n"
BERRYCALL_DSP_SET          equ 0x152  ; a1=w, a2=h, a3=bpp(0=32)
BERRYCALL_DSP_TEXT         equ 0x153  ; kembali ke VGA text 80x25
BERRYCALL_DSP_CANVAS       equ 0x154  ; a1=w, a2=h → id kanvas virtual
BERRYCALL_DSP_CANVAS_FREE  equ 0x155  ; a1=id kanvas
BERRYCALL_DSP_FLIP         equ 0x156  ; a1=id kanvas: flip ke LFB (vsync+blit)
BERRYCALL_DSP_SESSION      equ 0x157  ; a1=1 mulai sesi; a1=0 query status

; ═══════════════════════════════════════════════════════════════════════════
; LAYANAN JENDELA — WINDOW (0x160..0x16F) — pelayan NullWM (LBRT)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_WIN_CREATE       equ 0x160  ; a1=&bb_win_create_t → id jendela
BERRYCALL_WIN_DESTROY      equ 0x161  ; a1=id jendela
BERRYCALL_WIN_MOVE         equ 0x162  ; a1=id, a2=x, a3=y
BERRYCALL_WIN_RESIZE       equ 0x163  ; a1=id, a2=w, a3=h
BERRYCALL_WIN_FOCUS        equ 0x164  ; a1=id: raise + fokus input
BERRYCALL_WIN_FRAME        equ 0x165  ; a1=id, a2=&bb_win_rect_t
BERRYCALL_WIN_INPUT        equ 0x166  ; a1=id, a2=buf, a3=max
BERRYCALL_WIN_LIST         equ 0x167  ; a1=buf, a2=max
BERRYCALL_WIN_WORKSPACE    equ 0x168  ; a1=id, a2=workspace (0,0=query aktif)

; ═══════════════════════════════════════════════════════════════════════════
; LAYANAN AKTIVITAS — ACTIVITY (0x170..0x17F) — pelayan kernel
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_ACT_REG          equ 0x170  ; a1=&name, a2=mode (BC_ACT_*)
BERRYCALL_ACT_UNREG        equ 0x171  ; a1=&name
BERRYCALL_ACT_QUERY        equ 0x172  ; a1=&name, a2=&bb_activity_info_t
BERRYCALL_ACT_BUMP         equ 0x173  ; a1=&name: naikkan count
BERRYCALL_ACT_DROP         equ 0x174  ; a1=&name: turunkan count
BERRYCALL_ACT_LIST         equ 0x175  ; a1=buf, a2=max

BC_ACT_SINGLE              equ 0
BC_ACT_MULTI               equ 1

; ═══════════════════════════════════════════════════════════════════════════
; PENYIMPANAN — STORAGE (0x180..0x18F) — pelayan LBRT
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_STOR_PERSIST     equ 0x180  ; a1=path, a2=data, a3=size (write-through)
BERRYCALL_BDL_LOAD         equ 0x186  ; a1=path: muat .bdl ke registry komponen
BERRYCALL_LBRCT_LOAD       equ 0x187  ; a1=path: parse + muat bundle .lbrct

; ═══════════════════════════════════════════════════════════════════════════
; SISTEM — SYSTEM (0x1E0..0x1EF) — pelayan LBRT
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_PANIC_TRIGGER    equ 0x1E0  ; a1=exc_no(0→30), a2=rip, a3=rsp

; ═══════════════════════════════════════════════════════════════════════════
; UMUM (0x1F0..0x1FF)
; ═══════════════════════════════════════════════════════════════════════════
BERRYCALL_VERSION          equ 0x1FF  ; kembalikan BERRYCALL_ABI

; ═══════════════════════════════════════════════════════════════════════════
; STRUCT LAYOUTS (identik kernel & user — packed)
; ═══════════════════════════════════════════════════════════════════════════

; ── bb_activity_info_t (8 bytes packed) ──
; offset 0: mode  (uint32: BC_ACT_SINGLE/MULTI)
; offset 4: count (uint32: instance berjalan)
ACT_INFO_MODE  equ 0
ACT_INFO_COUNT equ 4

; ── bb_display_info_t (24 bytes packed) ──
; offset  0: w        (uint32)
; offset  4: h        (uint32)
; offset  8: bpp      (uint32)
; offset 12: hz       (uint32)
; offset 16: session  (uint32: 1 = aktif)
; offset 20: canvases (uint32)
DSP_INFO_W        equ 0
DSP_INFO_H        equ 4
DSP_INFO_BPP      equ 8
DSP_INFO_HZ       equ 12
DSP_INFO_SESSION  equ 16
DSP_INFO_CANVASES equ 20

; ── bb_win_create_t (56 bytes packed) ──
; offset  0: title[32]
; offset 32: app_id[20]
; offset 52: x        (int32)
; offset 56: y        (int32)
; offset 60: w        (uint32)
; offset 64: h        (uint32)
; offset 68: workspace(uint32)
WIN_CREATE_TITLE     equ 0
WIN_CREATE_APP_ID    equ 32
WIN_CREATE_X         equ 52
WIN_CREATE_Y         equ 56
WIN_CREATE_W         equ 60
WIN_CREATE_H         equ 64
WIN_CREATE_WORKSPACE equ 68

; ── bb_win_rect_t (16 bytes packed) ──
; offset  0: x (int32)
; offset  4: y (int32)
; offset  8: w (uint32)
; offset 12: h (uint32)
WIN_RECT_X equ 0
WIN_RECT_Y equ 4
WIN_RECT_W equ 8
WIN_RECT_H equ 12

; ── bb_win_event_t (20 bytes packed) ──
; offset  0: id   (uint32)
; offset  4: type (uint32: 1=key, 2=mouse, 3=close)
; offset  8: code (uint32: key ASCII / mouse 1=LMB 2=RMB)
; offset 12: x    (int32: relatif AREA KLIEN)
; offset 16: y    (int32)
WIN_EVENT_ID   equ 0
WIN_EVENT_TYPE equ 4
WIN_EVENT_CODE equ 8
WIN_EVENT_X    equ 12
WIN_EVENT_Y    equ 16

; ── Event types ──
BC_WIN_EVENT_KEY   equ 1
BC_WIN_EVENT_MOUSE equ 2
BC_WIN_EVENT_CLOSE equ 3

; ── bb_win_list_entry_t (44 bytes packed) ──
; offset  0: id        (uint32)
; offset  4: title[32]
; offset 36: workspace (uint32)
; offset 40: focused   (uint32: 1 = terfokus)
; offset 44: visible   (uint32: 1 = di workspace aktif)
WIN_LIST_ID        equ 0
WIN_LIST_TITLE     equ 4
WIN_LIST_WORKSPACE equ 36
WIN_LIST_FOCUSED   equ 40
WIN_LIST_VISIBLE   equ 44

; ═══════════════════════════════════════════════════════════════════════════
; berry_invoke — Panggil BerryCall (gateway 0x7E)
; ═══════════════════════════════════════════════════════════════════════════
;
; berry_invoke nomor, a1, a2, a3, a4
; Memuat argumen ke register ABI arsitektur lalu syscall SYS_BERRY_BERRYCALL.

macro berry_invoke num, a1=0, a2=0, a3=0, a4=0 {
    match =x86_16, TARGET_ARCH \{
        mov ax, SYS_BERRY_BERRYCALL
        mov bx, num
        mov cx, a1
        mov dx, a2
        int 0x80
    \}

    match =i386, TARGET_ARCH \{
        mov eax, SYS_BERRY_BERRYCALL
        mov edi, num
        mov esi, a1
        mov edx, a2
        mov ecx, a3
        int 0x80
    \}

    match =x86_64, TARGET_ARCH \{
        mov rax, SYS_BERRY_BERRYCALL
        mov rdi, num
        mov rsi, a1
        mov rdx, a2
        mov rcx, a3
        mov r8,  a4
        syscall
    \}

    match =arm32, TARGET_ARCH \{
        mov r7, SYS_BERRY_BERRYCALL
        mov r0, num
        mov r1, a1
        mov r2, a2
        mov r3, a3
        svc #0
    \}

    match =arm64, TARGET_ARCH \{
        mov x8, SYS_BERRY_BERRYCALL
        mov x0, num
        mov x1, a1
        mov x2, a2
        mov x3, a3
        svc #0
    \}

    match =riscv32 | riscv64, TARGET_ARCH \{
        li a7, SYS_BERRY_BERRYCALL
        mv a0, num
        mv a1, a1
        mv a2, a2
        mv a3, a3
        ecall
    \}
}

; ═══════════════════════════════════════════════════════════════════════════
; COMP_CALL — Panggil Ekspor BDL via Hash (resolver dinamis)
; ═══════════════════════════════════════════════════════════════════════════
;
; beasm menghitung hash FNV-1a secara compile-time dari nama fungsi,
; lalu memanggil BERRYCALL_COMP_CALL (0x141) — layanan LBRT:
;   a1=hash, a2=argc(0..2), a3/a4 = arg1/arg2
;
; Contoh:
;   berry_comp_call 'bsnd_dsp_polish', 1, buf, len
;   berry_comp_call 'bfb_page_blit', 4, x1, y1, x2, y2

macro berry_comp_call func_name*, argc=0, a1=0, a2=0 {
    local hash_val
    fnv1a_32 hash_val, `func_name

    match =x86_16, TARGET_ARCH \{
        mov ax, SYS_BERRY_BERRYCALL
        mov bx, BERRYCALL_COMP_CALL
        mov cx, hash_val
        mov dx, argc
        int 0x80
    \}

    match =i386, TARGET_ARCH \{
        mov eax, SYS_BERRY_BERRYCALL
        mov edi, BERRYCALL_COMP_CALL
        mov esi, hash_val
        mov edx, argc
        mov ecx, a1
        int 0x80
    \}

    match =x86_64, TARGET_ARCH \{
        mov rax, SYS_BERRY_BERRYCALL
        mov rdi, BERRYCALL_COMP_CALL
        mov rsi, hash_val
        mov rdx, argc
        mov rcx, a1
        mov r8,  a2
        syscall
    \}

    match =arm32, TARGET_ARCH \{
        mov r7, SYS_BERRY_BERRYCALL
        mov r0, BERRYCALL_COMP_CALL
        mov r1, hash_val
        mov r2, argc
        mov r3, a1
        svc #0
    \}

    match =arm64, TARGET_ARCH \{
        mov x8, SYS_BERRY_BERRYCALL
        mov x0, BERRYCALL_COMP_CALL
        mov x1, hash_val
        mov x2, argc
        mov x3, a1
        svc #0
    \}

    match =riscv32 | riscv64, TARGET_ARCH \{
        li a7, SYS_BERRY_BERRYCALL
        li a0, BERRYCALL_COMP_CALL
        mv a1, hash_val
        mv a2, argc
        mv a3, a1
        ecall
    \}
}

; ═══════════════════════════════════════════════════════════════════════════
; lbrt_resolve — Resolve Alamat Ekspor (COMP_SYM 0x145)
; ═══════════════════════════════════════════════════════════════════════════
;
; Return alamat fungsi di RAX/eax/x0/a0 (0 = tidak ada).  Dipakai untuk
; resolve saat init lalu panggil LANGSUNG lewat pointer (tanpa syscall di
; konteks berbahaya — hook exception / PSOD).

macro lbrt_resolve func_name* {
    local hash_val
    fnv1a_32 hash_val, `func_name

    match =x86_64, TARGET_ARCH \{
        mov rax, SYS_BERRY_BERRYCALL
        mov rdi, BERRYCALL_COMP_SYM
        mov rsi, hash_val
        syscall
    \}

    match =i386, TARGET_ARCH \{
        mov eax, SYS_BERRY_BERRYCALL
        mov edi, BERRYCALL_COMP_SYM
        mov esi, hash_val
        int 0x80
    \}

    match =arm64, TARGET_ARCH \{
        mov x8, SYS_BERRY_BERRYCALL
        mov x0, BERRYCALL_COMP_SYM
        mov x1, hash_val
        svc #0
    \}

    match =riscv32 | riscv64, TARGET_ARCH \{
        li a7, SYS_BERRY_BERRYCALL
        li a0, BERRYCALL_COMP_SYM
        mv a1, hash_val
        ecall
    \}
}

; ── Panggil alamat hasil resolve (di RAX/eax/x0/a0) ──
macro berry_call_resolved {
    match =x86_64, TARGET_ARCH \{
        call rax
    \}
    match =i386, TARGET_ARCH \{
        call eax
    \}
    match =arm32, TARGET_ARCH \{
        blx r0
    \}
    match =arm64, TARGET_ARCH \{
        blr x0
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        jalr ra, a0, 0
    \}
}

; ═══════════════════════════════════════════════════════════════════════════
; Convenience Wrappers (BerryCall coverage)
; ═══════════════════════════════════════════════════════════════════════════

; ── I/O ──
macro bc_io_write fd, buf, len {
    berry_invoke BERRYCALL_IO_WRITE, fd, buf, len, 0
}
macro bc_io_read fd, buf, len {
    berry_invoke BERRYCALL_IO_READ, fd, buf, len, 0
}
macro bc_io_open path, flags {
    berry_invoke BERRYCALL_IO_OPEN, path, flags, 0, 0
}
macro bc_io_close fd {
    berry_invoke BERRYCALL_IO_CLOSE, fd, 0, 0, 0
}
macro bc_io_ctl subop, arg {
    berry_invoke BERRYCALL_IO_CTL, subop, arg, 0, 0
}
macro bc_io_exec path, lang, deadline {
    berry_invoke BERRYCALL_IO_EXEC, path, lang, deadline, 0
}

; ── Proses ──
macro bc_getpid {
    berry_invoke BERRYCALL_PROC_GETPID, 0, 0, 0, 0
}
macro bc_proc_list buf, maxlen {
    berry_invoke BERRYCALL_PROC_LIST, buf, maxlen, 0, 0
}

; ── Waktu ──
macro bc_time_tick {
    berry_invoke BERRYCALL_TIME_TICK, 0, 0, 0, 0
}
macro bc_sleep ms {
    berry_invoke BERRYCALL_SLEEP, ms, 0, 0, 0
}

; ── Memori ──
macro bc_mem_alloc pages {
    berry_invoke BERRYCALL_MEM_ALLOC, pages, 0, 0, 0
}
macro bc_mem_free addr, pages {
    berry_invoke BERRYCALL_MEM_FREE, addr, pages, 0, 0
}

; ── Registry komponen ──
macro bc_comp_reg {
    berry_invoke BERRYCALL_COMP_REG, 0, 0, 0, 0
}
macro bc_comp_sym hash_val {
    berry_invoke BERRYCALL_COMP_SYM, hash_val, 0, 0, 0
}

; ── Display ──
macro bc_dsp_query info_ptr {
    berry_invoke BERRYCALL_DSP_QUERY, info_ptr, 0, 0, 0
}
macro bc_dsp_modes buf, maxlen {
    berry_invoke BERRYCALL_DSP_MODES, buf, maxlen, 0, 0
}
macro bc_dsp_set w, h, bpp {
    berry_invoke BERRYCALL_DSP_SET, w, h, bpp, 0
}
macro bc_dsp_text {
    berry_invoke BERRYCALL_DSP_TEXT, 0, 0, 0, 0
}
macro bc_dsp_canvas w, h {
    berry_invoke BERRYCALL_DSP_CANVAS, w, h, 0, 0
}
macro bc_dsp_canvas_free id {
    berry_invoke BERRYCALL_DSP_CANVAS_FREE, id, 0, 0, 0
}
macro bc_dsp_flip id {
    berry_invoke BERRYCALL_DSP_FLIP, id, 0, 0, 0
}
macro bc_dsp_session start {
    berry_invoke BERRYCALL_DSP_SESSION, start, 0, 0, 0
}

; ── Window ──
macro bc_win_create create_ptr {
    berry_invoke BERRYCALL_WIN_CREATE, create_ptr, 0, 0, 0
}
macro bc_win_destroy id {
    berry_invoke BERRYCALL_WIN_DESTROY, id, 0, 0, 0
}
macro bc_win_move id, x, y {
    berry_invoke BERRYCALL_WIN_MOVE, id, x, y, 0
}
macro bc_win_resize id, w, h {
    berry_invoke BERRYCALL_WIN_RESIZE, id, w, h, 0
}
macro bc_win_focus id {
    berry_invoke BERRYCALL_WIN_FOCUS, id, 0, 0, 0
}
macro bc_win_frame id, rect_ptr {
    berry_invoke BERRYCALL_WIN_FRAME, id, rect_ptr, 0, 0
}
macro bc_win_input id, buf, maxlen {
    berry_invoke BERRYCALL_WIN_INPUT, id, buf, maxlen, 0
}
macro bc_win_list buf, maxlen {
    berry_invoke BERRYCALL_WIN_LIST, buf, maxlen, 0, 0
}
macro bc_win_workspace id, ws {
    berry_invoke BERRYCALL_WIN_WORKSPACE, id, ws, 0, 0
}

; ── Activity ──
macro bc_act_reg name, mode {
    berry_invoke BERRYCALL_ACT_REG, name, mode, 0, 0
}
macro bc_act_unreg name {
    berry_invoke BERRYCALL_ACT_UNREG, name, 0, 0, 0
}
macro bc_act_query name, info_ptr {
    berry_invoke BERRYCALL_ACT_QUERY, name, info_ptr, 0, 0
}
macro bc_act_bump name {
    berry_invoke BERRYCALL_ACT_BUMP, name, 0, 0, 0
}
macro bc_act_drop name {
    berry_invoke BERRYCALL_ACT_DROP, name, 0, 0, 0
}
macro bc_act_list buf, maxlen {
    berry_invoke BERRYCALL_ACT_LIST, buf, maxlen, 0, 0
}

; ── Storage ──
macro bc_stor_persist path, data, size {
    berry_invoke BERRYCALL_STOR_PERSIST, path, data, size, 0
}
macro bc_bdl_load path {
    berry_invoke BERRYCALL_BDL_LOAD, path, 0, 0, 0
}
macro bc_lbrct_load path {
    berry_invoke BERRYCALL_LBRCT_LOAD, path, 0, 0, 0
}

; ── System ──
macro bc_panic_trigger exc_no, rip, rsp {
    berry_invoke BERRYCALL_PANIC_TRIGGER, exc_no, rip, rsp, 0
}
macro bc_version {
    berry_invoke BERRYCALL_VERSION, 0, 0, 0, 0
}
