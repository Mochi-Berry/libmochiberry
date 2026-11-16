"""codegen2.py -- regenerate gen/*.c and builtin-*.h with upstream qjsc."""
import os
import re
import subprocess
import sys

QJSC = "E:/tmp/qjsng/quickjs-0.18.0/build/qjsc.exe"
ROOT = "E:/bcf/mochiberry"
STAGE = "E:/tmp/opc/codegen"

RULES = [
    (re.compile(r"\bqjsc"), "mbjsc"),
    (re.compile(r"\bqjs"), "mbjs"),
    (re.compile(r"\bQJS_"), "MBJS_"),
    (re.compile(r"\bJS_"), "MBJS_"),
    (re.compile(r"\bjs_"), "mbjs_"),
    (re.compile(r"\bJS(?!ON)(?=[A-Z])"), "MBJS"),
    (re.compile(r"\bQUICKJS\b"), "MBJS"),
    (re.compile(r"\bquickjs", re.I), "mbjs"),
]

JOBS = [
    ("gen/repl.c", ["-ss", "-m"], "repl.js"),
    ("gen/standalone.c", ["-ss", "-m"], "standalone.js"),
    ("gen/function_source.c", ["-e"], "tests/function_source.js"),
    ("gen/hello.c", ["-e"], "examples/hello.js"),
    ("gen/hello_module.c", ["-e", "-m"], "examples/hello_module.js"),
    ("gen/test_fib.c", ["-e", "-m"], "examples/test_fib.js"),
    ("builtin-array-fromasync.h", ["-C", "-ss"], "builtin-array-fromasync.js"),
    ("builtin-iterator-zip.h", ["-C", "-ss"], "builtin-iterator-zip.js"),
    ("builtin-iterator-zip-keyed.h", ["-C", "-ss"], "builtin-iterator-zip-keyed.js"),
]


def brand(text):
    for rx, rep in RULES:
        text = rx.sub(rep, text)
    return text


def read(p):
    with open(p, encoding="utf-8", errors="surrogateescape", newline="") as f:
        return f.read()


def write(p, t):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
        f.write(t)


def main():
    install = "--install" in sys.argv
    os.makedirs(STAGE, exist_ok=True)
    for out_rel, flags, src_rel in JOBS:
        out_stage = os.path.join(STAGE, out_rel)
        os.makedirs(os.path.dirname(out_stage), exist_ok=True)
        cmd = [QJSC] + flags + ["-o", out_stage, src_rel]
        r = subprocess.run(cmd, capture_output=True, text=True, errors="surrogateescape",
                           cwd=ROOT)
        if r.returncode != 0 or not os.path.exists(out_stage):
            print("FAIL %-32s rc=%s %s" % (out_rel, r.returncode, r.stderr.strip()[:120]))
            continue
        text = read(out_stage)
        branded = brand(text)
        write(out_stage, branded)
        print("OK   %-32s %7d bytes  src=%s" % (out_rel, len(branded), src_rel))
        if install:
            tgt = os.path.join(ROOT, out_rel)
            write(tgt, branded)
            print("     -> installed %s" % tgt)
    checks = {
        "gen/standalone.c": [r"mbjsc_standalone\s*\[", r"mbjsc_standalone_size"],
        "gen/repl.c": [r"mbjsc_repl\s*\[", r"mbjsc_repl_size"],
        "builtin-array-fromasync.h": [r"mbjsc_builtin_array_fromasync\s*\["],
        "builtin-iterator-zip.h": [r"mbjsc_builtin_iterator_zip\s*\["],
        "builtin-iterator-zip-keyed.h": [r"mbjsc_builtin_iterator_zip_keyed\s*\["],
    }
    print("\n== sanity simbol ==")
    bad = 0
    for f, syms in checks.items():
        t = read(os.path.join(STAGE, f))
        for s in syms:
            # the array is emitted as "const uint8_t <name>[<n>] = {" so match
            # "<name>[" and not the literal "<name>[]" form.
            ok = re.search(s, t) is not None
            if not ok:
                bad = 1
            print("  %-32s %-38s %s" % (f, s.replace("\\s", " "), "OK" if ok else "HILANG"))
    if bad:
        print("SANITY FAILED: simbol bytecode tidak ditemukan")
        sys.exit(1)


if __name__ == "__main__":
    main()
