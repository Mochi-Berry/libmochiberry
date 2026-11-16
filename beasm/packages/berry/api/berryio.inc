; ═══════════════════════════════════════════════════════════════════════════
; berryio.inc — Library Modular BerryIO (beasm / fasmg)
; ═══════════════════════════════════════════════════════════════════════════
;
; BerryIO = keluarga I/O + layanan sistem Berry, SATU sumber kebenaran:
; kernel/include/berrycall.h (kontrak BerryCall ABI v1, dua lapis).
;
; File ini SELF-CONTAINED: include 'lbrt_api.inc' (konstanta + berry_invoke),
; lalu menambahkan makro berryio_* yang MEMBUNGKUS seluruh keluarga kontrak
; ke satu namespace: io.*, fs.*, proc.*, time.*, mem.*, comp.*, display.*,
; window.*, activity.*, storage.*, system.* — plus layout struct biner yang
; identik dengan kernel/include/*_format.h.
;
; Cara pakai (dari .basm fasmg):
;   include 'packages/berry/api/berryio.inc'
;   ...
;   berryio_io_write 1, msg, len        ; io.write → SOW (TTY)
;   berryio_dsp_query info_ptr           ; display.query → NullDS
;   berryio_win_create create_ptr        ; window.create → NullWM
;   berryio_act_bump 'kalkulator'        ; activity.bump → kernel
;
; Semua makro menaruh return di register ABI (rax/eax/x0/a0) lewat
; berry_invoke — konvensi berrycall 5 arg.  Status >= 0 sukses;
; -1..-9 error (SEBERRY_*).
;
; ═══════════════════════════════════════════════════════════════════════════

; ── Self-contained: konstanta + berry_invoke dari lbrt_api.inc ──
include 'lbrt_api.inc'

; ═══════════════════════════════════════════════════════════════════════════
; 1. BERRYIO & FILE  (0x100..0x10F)  — io.read/write/open/close/ctl/stat/exec
; ═══════════════════════════════════════════════════════════════════════════

; fd standar
BERRYIO_FD_STDOUT equ 1
BERRYIO_FD_STDERR equ 2

; Flags open (identik FS_O_* kernel / UBOSIX_O_*)
BERRYIO_O_CREATE  equ 0x01
BERRYIO_O_PERSIST equ 0x02
BERRYIO_O_SYNC    equ 0x04
BERRYIO_O_ASYNC   equ 0x08

; Bit permission (FS_PERM_*)
BERRYIO_PERM_READ   equ 0x01
BERRYIO_PERM_WRITE  equ 0x02
BERRYIO_PERM_EXEC   equ 0x04
BERRYIO_PERM_HIDDEN equ 0x08

; Tulis ke fd (1/2 = stdout/stderr); a2=buf, a3=len
macro berryio_io_write fd, buf, len {
    berry_invoke BERRYCALL_IO_WRITE, fd, buf, len, 0
}

; Baca dari fd; a2=buf, a3=len → jumlah byte
macro berryio_io_read fd, buf, len {
    berry_invoke BERRYCALL_IO_READ, fd, buf, len, 0
}

; Buka berkas; a1=path, a2=flags → fd
macro berryio_io_open path, flags {
    berry_invoke BERRYCALL_IO_OPEN, path, flags, 0, 0
}

; Tutup fd
macro berryio_io_close fd {
    berry_invoke BERRYCALL_IO_CLOSE, fd, 0, 0, 0
}

; Stat berkas; a1=path, a2=buf, a3=len
macro berryio_io_stat path, buf, len {
    berry_invoke BERRYCALL_IO_STAT, path, buf, len, 0
}

; Control BerryIO; a1=subop (BERRYIO_CTL_*), a2=arg
macro berryio_io_ctl subop, arg {
    berry_invoke BERRYCALL_IO_CTL, subop, arg, 0, 0
}

; Baca SELURUH file by path (open+read+close) — pola ubosix_fs_read
; a1=path, a2=buf, a3=max → byte terbaca / < 0 error
macro berryio_fs_read path, buf, max {
    local fd, n, tmp
    berryio_io_open path, 0
    ; hasil di rax
    mov fd, rax
    test fd, fd
    js .fail
    berryio_io_read fd, buf, max
    mov n, rax
    berryio_io_close fd
    mov rax, n
    jmp .done
.fail:
    mov rax, -1
.done:
}

; Eksekusi file by path (BEX/BEFB/BDL) — butuh mode EXEC
macro berryio_fs_exec path, lang, deadline {
    berry_invoke BERRYCALL_IO_EXEC, path, lang, deadline, 0
}

; Set permission / cloak; a1=subop (BERRYIO_PERM_*_OP), a2=path, a3=mode
macro berryio_fs_perm subop, path, mode {
    berry_invoke BERRYCALL_FS_PERM, subop, path, mode, 0
}

; Commit persist tertunda (FS_O_ASYNC) utk fd terbuka
macro berryio_fs_sync fd {
    berry_invoke BERRYCALL_FS_SYNC, fd, 0, 0, 0
}

; ── Subop BERRYCALL_IO_CTL (identik BC_IOCTL_* di lbrt_api.inc) ──
BERRYIO_CTL_QUERY_BACKEND  equ BC_IOCTL_QUERY_BACKEND   ; 0
BERRYIO_CTL_CLEAR          equ BC_IOCTL_CLEAR           ; 1
BERRYIO_CTL_CURSOR         equ BC_IOCTL_CURSOR          ; 2
BERRYIO_CTL_QUERY_ROLE     equ BC_IOCTL_QUERY_ROLE      ; 3
BERRYIO_CTL_ATTACH_DISPLAY equ BC_IOCTL_ATTACH_DISPLAY  ; 4

; Backend output
BERRYIO_BACKEND_VGA_SERIAL equ BC_BACKEND_VGA_SERIAL
BERRYIO_BACKEND_UEFI_CONOUT equ BC_BACKEND_UEFI_CONOUT

; Peran output proses
BERRYIO_ROLE_TERMINAL equ BC_ROLE_TERMINAL
BERRYIO_ROLE_GUI      equ BC_ROLE_GUI

; ═══════════════════════════════════════════════════════════════════════════
; 2. PROSES  (0x110..0x11F)  — proc.getpid / proc.list
; ═══════════════════════════════════════════════════════════════════════════
macro berryio_proc_getpid {
    berry_invoke BERRYCALL_PROC_GETPID, 0, 0, 0, 0
}
macro berryio_proc_list buf, maxlen {
    berry_invoke BERRYCALL_PROC_LIST, buf, maxlen, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 3. WAKTU  (0x120..0x12F)  — time.tick / time.sleep
; ═══════════════════════════════════════════════════════════════════════════
macro berryio_time_tick {
    berry_invoke BERRYCALL_TIME_TICK, 0, 0, 0, 0
}
macro berryio_sleep ms {
    berry_invoke BERRYCALL_SLEEP, ms, 0, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 4. MEMORI  (0x130..0x13F)  — mem.alloc/free (halaman 4 KB)
; ═══════════════════════════════════════════════════════════════════════════
macro berryio_mem_alloc pages {
    berry_invoke BERRYCALL_MEM_ALLOC, pages, 0, 0, 0
}
macro berryio_mem_free addr, pages {
    berry_invoke BERRYCALL_MEM_FREE, addr, pages, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 5. REGISTRY KOMPONEN  (0x140..0x14F)  — comp.reg/call/sym + bpp
;    LAYANAN LBRT (kernel mengembalikan -1)
; ═══════════════════════════════════════════════════════════════════════════
; Jumlah simbol BDL di registry
macro berryio_comp_reg {
    berry_invoke BERRYCALL_COMP_REG, 0, 0, 0, 0
}
; Panggil ekspor BDL via hash FNV-1a: a1=hash, a2=argc(0..2), a3/a4=arg
macro berryio_comp_call hash_val, argc=0, a1=0, a2=0 {
    berry_invoke BERRYCALL_COMP_CALL, hash_val, argc, a1, a2
}
; Hash ekspor → alamat fungsi (0 bila tak ada).  Resolve saat init,
; lalu panggil LANGSUNG lewat pointer (tanpa syscall di konteks bahaya).
macro berryio_comp_sym hash_val {
    berry_invoke BERRYCALL_COMP_SYM, hash_val, 0, 0, 0
}
; Muat .bpp GUI app dari /system/app/ — LBRT bpp_loader
macro berryio_bpp_load req_ptr {
    berry_invoke BERRYCALL_BPP_LOAD, req_ptr, 0, 0, 0
}
; Mint token BPP segar per instance jendela
macro berryio_bpp_mint req_ptr {
    berry_invoke BERRYCALL_BPP_MINT, req_ptr, 0, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 6. DISPLAY SERVER  (0x150..0x15F)  — display.* (NullDS)
;    Pelayan: LBRT / komponen aktif.  Kernel: -1.
; ═══════════════════════════════════════════════════════════════════════════
; Layout bb_display_info_t (24 B packed) — kernel/include/befb_format.h
BERRYIO_DSP_INFO_W        equ 0
BERRYIO_DSP_INFO_H        equ 4
BERRYIO_DSP_INFO_BPP      equ 8
BERRYIO_DSP_INFO_HZ       equ 12
BERRYIO_DSP_INFO_SESSION  equ 16
BERRYIO_DSP_INFO_CANVASES equ 20

macro berryio_dsp_query info_ptr {
    berry_invoke BERRYCALL_DSP_QUERY, info_ptr, 0, 0, 0
}
macro berryio_dsp_modes buf, maxlen {
    berry_invoke BERRYCALL_DSP_MODES, buf, maxlen, 0, 0
}
macro berryio_dsp_set w, h, bpp {
    berry_invoke BERRYCALL_DSP_SET, w, h, bpp, 0
}
macro berryio_dsp_text {
    berry_invoke BERRYCALL_DSP_TEXT, 0, 0, 0, 0
}
macro berryio_dsp_canvas w, h {
    berry_invoke BERRYCALL_DSP_CANVAS, w, h, 0, 0
}
macro berryio_dsp_canvas_free id {
    berry_invoke BERRYCALL_DSP_CANVAS_FREE, id, 0, 0, 0
}
macro berryio_dsp_flip id {
    berry_invoke BERRYCALL_DSP_FLIP, id, 0, 0, 0
}
macro berryio_dsp_session start {
    berry_invoke BERRYCALL_DSP_SESSION, start, 0, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 7. WINDOW MANAGER  (0x160..0x16F)  — window.* (NullWM)
;    Pelayan: LBRT / komponen aktif.  Kernel: -1.
; ═══════════════════════════════════════════════════════════════════════════
; Layout bb_win_create_t (72 B packed) — kernel/include/befb_format.h
;   title[32] @ 0, app_id[20] @ 32, x @ 52, y @ 56, w @ 60, h @ 64, ws @ 68
BERRYIO_WIN_CREATE_TITLE     equ 0
BERRYIO_WIN_CREATE_APP_ID    equ 32
BERRYIO_WIN_CREATE_X         equ 52
BERRYIO_WIN_CREATE_Y         equ 56
BERRYIO_WIN_CREATE_W         equ 60
BERRYIO_WIN_CREATE_H         equ 64
BERRYIO_WIN_CREATE_WORKSPACE equ 68
BERRYIO_WIN_CREATE_SIZE      equ 72

; Layout bb_win_rect_t (16 B packed)
BERRYIO_WIN_RECT_X equ 0
BERRYIO_WIN_RECT_Y equ 4
BERRYIO_WIN_RECT_W equ 8
BERRYIO_WIN_RECT_H equ 12

; Layout bb_win_event_t (20 B packed)
BERRYIO_WIN_EVENT_ID   equ 0
BERRYIO_WIN_EVENT_TYPE equ 4
BERRYIO_WIN_EVENT_CODE equ 8
BERRYIO_WIN_EVENT_X    equ 12
BERRYIO_WIN_EVENT_Y    equ 16

; Jenis event
BERRYIO_WIN_EVENT_KEY   equ 1
BERRYIO_WIN_EVENT_MOUSE equ 2
BERRYIO_WIN_EVENT_CLOSE equ 3

; Layout bb_win_list_entry_t (48 B packed)
BERRYIO_WIN_LIST_ID        equ 0
BERRYIO_WIN_LIST_TITLE     equ 4
BERRYIO_WIN_LIST_WORKSPACE equ 36
BERRYIO_WIN_LIST_FOCUSED   equ 40
BERRYIO_WIN_LIST_VISIBLE   equ 44

macro berryio_win_create create_ptr {
    berry_invoke BERRYCALL_WIN_CREATE, create_ptr, 0, 0, 0
}
macro berryio_win_destroy id {
    berry_invoke BERRYCALL_WIN_DESTROY, id, 0, 0, 0
}
macro berryio_win_move id, x, y {
    berry_invoke BERRYCALL_WIN_MOVE, id, x, y, 0
}
macro berryio_win_resize id, w, h {
    berry_invoke BERRYCALL_WIN_RESIZE, id, w, h, 0
}
macro berryio_win_focus id {
    berry_invoke BERRYCALL_WIN_FOCUS, id, 0, 0, 0
}
macro berryio_win_frame id, rect_ptr {
    berry_invoke BERRYCALL_WIN_FRAME, id, rect_ptr, 0, 0
}
macro berryio_win_input id, buf, maxlen {
    berry_invoke BERRYCALL_WIN_INPUT, id, buf, maxlen, 0
}
macro berryio_win_list buf, maxlen {
    berry_invoke BERRYCALL_WIN_LIST, buf, maxlen, 0, 0
}
macro berryio_win_workspace id, ws {
    berry_invoke BERRYCALL_WIN_WORKSPACE, id, ws, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 8. AKTIVITAS  (0x170..0x17F)  — activity.* (tabel kernel, LBRT forward)
; ═══════════════════════════════════════════════════════════════════════════
BERRYIO_ACT_SINGLE equ BC_ACT_SINGLE   ; 0
BERRYIO_ACT_MULTI  equ BC_ACT_MULTI    ; 1

; Layout bb_activity_info_t (8 B packed)
BERRYIO_ACT_INFO_MODE  equ 0
BERRYIO_ACT_INFO_COUNT equ 4

macro berryio_act_reg name, mode {
    berry_invoke BERRYCALL_ACT_REG, name, mode, 0, 0
}
macro berryio_act_unreg name {
    berry_invoke BERRYCALL_ACT_UNREG, name, 0, 0, 0
}
macro berryio_act_query name, info_ptr {
    berry_invoke BERRYCALL_ACT_QUERY, name, info_ptr, 0, 0
}
macro berryio_act_bump name {
    berry_invoke BERRYCALL_ACT_BUMP, name, 0, 0, 0
}
macro berryio_act_drop name {
    berry_invoke BERRYCALL_ACT_DROP, name, 0, 0, 0
}
macro berryio_act_list buf, maxlen {
    berry_invoke BERRYCALL_ACT_LIST, buf, maxlen, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 9. STORAGE  (0x180..0x18F)  — storage.persist / bdl.load / lbrct.load
;    Pelayan: LBRT (storage driver).  Kernel: jembatan persist saja.
; ═══════════════════════════════════════════════════════════════════════════
; Tulis file (RAM overlay) ke DISK fisik — BFS (BIOS) / ESP FAT32 (UEFI)
macro berryio_stor_persist path, data, size {
    berry_invoke BERRYCALL_STOR_PERSIST, path, data, size, 0
}
; Muat .bdl dari FS ke registry komponen (hotswap)
macro berryio_bdl_load path {
    berry_invoke BERRYCALL_BDL_LOAD, path, 0, 0, 0
}
; Muat bundle .lbrct (RTCF) — semua .bdl masuk registry
macro berryio_lbrct_load path {
    berry_invoke BERRYCALL_LBRCT_LOAD, path, 0, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; 10. SISTEM  (0x1E0..0x1FF)  — panic.trigger / version
; ═══════════════════════════════════════════════════════════════════════════
; Pemicu PSOD terprogram (libberrypanic): a1=exc_no (0→30), a2=rip, a3=rsp
macro berryio_panic_trigger exc_no, rip, rsp {
    berry_invoke BERRYCALL_PANIC_TRIGGER, exc_no, rip, rsp, 0
}
; Kembalikan BERRYCALL_ABI (versi kontrak)
macro berryio_version {
    berry_invoke BERRYCALL_VERSION, 0, 0, 0, 0
}

; ═══════════════════════════════════════════════════════════════════════════
; ALIAS — nama pendek selaras lbrt_api.inc (bc_* -> berryio_*)
; ═══════════════════════════════════════════════════════════════════════════
; Pemakai lama lbrt_api.inc bisa memakai bc_*; pemakai baru cukup berryio_*.
; Tidak perlu alias eksplisit — lbrt_api.inc sudah mendefinisikan bc_*.
