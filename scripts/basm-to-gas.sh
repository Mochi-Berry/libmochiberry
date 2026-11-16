#!/usr/bin/env bash
# basm-to-gas.sh — convert Berry OS BASM (Intel syntax) to GNU as (AT&T)
#
# Why: the JS-to-native transpiler emits Berry OS assembly (beasm format) for
# bare-metal / Berry OS binaries. To verify the generated code on a normal
# host, this script rewrites it to GAS AT&T syntax and remaps the Berry
# syscall numbers (SOW=write, WITHER=exit) to their Linux equivalents.
#
# Usage:
#   sh scripts/basm-to-gas.sh in.basm [out.s]
#   sh scripts/basm-to-gas.sh in.basm -o out.s
#   sh scripts/basm-to-gas.sh in.basm --run          # assemble+link+run
#
# Requirements: python3, as (GNU binutils), ld (for --run).

set -eu

IN="${1:-}"
[ -n "$IN" ] || { echo "usage: $0 in.basm [-o out.s|--run]" >&2; exit 2; }
[ -f "$IN" ] || { echo "basm-to-gas: no such file: $IN" >&2; exit 2; }

OUT=""
RUN=0
case "${2:-}" in
  -o) OUT="${3:-}" ;;
  --run) RUN=1 ;;
  "") : ;;
  *) OUT="$2" ;;
esac
[ -n "$OUT" ] || OUT="$(basename "$IN" .basm).s"

# Berry OS modules use the SysV argument registers.  Pick the ABI of the
# host: Linux keeps rdi/rsi/rdx, Windows (MS x64) uses rcx/rdx/r8.
ABI=sysv
case "$(uname -s 2>/dev/null || echo Linux)" in
  MINGW*|MSYS*|CYGWIN*|Windows*) ABI=ms ;;
esac
if [ "${3:-}" = "--abi" ]; then ABI="$4"; fi

python3 - "$IN" "$OUT" "$ABI" <<'PYEOF'
import re, sys

src, out, abi_mode = sys.argv[1], sys.argv[2], sys.argv[3]

REGS = re.compile(r'\b(rax|eax|ax|al|rbx|ebx|bx|bl|rcx|ecx|cx|cl|rdx|edx|dx|dl|rsi|esi|si|sil|rdi|edi|di|dil|rbp|ebp|rsp|esp|r8|r8d|r8w|r8b|r9|r9d|r9w|r9b|r10|r10d|r10w|r10b|r11|r11d|r11w|r11b|r12|r12d|r12w|r12b|r13|r13d|r13w|r13b|r14|r14d|r14w|r14b|r15|r15d|r15w|r15b)\b')

def reg(m):
    return '%' + m.group(0)

# Berry OS modules use the SysV argument registers (rdi, rsi, rdx).  The
# Microsoft x64 ABI passes them in rcx, rdx, r8 instead, so on a Windows
# host the module has to be re-mapped or every bridge call gets garbage
# arguments.  The mapping is applied to the final assembly text.
if abi_mode == "ms":
    ABI_MAP = {
        'rdi': 'rcx', 'edi': 'ecx', 'di': 'cx', 'dil': 'cl',
        'rsi': 'rdx', 'esi': 'edx', 'si': 'dx', 'sil': 'dl',
        'rdx': 'r8', 'edx': 'r8d', 'dx': 'r8w',
    }
    ABI_RE = re.compile(r'%(' + '|'.join(sorted(ABI_MAP, key=len, reverse=True)) + r')\b')
    def abi(s):
        return ABI_RE.sub(lambda m: '%' + ABI_MAP[m.group(1)], s)
else:
    def abi(s):
        return s

# Berry OS syscall numbers -> Linux (only for the syscall-number loads the
# transpiler emits: mov rax, 0x02 (SOW/write) and mov rax, 0x05 (WITHER/exit))
BERRY2LINUX = {'0x02': '1', '0x05': '60'}

def mem(g):
    """[rbp-8] -> -8(%rbp); [rel sym] -> sym(%rip); [rsp] -> (%rsp)"""
    inner = g.group(1).strip()
    m = re.match(r'rel\s+(\w+)\s*([+-])\s*(0x[0-9a-fA-F]+|\d+)', inner)
    if m:
        return '%s%s%s(%%rip)' % (m.group(1), m.group(2), m.group(3))
    m = re.match(r'rel\s+(\w+)', inner)
    if m:
        return '%s(%%rip)' % m.group(1)
    m = re.match(r'(\w+)\s*([+-])\s*(0x[0-9a-fA-F]+|\d+)', inner)
    if m:
        return '%s%s(%%%s)' % (m.group(2), m.group(3), m.group(1))
    m2 = re.match(r'(\w+)', inner)
    if m2 and REGS.match(m2.group(1)):
        return '(%%%s)' % m2.group(1)
    return inner

def conv(line):
    line = line.strip()
    if not line:
        return ''
    if line.startswith(';'):
        return '#' + line[1:]
    if line.startswith("section "):
        sec = re.search(r"'\.?(\w+)'", line).group(1)
        # Use the standard GAS directives: a bare ".section .data" creates a
        # user section *without* the write flag, so the engine struct the
        # module reserves would live in read-only memory and crash on the
        # first memset.  ".data"/".text" select the proper COFF sections.
        if sec == 'data':
            return '.data'
        if sec == 'text':
            return '.text'
        return '.section .%s,"aw"' % sec
    if line.startswith("BERRY_EXPORT") or line.startswith("dd "):
        return ''
    m = re.match(r'(\w+)\s+db\s+\'(.*)\',\s*0$', line)
    if m:
        return '%s: .string "%s"' % (m.group(1), m.group(2))
    # label db 0x1b, 0x4c, ...  ->  label: .byte 0x1b, 0x4c, ...
    m = re.match(r'(\w+)\s+db\s+([^\'].*)$', line)
    if m:
        return '%s: .byte %s' % (m.group(1), m.group(2))
    # bare db 0x01, ...  ->  .byte 0x01, ...
    m = re.match(r'db\s+([^\'].*)$', line)
    if m:
        return '.byte ' + m.group(1)
    # BERRY_IMPORT is a Berry-OS-loader concept; the .quad references below
    # carry the symbols, so the import line itself is dropped on the host.
    if line.startswith('BERRY_IMPORT'):
        return ''
    # dq a, b, c  ->  .quad a, b, c   (symbols become link-time relocs)
    m = re.match(r'(\w+)\s+dq\s+(.*)$', line)
    if m:
        return '%s: .quad %s' % (m.group(1), m.group(2))
    if line.startswith('dq ') or line == 'dq':
        return '.quad' + line[2:]
    # label resq N / resb N -> label: .zero 8N / .zero N
    m = re.match(r'(\w+)\s+resq\s+(\d+)$', line)
    if m:
        return '%s: .zero %s' % (m.group(1), str(8 * int(m.group(2))))
    m = re.match(r'(\w+)\s+resb\s+(\d+)$', line)
    if m:
        return '%s: .zero %s' % (m.group(1), str(int(m.group(2))))
    # align N -> .align N (beasm pads the current section to an N boundary)
    m = re.match(r'align\s+(\d+)$', line)
    if m:
        return '.align %s' % m.group(1)
    m = re.match(r'resq\s+(\d+)$', line)
    if m:
        return '.zero ' + str(8 * int(m.group(1)))
    m = re.match(r'resb\s+(\d+)$', line)
    if m:
        return '.zero ' + str(int(m.group(1)))
    # name = expr  (beasm "$" is current address; GAS uses ".")
    m = re.match(r'(\w+)\s*=\s*(.*)$', line)
    if m:
        return '%s = %s' % (m.group(1), m.group(2).replace('$', '.'))
    if line.endswith(':'):
        if line == '_start:':
            return '.globl _start\n_start:'
        if line == 'main:':
            return '.globl main\nmain:'
        if line == 'mbjs_app_entry:':
            return '.globl mbjs_app_entry\nmbjs_app_entry:'
        return line
    m = re.match(r'(\w+)\s*(.*)$', line)
    if not m:
        return line
    op, rest = m.group(1), m.group(2)
    ops = [o.strip() for o in rest.split(',')] if rest else []
    # movzx rax, al -> movzbl %al, %eax (canonical GAS form)
    if op == 'movzx' and len(ops) == 2:
        return 'movzbl %%%s, %%e%s' % (ops[1], ops[0][1:])
    # Berry syscall-number remap (mov rax, 0xNN right before syscall)
    if op == 'mov' and len(ops) == 2 and ops[0] == 'rax' and ops[1] in BERRY2LINUX:
        ops = ['rax', '$' + BERRY2LINUX[ops[1]]]
    out = [op]
    if len(ops) == 1:
        o = ops[0]
        mm = re.match(r'\[(.*)\]', o)
        if mm:
            # indirect call/jmp through memory needs '*' in AT&T
            out.append(('*' if op in ('call', 'jmp') else '') + mem(mm))
        else:
            r = REGS.sub(reg, o)
            if op in ('call', 'jmp') and r != o:
                r = '*' + r   # call rax  ->  call *%rax
            out.append(r)
    elif len(ops) == 2:
        dst, src = ops
        parts = []
        for o in (src, dst):
            if re.match(r'^0x', o) or o.lstrip('-').isdigit():
                parts.append('$' + o)
            elif re.match(r'^[A-Za-z_]\w*$', o) and not REGS.match(o):
                parts.append('$' + o)   # symbol immediate, e.g. a length const
            else:
                mm = re.match(r'\[(.*)\]', o)
                if mm:
                    parts.append(mem(mm))
                else:
                    parts.append(REGS.sub(reg, o))
        out.append(', '.join(parts))
    return ' '.join(out)

lines = []
with open(src) as f:
    for raw in f:
        lines.append(abi(conv(raw)))
open(out, 'w').write('\n'.join(lines) + '\n')
print("basm-to-gas: %s -> %s (%s ABI)" % (src, out, abi_mode))
PYEOF

if [ "$RUN" = 1 ]; then
  BIN="$(basename "$OUT" .s)"
  as --64 -o "$BIN.o" "$OUT"
  ld -o "$BIN" "$BIN.o"
  echo "basm-to-gas: assembled+linked -> ./$BIN (running)"
  ./"$BIN"
fi
