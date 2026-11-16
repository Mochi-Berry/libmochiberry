#!/usr/bin/env python3
"""Classify test262 results_full*.txt into per-year and per-cluster buckets.

Usage: python classify.py <results_file.txt> [--json out.json]
Joins '###R PASS|FAIL|SKIP <path>' lines to t262_yearof.json (path -> year or null).
Pitfall: ###R lines end with \r\n; must .strip() after split or every join misses.
"""
import json, re, sys, collections, os

results_path = sys.argv[1]
yearof = json.load(open("F:/bcf/mochiberry/t262_yearof.json", encoding="utf-8"))

prefix = "test262/test/"

year_stat = collections.defaultdict(lambda: [0, 0, 0])  # year -> [pass, fail, skip]
fail_paths = collections.defaultdict(list)              # year -> [path]
unknown = [0, 0, 0]  # pass, fail, skip for paths missing from yearof map

pat = re.compile(r"^###R (PASS|FAIL|SKIP)\s+(.*)$")
total = collections.Counter()

with open(results_path, encoding="utf-8", errors="replace") as f:
    for line in f:
        m = pat.match(line.rstrip("\r\n"))
        if not m:
            continue
        status, path = m.group(1), m.group(2).strip().replace("\\", "/")
        i = path.find("test262/test/")
        if i >= 0:
            rel = path[i + len("test262/test/"):]
        elif path.startswith("test/"):
            rel = path[5:]
        else:
            rel = path
        total[status] += 1
        year = yearof.get(rel)
        if year is None:
            # annexB / no-feature -> null; bucket as 'pre2020' for reporting
            bucket = "pre2020"
            slot = 2 if False else None  # keep null-year separate bucket but same stat array
        else:
            bucket = str(year)
        idx = {"PASS": 0, "FAIL": 1, "SKIP": 2}[status]
        year_stat[bucket][idx] += 1
        if status == "FAIL":
            fail_paths[bucket].append(rel)

print("=" * 60)
print("TOTAL: pass=%d fail=%d skip=%d  (from %s)" % (
    total["PASS"], total["FAIL"], total["SKIP"], os.path.basename(results_path)))
print("=" * 60)

def pct(p, f):
    tot = p + f
    return "%.1f%%" % (100.0 * p / tot) if tot else "n/a"

# Order: released editions first (2015..2026), then pre2020, then 2027+
def ykey(y):
    if y == "2027": return 9999
    if y == "pre2020": return 0
    try: return int(y)
    except: return 5000

print("\n-- PER-YEAR CONFORMANCE (pass, fail, skip, pass%%) --")
for y in sorted(year_stat, key=ykey):
    p, f_, s = year_stat[y]
    print("  %-8s pass=%-6d fail=%-6d skip=%-5d  pass%%=%s" % (y, p, f_, s, pct(p, f_)))

# For target editions, show top failing clusters (group by first 3 path segments)
TARGETS = ["2020", "2021", "2022", "2023", "2024", "2025", "2026", "2027", "pre2020"]
print("\n-- TOP FAILING CLUSTERS (per target year, grouped by 3-seg dir) --")
for y in TARGETS:
    paths = fail_paths.get(y, [])
    if not paths:
        continue
    groups = collections.Counter()
    for rel in paths:
        parts = rel.split("/")
        key = "/".join(parts[:3]) if len(parts) > 3 else rel
        groups[key] += 1
    print("\n[%s] total fails=%d, top clusters:" % (y, len(paths)))
    for k, n in groups.most_common(12):
        print("    %5d  %s" % (n, k))

# Dump failing paths for small released years to inspect individually
if "--dump" in sys.argv:
    out = {}
    for y in TARGETS:
        out[y] = fail_paths.get(y, [])
    json.dump(out, open("F:/bcf/mochiberry/fails_byyear.json", "w"), indent=1)
    print("\n[written] F:/bcf/mochiberry/fails_byyear.json")
