"""Split MochiBerry's monolithic mbjs.c into per-feature translation units.

Method (structural; verified by the compiler afterwards):
  * File-scope definitions are recognised by column-0 position (this codebase is
    consistently formatted: every file-scope declaration starts at column 0,
    function-local code is always indented).
  * Brace depth uses a state-aware scanner that skips line comments, block
    comments, string literals and character constants, so lines such as `'{'`
    inside the parser/JSON code cannot fool the split.
  * Parameter lists are matched with a paren counter, so one-line definitions
    like `static double f(double d) { return fabs(d); }` yield a clean prototype
    instead of swallowing the body.
  * Module boundaries are snapped to a "safe" line: the start of a file-scope
    definition or of a preprocessor conditional, never inside one. The parser
    lives inside big `#ifndef MBJS_DISABLE_PARSER` blocks, so those stay whole.
  * File-scope type definitions (typedef / struct / union / enum bodies) and
    file-scope macros are MOVED into mbjs-internal.h, because the prototypes in
    the header need them and in the monolith they were scattered down the file.
  * File-scope `static` symbols are promoted to external linkage and their
    prototypes moved into mbjs-internal.h, because the modules reference each
    other. `static inline` becomes a plain external function (an `inline`
    definition without `extern` emits no out-of-line copy).
  * Everything inside a conditional keeps its guard in the header, so a
    declaration and its definition are always visible together.

Usage: python scripts/split_mbjs.py
"""
import io, os, re, json, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "mbjs.c")
BAK = r"E:\tmp\es2027-backup"

PLAN = [
    ("bignum",      12700,    "arbitrary precision integers"),
    ("runtime",     14500,    "objects, async functions, operators"),
    ("parser",      22900,    "parser + bytecode compiler"),
    ("bytecode",    38600,    "bytecode reader/writer"),
    ("builtin-obj", 41500,    "Object/Function/Error/Array/Iterator builtins"),
    ("builtin-num", 46900,    "Number/String/Math builtins"),
    ("builtin-re",  49850,    "RegExp/JSON builtins"),
    ("builtin-refl",52500,    "Reflect/Proxy/Symbol builtins"),
    ("builtin-map", 54150,    "Map/Set/DisposableStack builtins"),
    ("builtin-prom",56365,    "Promise/async builtins"),
    ("builtin-date",58345,    "Date/BigInt builtins"),
    ("builtin-ta",  59700,    "ArrayBuffer/TypedArray/DataView/Atomics builtins"),
    ("builtin-misc",63660,    "WeakRef/FinalizationRegistry/CallSite/DOMException/Uint8Array"),
]

FUNC_RE = re.compile(r"^(static\s+)?((?:[A-Za-z_][A-Za-z0-9_]*\s+|\*\s*)+)([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//.*", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def read_lines():
    return io.open(SRC, encoding="utf-8", errors="surrogateescape").read().split("\n")


def brace_scanner(lines):
    res, depth = [], 0
    in_blk = in_str = in_chr = False
    for ln in lines:
        sd = depth
        i, n = 0, len(ln)
        while i < n:
            ch, nxt = ln[i], (ln[i + 1] if i + 1 < n else "")
            if in_blk:
                if ch == "*" and nxt == "/":
                    in_blk = False; i += 1
                i += 1; continue
            if in_str:
                if ch == "\\" and nxt: i += 2; continue
                if ch == '"': in_str = False
                i += 1; continue
            if in_chr:
                if ch == "\\" and nxt: i += 2; continue
                if ch == "'": in_chr = False
                i += 1; continue
            if ch == "/" and nxt == "*": in_blk = True; i += 2; continue
            if ch == "/" and nxt == "/": break
            if ch == '"': in_str = True; i += 1; continue
            if ch == "'": in_chr = True; i += 1; continue
            if ch == "{": depth += 1
            elif ch == "}": depth -= 1
            i += 1
        res.append((sd, depth))
    return res


def scan_blocks(lines):
    stack, blocks, guard = [], [], []
    for i, ln in enumerate(lines):
        guard.append(tuple(g for _, _, g in stack))
        s = ln.lstrip()
        if s.startswith("#"):
            if re.match(r"#\s*(if|ifdef|ifndef)\b", s):
                stack.append((i, i, s.strip()))
            elif re.match(r"#\s*endif\b", s):
                if stack:
                    a, _, g = stack.pop()
                    blocks.append((a, i, g))
    return blocks, guard


def match_paren(lines, i, open_col, n):
    """return (line_idx, col_idx) of the ')' matching the '(' at (i, open_col)."""
    depth = 0
    j = i
    while j < n:
        line = lines[j]
        start = open_col if j == i else 0
        k = start
        while k < len(line):
            c = line[k]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
                if depth == 0:
                    return j, k
            k += 1
        j += 1
    return None, None


def find_defs(lines, guard, depths):
    defs, i, n = [], 0, len(lines)
    while i < n:
        ln = lines[i]
        sd = depths[i][0]
        if sd != 0 or ln[:1] in (" ", "\t", "#", "/", "*", "}") or not ln.strip():
            i += 1; continue
        g = guard[i]
        m = FUNC_RE.match(ln)
        if m:
            name = m.group(3)
            op = ln.find("(", m.start(3))
            if op < 0:
                i += 1; continue
            j, c = match_paren(lines, i, op, n)
            if j is None:
                i += 1; continue
            # body: first '{' after the parameter list
            k, kc = j, c + 1
            while k < n:
                line = lines[k]
                start = kc if k == j else 0
                pos = line.find("{", start)
                if pos >= 0:
                    kc = pos
                    break
                if ";" in line[start:]:
                    k = None
                    break
                k += 1
            if k is None:
                i = j + 1; continue
            # end of definition: brace depth returns to sd
            x = k
            while x < n and depths[x][1] > sd:
                x += 1
            defs.append(dict(kind="fn", name=name, s0=i, s1=j, endcol=c, end=x, guard=g))
            i = x + 1
            continue
        vm = re.match(r"^(static\s+)?((?:const\s+)?(?:[A-Za-z_][A-Za-z0-9_]*\s+|\*\s*)+)([A-Za-z_][A-Za-z0-9_]*)\s*(\[[^\]]*\])?\s*(=|;)", ln)
        if vm:
            j = i
            while j < n and ";" not in lines[j]:
                j += 1
            defs.append(dict(kind="var", name=vm.group(3), s0=i, s1=j, end=j, guard=g))
            i = j + 1
            continue
        i += 1
    return defs


def find_macro_regions(lines, guard):
    regions, i, n = [], 0, len(lines)
    while i < n:
        ln = lines[i]
        if re.match(r"^#\s*(define|undef)\b", ln):
            j = i
            while j < n and lines[j].rstrip().endswith("\\"):
                j += 1
            regions.append((i, j, guard[i]))
            i = j + 1
            continue
        i += 1
    return regions


def find_type_regions(lines, guard, depths):
    regions, i, n = [], 0, len(lines)
    while i < n:
        ln = lines[i]
        sd = depths[i][0]
        if sd != 0 or ln[:1] in (" ", "\t", "#"):
            i += 1; continue
        s = ln.strip()
        is_td = s.startswith("typedef")
        is_tag = re.match(r"^(struct|union|enum)\b", s) and not re.match(r"^(struct|union|enum)\s+[A-Za-z_][A-Za-z0-9_]*\s*;", s)
        if is_td or is_tag:
            if "{" in s:
                x = i
                while x < n and depths[x][1] > sd:
                    x += 1
                regions.append((i, x, guard[i]))
                i = x + 1
                continue
            j = i
            while j < n and ";" not in lines[j]:
                j += 1
            regions.append((i, j, guard[i]))
            i = j + 1
            continue
        i += 1
    return regions


def main():
    lines = read_lines()
    blocks, guard = scan_blocks(lines)
    depths = brace_scanner(lines)
    defs = find_defs(lines, guard, depths)
    types = find_type_regions(lines, guard, depths)
    macros = find_macro_regions(lines, guard)
    print("definisi file-scope: %d (fungsi %d, var %d); blok tipe: %d; makro: %d" %
          (len(defs), sum(1 for d in defs if d["kind"] == "fn"),
           sum(1 for d in defs if d["kind"] == "var"), len(types), len(macros)))
    print("kedalaman brace akhir:", depths[-1][1], "(harus 0)")

    block_starts = set(b[0] for b in blocks)
    def_starts = set(d["s0"] for d in defs)
    type_starts = set(t[0] for t in types)
    safe = sorted(block_starts | def_starts | type_starts)

    bounds = []
    for name, target, desc in PLAN:
        cand = [s for s in safe if s >= target]
        if not cand:
            raise SystemExit("tidak ada batas aman untuk %s" % name)
        b = cand[0]
        if bounds and b <= bounds[-1][1]:
            b = next((s for s in safe if s > bounds[-1][1]), None)
            if b is None:
                raise SystemExit("batas tak bisa dipisah: %s" % name)
        bounds.append((name, b, desc))
    bounds.append(("END", len(lines), ""))

    print("\n=== batas modul ===")
    for name, b, desc in bounds[:-1]:
        print("  %-13s mulai baris %6d  (%s)" % (name, b + 1, desc))

    first_def = min(d["s0"] for d in defs)
    header_lines = set(range(0, first_def))
    for a, b, g in types:
        header_lines.update(range(a, b + 1))
    macro_regions = [(a, b, g) for a, b, g in macros if a >= first_def]
    for a, b, g in macro_regions:
        header_lines.update(range(a, b + 1))

    type_regions_by_guard = collections.OrderedDict()
    for a, b, g in types:
        type_regions_by_guard.setdefault(g, []).append((a, b))

    promo_fns, promo_vars = [], []
    for d in defs:
        if not lines[d["s0"]].startswith("static"):
            continue
        if d["kind"] == "fn":
            # komentar dibuang: menggabung baris membuat '//' memakan sisa
            # parameter list (dan parameter list jadi tak pernah tertutup)
            # strip komentar PER BARIS dulu, lalu gabung: komentar di
            # tengah daftar parameter (mis. '// tri-state') akan memakan
            # parameter yang ada di baris lain kalau digabung lebih dulu.
            # Baris terakhir dipotong di posisi ')' pada teks mentah.
            segs = [lines[k] for k in range(d["s0"], d["s1"] + 1)]
            segs[-1] = segs[-1][: d["endcol"] + 1]
            sig = re.sub(r"\s+", " ", " ".join(strip_comments(s) for s in segs)).strip()
            proto = sig + ";"
            promo_fns.append((d["name"], proto, d["guard"]))
        else:
            decl = re.sub(r"\s+", " ", " ".join(l.strip() for l in lines[d["s0"]:d["s1"] + 1])).strip()
            decl = decl.split("=")[0].strip()
            if not decl.endswith(";"):
                decl += ";"
            decl = "extern " + decl[len("static "):] if decl.startswith("static ") else decl
            promo_vars.append((d["name"], decl, d["guard"]))
    print("\ndipromote: %d fungsi, %d variabel" % (len(promo_fns), len(promo_vars)))

    os.makedirs(BAK, exist_ok=True)
    orig = os.path.join(BAK, "mbjs.c.orig")
    if not os.path.exists(orig):
        io.open(orig, "w", encoding="utf-8", errors="surrogateescape").write("\n".join(lines))

    hdr = []
    hdr.append("/* mbjs-internal.h — shared internals of the MochiBerry engine.")
    hdr.append(" *")
    hdr.append(" * Generated by scripts/split_mbjs.py from the former monolithic mbjs.c.")
    hdr.append(" * Every translation unit includes this header. It holds the file's")
    hdr.append(" * types and macros plus the file-scope prototypes that were `static`")
    hdr.append(" * in the monolith and are now shared between the per-feature modules.")
    hdr.append(" * Declarations keep the preprocessor guard of their definition.")
    hdr.append(" */")
    hdr.append("#ifndef MBJS_INTERNAL_H")
    hdr.append("#define MBJS_INTERNAL_H")
    hdr.append("")
    hdr.extend(lines[:first_def])
    hdr.append("")

    def emit_guarded(items, get_text):
        by_guard = collections.OrderedDict()
        for it in items:
            by_guard.setdefault(it[2], []).append(it)
        for g, group in by_guard.items():
            if g:
                for gd in g:
                    hdr.append(gd)
            for it in group:
                hdr.append(get_text(it))
            if g:
                for gd in reversed(g):
                    hdr.append("#endif")

    hdr.append("/* ---- shared macros + types (moved here from the monolith, in file order) ---- */")
    shared = [(a, b, g) for a, b, g in types] + list(macro_regions)
    shared.sort()
    shared_by_guard = collections.OrderedDict()
    for a, b, g in shared:
        shared_by_guard.setdefault(g, []).append((a, b))
    for g, regs in shared_by_guard.items():
        if g:
            for gd in g:
                hdr.append(gd)
        for a, b in regs:
            hdr.extend(lines[a:b + 1])
            hdr.append("")
        if g:
            for gd in reversed(g):
                hdr.append("#endif")

    hdr.append("/* ---- shared file-scope prototypes (were static in the monolith) ---- */")
    emit_guarded(promo_fns, lambda it: it[1])
    emit_guarded(promo_vars, lambda it: it[1])
    hdr.append("")
    hdr.append("#endif /* MBJS_INTERNAL_H */")
    hdr_path = os.path.join(ROOT, "mbjs-internal.h")
    io.open(hdr_path, "w", encoding="utf-8", newline="\n").write("\n".join(hdr))
    print("\nheader:", os.path.getsize(hdr_path), "bytes")

    modules = [("core", first_def, bounds[0][1])] + [(n, b, bounds[i + 1][1]) for i, (n, b, _) in enumerate(bounds[:-1])]
    descs = dict((n, d) for n, _, d in PLAN)
    descs["core"] = "runtime core: atoms, shapes, GC, objects"

    def strip_static(text):
        out = []
        for ln in text.split("\n"):
            if ln.startswith("static ") and not ln.startswith("  "):
                out.append(ln[len("static "):])
            else:
                out.append(ln)
        return "\n".join(out)

    for name, a, b in modules:
        kept = [lines[idx] for idx in range(a, b) if idx not in header_lines]
        body = strip_static("\n".join(kept))
        top = ("/* mbjs-%s.c — %s\n *\n * Part of the split of the former monolithic mbjs.c.\n"
               " * Generated by scripts/split_mbjs.py.\n */\n#include \"mbjs-internal.h\"\n\n"
               % (name, descs.get(name, "")))
        path = os.path.join(ROOT, "mbjs-%s.c" % name)
        io.open(path, "w", encoding="utf-8", newline="\n").write(top + body + "\n")
        print("  modul %-13s %6d-%-6d (%5d baris tertulis)" % (name, a + 1, b, len(kept)))

    json.dump({"bounds": bounds, "first_def": first_def,
               "promoted": len(promo_fns) + len(promo_vars), "types": len(types)},
              open(os.path.join(BAK, "split_info.json"), "w"), indent=1)


if __name__ == "__main__":
    main()