; ═══════════════════════════════════════════════════════════════════════════
; gui_api.inc — GUI App API (100% DISPLAY/WINDOW/ACTIVITY + libbfb)
; ═══════════════════════════════════════════════════════════════════════════
;
; UBerryNix TIDAK PAKAI IPC tradisional!  GUI app Ring-3 (BEFB) berkomunikasi
; dengan compositor lewat:
;
;   1. berrycall keluarga DISPLAY (0x150..0x15F) — display server (NullDS)
;   2. berrycall keluarga WINDOW (0x160..0x16F)  — window manager (NullWM)
;   3. berrycall keluarga ACTIVITY (0x170..0x17F) — tabel aktivitas kernel
;   4. berrycall COMP_CALL (0x141) → libbfb ekspor (rendering cepat)
;
; Aplikasi TERMINAL tidak pernah menyentuh keluarga ini (io.write → TTY).
; Keluarga ini untuk aplikasi GUI (role = BC_ROLE_GUI via io.ctl).
;
; SELF-CONTAINED: include 'lbrt_api.inc' (konstanta BerryCall + berry_invoke
; + bc_* wrappers) + berryio.inc (namespace berryio_*) — TIDAK perlu include
; manual lain.  Konstanta 0x150..0x17F + 0x141 didefinisikan di lbrt_api.inc.
;
; ═══════════════════════════════════════════════════════════════════════════

; ── Self-contained: konstanta + berry_invoke dari lbrt_api.inc ──
include 'lbrt_api.inc'

; ═══════════════════════════════════════════════════════════════════════════
; STRUCT LAYOUTS (dari berrycall.h)
; ═══════════════════════════════════════════════════════════════════════════

; ── bb_display_info_t (24 bytes packed) ──
DSP_INFO_W        equ 0
DSP_INFO_H        equ 4
DSP_INFO_BPP      equ 8
DSP_INFO_HZ       equ 12
DSP_INFO_SESSION  equ 16
DSP_INFO_CANVASES equ 20

; ── bb_win_create_t (72 bytes packed) ──
; title[32] @ 0, app_id[20] @ 32, x @ 52, y @ 56, w @ 60, h @ 64, ws @ 68
WIN_CREATE_TITLE     equ 0
WIN_CREATE_APP_ID    equ 32
WIN_CREATE_X         equ 52
WIN_CREATE_Y         equ 56
WIN_CREATE_W         equ 60
WIN_CREATE_H         equ 64
WIN_CREATE_WORKSPACE equ 68
WIN_CREATE_SIZE      equ 72

; ── bb_win_rect_t (16 bytes packed) ──
WIN_RECT_X equ 0
WIN_RECT_Y equ 4
WIN_RECT_W equ 8
WIN_RECT_H equ 12

; ── bb_win_event_t (20 bytes packed) ──
WIN_EVENT_ID   equ 0
WIN_EVENT_TYPE equ 4
WIN_EVENT_CODE equ 8
WIN_EVENT_X    equ 12
WIN_EVENT_Y    equ 16

; ── Event types ──
BC_WIN_EVENT_KEY   equ 1
BC_WIN_EVENT_MOUSE equ 2
BC_WIN_EVENT_CLOSE equ 3

; ── bb_win_list_entry_t (48 bytes packed) ──
WIN_LIST_ID        equ 0
WIN_LIST_TITLE     equ 4
WIN_LIST_WORKSPACE equ 36
WIN_LIST_FOCUSED   equ 40
WIN_LIST_VISIBLE   equ 44

; ── bb_activity_info_t (8 bytes packed) ──
ACT_INFO_MODE  equ 0
ACT_INFO_COUNT equ 4

; ── bb_mouse_info_t (20 bytes packed) — kernel/include/syscall.h ──
MOUSE_INFO_X       equ 0
MOUSE_INFO_Y       equ 4
MOUSE_INFO_BUTTONS equ 8
MOUSE_INFO_DIRTY   equ 12
MOUSE_INFO_WHEEL   equ 16

; ═══════════════════════════════════════════════════════════════════════════
; DISPLAY SERVER — berrycall keluarga 0x150..0x157
; ═══════════════════════════════════════════════════════════════════════════

; Query info display (mode aktif + sesi + kanvas)
; a1=&bb_display_info_t
macro gui_display_query info_ptr {
    berry_invoke BERRYCALL_DSP_QUERY, info_ptr, 0, 0, 0
}

; Daftar mode "WxH@bpp\n"
; a1=buf, a2=max
macro gui_display_modes buf, maxlen {
    berry_invoke BERRYCALL_DSP_MODES, buf, maxlen, 0, 0
}

; Set mode utama (Bochs VBE, ring-0)
; a1=w, a2=h, a3=bpp(0=32)
macro gui_display_set w, h, bpp {
    berry_invoke BERRYCALL_DSP_SET, w, h, bpp, 0
}

; Kembali ke VGA text 80x25
macro gui_display_text {
    berry_invoke BERRYCALL_DSP_TEXT, 0, 0, 0, 0
}

; Alokasi kanvas virtual (buffer komposisi di arena DS)
; a1=w, a2=h → id kanvas di RAX/eax/x0
macro gui_display_canvas w, h {
    berry_invoke BERRYCALL_DSP_CANVAS, w, h, 0, 0
}

; Bebaskan kanvas virtual
; a1=id kanvas
macro gui_display_canvas_free id {
    berry_invoke BERRYCALL_DSP_CANVAS_FREE, id, 0, 0, 0
}

; Flip kanvas ke LFB (vsync + blit)
; a1=id kanvas (0 = utama)
macro gui_display_flip id {
    berry_invoke BERRYCALL_DSP_FLIP, id, 0, 0, 0
}

; Sesi display server
; a1=1 mulai sesi; a1=0 query status (1 = aktif)
macro gui_display_session start {
    berry_invoke BERRYCALL_DSP_SESSION, start, 0, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; WINDOW MANAGER — berrycall keluarga 0x160..0x168
; ═══════════════════════════════════════════════════════════════════════════

; Buat jendela
; a1=&bb_win_create_t → id jendela (>= 0) di RAX/eax/x0
macro gui_win_create create_ptr {
    berry_invoke BERRYCALL_WIN_CREATE, create_ptr, 0, 0, 0
}

; Tutup jendela
; a1=id
macro gui_win_destroy id {
    berry_invoke BERRYCALL_WIN_DESTROY, id, 0, 0, 0
}

; Pindah jendela
; a1=id, a2=x, a3=y
macro gui_win_move id, x, y {
    berry_invoke BERRYCALL_WIN_MOVE, id, x, y, 0
}

; Ubah ukuran jendela (min 80x40)
; a1=id, a2=w, a3=h
macro gui_win_resize id, w, h {
    berry_invoke BERRYCALL_WIN_RESIZE, id, w, h, 0
}

; Fokus jendela (raise + input focus)
; a1=id
macro gui_win_focus id {
    berry_invoke BERRYCALL_WIN_FOCUS, id, 0, 0, 0
}

; Baca rect final jendela (bingkai + area klien)
; a1=id, a2=&bb_win_rect_t
macro gui_win_frame id, rect_ptr {
    berry_invoke BERRYCALL_WIN_FRAME, id, rect_ptr, 0, 0
}

; Ambil event yang di-route WM ke jendela
; a1=id, a2=buf, a3=max → jumlah event di RAX
macro gui_win_input id, buf, maxlen {
    berry_invoke BERRYCALL_WIN_INPUT, id, buf, maxlen, 0
}

; Daftar jendela (tasklist)
; a1=buf, a2=max
macro gui_win_list buf, maxlen {
    berry_invoke BERRYCALL_WIN_LIST, buf, maxlen, 0, 0
}

; Workspace
; a1=id, a2=ws (0,0=query aktif; 0,N=ganti aktif)
macro gui_win_workspace id, ws {
    berry_invoke BERRYCALL_WIN_WORKSPACE, id, ws, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; ACTIVITY (UBOSIX) — berrycall keluarga 0x170..0x175
; ═══════════════════════════════════════════════════════════════════════════

BC_ACT_SINGLE equ 0   ; hanya satu instance boleh jalan
BC_ACT_MULTI  equ 1   ; tiap buka = instance baru

macro gui_act_reg name, mode {
    berry_invoke BERRYCALL_ACT_REG, name, mode, 0, 0
}
macro gui_act_unreg name {
    berry_invoke BERRYCALL_ACT_UNREG, name, 0, 0, 0
}
macro gui_act_query name, info_ptr {
    berry_invoke BERRYCALL_ACT_QUERY, name, info_ptr, 0, 0
}
macro gui_act_bump name {
    berry_invoke BERRYCALL_ACT_BUMP, name, 0, 0, 0
}
macro gui_act_drop name {
    berry_invoke BERRYCALL_ACT_DROP, name, 0, 0, 0
}
macro gui_act_list buf, maxlen {
    berry_invoke BERRYCALL_ACT_LIST, buf, maxlen, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; ROLE & ATTACH — io.ctl (0x104) subop
; ═══════════════════════════════════════════════════════════════════════════

BC_IOCTL_QUERY_BACKEND     equ 0
BC_IOCTL_CLEAR             equ 1
BC_IOCTL_CURSOR            equ 2
BC_IOCTL_QUERY_ROLE        equ 3
BC_IOCTL_ATTACH_DISPLAY    equ 4

BC_ROLE_TERMINAL           equ 0
BC_ROLE_GUI                equ 1

; Query peran output (TERMINAL vs GUI)
macro gui_query_role {
    berry_invoke BERRYCALL_IO_CTL, BC_IOCTL_QUERY_ROLE, 0, 0, 0
}

; Bergabung ke sesi display server
; a2=&bb_display_info_t → id kanvas utama di RAX
macro gui_attach_display info_ptr {
    berry_invoke BERRYCALL_IO_CTL, BC_IOCTL_ATTACH_DISPLAY, info_ptr, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; RENDER CEPAT — berrycall COMP_CALL (0x141) → libbfb ekspor
; ═══════════════════════════════════════════════════════════════════════════
;
; Untuk rendering per-frame, aplikasi GUI memanggil libbfb lewat COMP_CALL:
; beasm menghitung hash FNV-1a compile-time dari nama ekspor.
;
; Contoh:
;   gui_comp_call 'bfb_put_pixel', 2, x, color   ; argc=2, arg1=x, arg2=color

macro gui_comp_call func_name*, argc=0, a1=0, a2=0 {
    local hash_val
    fnv1a_32 hash_val, `func_name

    match =x86_64, TARGET_ARCH \{
        mov rax, SYS_BERRY_BERRYCALL
        mov rdi, BERRYCALL_COMP_CALL
        mov rsi, hash_val
        mov rdx, argc
        mov rcx, a1
        mov r8,  a2
        syscall
    \}

    match =i386, TARGET_ARCH \{
        mov eax, SYS_BERRY_BERRYCALL
        mov edi, BERRYCALL_COMP_CALL
        mov esi, hash_val
        mov edx, argc
        mov ecx, a1
        int 0x80
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

; ── Convenience render (via COMP_CALL) ──

; Put pixel: bfb_put_pixel(x, y, color)
macro gui_pixel x, y, color {
    gui_comp_call 'bfb_put_pixel', 3, x, (y shl 32 or color), 0
}

; Draw text: bfb_draw_text(x, y, str, color)
macro gui_text x, y, str_ptr, color {
    gui_comp_call 'bfb_draw_text', 4, x, (y shl 32 or (str_ptr and 0xFFFFFFFF)), color, 0
}

; Page init: bfb_page_init(&cfg)
macro gui_page_init cfg_ptr {
    gui_comp_call 'bfb_page_init', 1, cfg_ptr, 0
}

; Page blit (dirty-rect): bfb_page_blit(x, y, w, h)
macro gui_page_blit x, y, w, h {
    gui_comp_call 'bfb_page_blit', 4, x, (y shl 32 or w), h, 0
}

; Page vsync ok: bfb_page_vsync_ok()
macro gui_page_vsync {
    gui_comp_call 'bfb_page_vsync_ok', 0, 0, 0
}

; Blit full (fallback vsync): bfb_blit_full()
macro gui_blit_full {
    gui_comp_call 'bfb_blit_full', 0, 0, 0
}

; Query bfb: bfb_query(subop, out)
macro gui_query subop, out_ptr {
    gui_comp_call 'bfb_query', 2, subop, out_ptr
}

; ═══════════════════════════════════════════════════════════════════════════
; INPUT — mouse & keyboard (syscall langsung, bukan IPC)
; ═══════════════════════════════════════════════════════════════════════════

; Baca mouse PS/2: a1=mode(0=read), a2=&bb_mouse_info_t
macro gui_mouse_read info_ptr {
    match =x86_64, TARGET_ARCH \{
        mov rax, SYS_BERRY_MOUSE
        mov rdi, 0          ; MOUSE_MODE_READ
        mov rsi, info_ptr
        syscall
    \}
    match =i386, TARGET_ARCH \{
        mov eax, SYS_BERRY_MOUSE
        mov edi, 0
        mov esi, info_ptr
        int 0x80
    \}
    match =arm64, TARGET_ARCH \{
        mov x8, SYS_BERRY_MOUSE
        mov x0, 0
        mov x1, info_ptr
        svc #0
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        li a7, SYS_BERRY_MOUSE
        li a0, 0
        mv a1, info_ptr
        ecall
    \}
}

; Poll keyboard: a1=&buf (scancode)
macro gui_key_poll out_buf {
    match =x86_64, TARGET_ARCH \{
        mov rax, SYS_BERRY_POLL_INPUT
        mov rdi, out_buf
        syscall
    \}
    match =i386, TARGET_ARCH \{
        mov eax, SYS_BERRY_POLL_INPUT
        mov edi, out_buf
        int 0x80
    \}
    match =arm64, TARGET_ARCH \{
        mov x8, SYS_BERRY_POLL_INPUT
        mov x0, out_buf
        svc #0
    \}
    match =riscv32 | riscv64, TARGET_ARCH \{
        li a7, SYS_BERRY_POLL_INPUT
        mv a0, out_buf
        ecall
    \}
}
