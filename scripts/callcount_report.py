#!/usr/bin/env python3
"""callcount_report.py -- turn raw -finstrument-functions counts into a report.

Reads the raw "<hex addr> <count>" files produced by profcount.c, subtracts the
baseline (empty script) run, maps the addresses to symbol names with nm, and
prints:
  * the top functions by call count for each engine
  * a side-by-side comparison of the functions both engines share
    (MochiBerry's MBJS_/mbjs_ names are normalised to QuickJS's JS_/js_ names)
"""
import os
import re
import subprocess
import sys

OUT = sys.argv[1]
OUT_WIN = sys.argv[2]
MB_EXE = sys.argv[3]
QJS_EXE = sys.argv[4]
# nm must be given as an explicit path: the native Windows python inherits an
# MSYS-style PATH, where "/c/..." entries do not resolve for CreateProcess.
NM = sys.argv[5] if len(sys.argv) > 5 else "nm"


def read_counts(path):
    """returns (counts, module_base); addresses are stored relative to the
    module base because the exe is loaded at a randomised address (ASLR)."""
    counts = {}
    base = 0
    if not os.path.exists(path):
        return counts, base
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                if parts[0] == "BASE":
                    base = int(parts[1], 16)
                    continue
                counts[int(parts[0], 16)] = int(parts[1])
    return counts, base


def pe_image_base(exe):
    """ImageBase from the PE optional header (PE32+ -> offset 24)."""
    with open(exe, "rb") as f:
        data = f.read(0x1000)
    e_lfanew = int.from_bytes(data[0x3C:0x40], "little")
    opt = e_lfanew + 24
    magic = int.from_bytes(data[opt:opt + 2], "little")
    if magic == 0x20B:          # PE32+
        return int.from_bytes(data[opt + 24:opt + 32], "little")
    if magic == 0x10B:          # PE32
        return int.from_bytes(data[opt + 28:opt + 32], "little")
    return 0


def symbols(exe):
    """address -> (name, size) for every symbol in the executable."""
    out = subprocess.run([NM, "-S", exe], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 4:
            try:
                addr = int(parts[0], 16)
                size = int(parts[1], 16)
            except ValueError:
                continue
            syms.append((addr, size, parts[3]))
        elif len(parts) == 3 and parts[1] in "tT":
            try:
                addr = int(parts[0], 16)
            except ValueError:
                continue
            syms.append((addr, 0, parts[2]))
    syms.sort()
    return syms


def build_map(syms):
    """addr -> name, using the containing symbol (largest size wins)."""
    m = {}
    for addr, size, name in syms:
        if name.startswith("."):
            continue
        if size:
            for a in range(addr, addr + size, 16):
                m.setdefault(a, name)
        m.setdefault(addr, name)
    # fill the holes with the nearest preceding symbol
    addrs = sorted(syms)
    table = {}
    prev = None
    for a in addrs:
        if prev is not None:
            for x in range(prev[0] + 1, a[0]):
                table[x] = prev[2]
        prev = a
        table[a] = prev[2]
    table.update(m)
    return table


def normalise(name):
    if name.startswith("MBJS_"):
        return "JS_" + name[5:]
    if name.startswith("mbjs_"):
        return "js_" + name[5:]
    return name


def workload_counts(base_file, work_file, symmap):
    base, bbase = read_counts(os.path.join(OUT, base_file))
    work, wbase = read_counts(os.path.join(OUT, work_file))
    # work and base come from different processes: ASLR may place them at
    # different module bases, so normalise both to file-relative offsets.
    boff = {addr - bbase: c for addr, c in base.items()}
    per_name = {}
    for addr, c in work.items():
        off = addr - wbase
        d = c - boff.get(off, 0)
        if d <= 0:
            continue
        name = symmap.get(off, "off:0x%x" % off)
        per_name[name] = per_name.get(name, 0) + d
    return per_name


def file_relative_map(exe):
    """nm addresses -> file offsets relative to the PE ImageBase."""
    img = pe_image_base(exe)
    syms = symbols(exe)
    shifted = [(addr - img, size, name) for addr, size, name in syms]
    return build_map(shifted), img


mb_map, mb_img = file_relative_map(MB_EXE)
qjs_map, qjs_img = file_relative_map(QJS_EXE)
print("PE ImageBase: MochiBerry=0x%x  QuickJS-ng=0x%x" % (mb_img, qjs_img))

mb = workload_counts("mb_base.raw", "mb_work.raw", mb_map)
qjs = workload_counts("qjs_base.raw", "qjs_work.raw", qjs_map)

print("=" * 72)
print("TOP FUNCTIONS BY CALL COUNT (workload only, baseline subtracted)")
print("=" * 72)
for label, data in (("MochiBerry", mb), ("QuickJS-ng 0.18.0", qjs)):
    total = sum(data.values())
    print("\n--- %s   total C calls in workload: %d ---" % (label, total))
    for name, c in sorted(data.items(), key=lambda kv: -kv[1])[:20]:
        print("  %10d  %s" % (c, name))

print()
print("=" * 72)
print("SIDE BY SIDE (normalised names, shared functions only)")
print("=" * 72)
qnorm = {}
for name, c in qjs.items():
    qnorm[normalise(name)] = qnorm.get(normalise(name), 0) + c
rows = []
for name, c in mb.items():
    n = normalise(name)
    if n in qnorm:
        rows.append((c, qnorm[n], name, n))
rows.sort(key=lambda r: -(r[0] + r[1]))
print("  %10s %10s %8s  %s" % ("MochiBerry", "QuickJS-ng", "ratio", "function"))
for c, q, raw, n in rows[:25]:
    ratio = (c / q) if q else float("inf")
    print("  %10d %10d %8.2f  %s" % (c, q, ratio, raw))
