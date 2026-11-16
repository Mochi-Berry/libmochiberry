/*
 * takegawa.js — TGPM, the Takegawa package manager for MochiBerry.
 *
 * TGPM is a *global, content-addressed* package manager: every package tree is
 * hashed (SHA-256) and stored once in a shared store.  Projects reference the
 * single stored copy through a link (symlink/junction) so installations never
 * pile up the way per-project node_modules do.  A copy mode is available for
 * tools that insist on a real node_modules directory, and an existing
 * node_modules tree is also understood.
 *
 * Public API is exported for tests and embedding; the CLI runs when this file
 * is the entry module (import.meta.main) or when a native launcher sets
 * globalThis.__TGPM_MAIN__.
 */

import * as std from "mbjs:std";
import * as os from "mbjs:os";

export const VERSION = "0.1.0";
export const MANIFEST_NAMES = ["takegawa.json", "package.json"];
export const LOCKFILE = "takegawa.lock";
export const MODULES_DIR = "takegawa_modules";
export const NODE_MODULES_DIR = "node_modules";
export const STORE_FORMAT = 1;

/* ------------------------------------------------------------------ */
/* small helpers                                                       */
/* ------------------------------------------------------------------ */

function isWindows() {
  return os.platform === "win32";
}

function println(s) {
  std.printf("%s\n", s === undefined ? "" : String(s));
}

function warn(s) {
  std.printf("tgpm: %s\n", String(s));
}

function fail(msg) {
  throw new Error(msg);
}

function toSlash(p) {
  return String(p).replace(/\\/g, "/");
}

export function isAbsolutePath(p) {
  p = toSlash(p);
  return p.startsWith("/") || /^[A-Za-z]:\//.test(p) || /^[A-Za-z]:$/.test(p);
}

export function cwd() {
  const r = os.getcwd();
  return Array.isArray(r) ? r[0] : r;
}

export function normalize(p) {
  p = toSlash(p);
  let drive = "";
  const dm = /^([A-Za-z]:)/.exec(p);
  if (dm) {
    drive = dm[1];
    p = p.slice(2);
  }
  const abs = p.startsWith("/");
  const out = [];
  for (const seg of p.split("/")) {
    if (seg === "" || seg === ".") continue;
    if (seg === "..") {
      if (out.length && out[out.length - 1] !== "..") out.pop();
      else if (!abs) out.push("..");
    } else {
      out.push(seg);
    }
  }
  let s = out.join("/");
  if (drive) return drive + "/" + s;
  if (abs) return "/" + s;
  return s === "" ? "." : s;
}

export function join(...parts) {
  const bits = [];
  for (const part of parts) {
    if (part === undefined || part === null || part === "") continue;
    bits.push(String(part));
  }
  if (bits.length === 0) return ".";
  return normalize(bits.join("/"));
}

export function dirname(p) {
  p = toSlash(p).replace(/\/+$/, "");
  const i = p.lastIndexOf("/");
  if (i < 0) return ".";
  if (i === 0) return "/";
  if (/^[A-Za-z]:$/.test(p.slice(0, i))) return p.slice(0, i) + "/";
  return p.slice(0, i);
}

export function basename(p) {
  p = toSlash(p).replace(/\/+$/, "");
  const i = p.lastIndexOf("/");
  return i < 0 ? p : p.slice(i + 1);
}

export function resolvePath(p, base) {
  if (base === undefined) base = cwd();
  return isAbsolutePath(p) ? normalize(p) : normalize(join(base, p));
}

export function relativePath(from, to) {
  from = normalize(isAbsolutePath(from) ? from : resolvePath(from));
  to = normalize(isAbsolutePath(to) ? to : resolvePath(to));
  const a = toSlash(from).split("/").filter(Boolean);
  const b = toSlash(to).split("/").filter(Boolean);
  // On Windows compare case-insensitively for the shared prefix.
  let i = 0;
  while (i < a.length && i < b.length && (isWindows() ? a[i].toLowerCase() === b[i].toLowerCase() : a[i] === b[i])) i++;
  const up = [];
  for (let k = i; k < a.length; k++) up.push("..");
  return up.concat(b.slice(i)).join("/") || ".";
}

/* ------------------------------------------------------------------ */
/* SHA-256 (pure JS; the engine exposes no crypto.subtle)              */
/* ------------------------------------------------------------------ */

const SHA256_K = [
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
];

function utf8Bytes(s) {
  if (typeof TextEncoder !== "undefined") return new TextEncoder().encode(s);
  const out = [];
  for (let i = 0; i < s.length; i++) {
    let c = s.charCodeAt(i);
    if (c < 0x80) out.push(c);
    else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
    else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length) {
      const c2 = s.charCodeAt(++i);
      const cp = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
      out.push(0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 63), 0x80 | ((cp >> 6) & 63), 0x80 | (cp & 63));
    } else out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
  }
  return new Uint8Array(out);
}

function rotr(x, n) {
  return ((x >>> n) | (x << (32 - n))) >>> 0;
}

export function sha256(input) {
  const bytes = typeof input === "string" ? utf8Bytes(input) : input;
  const H = [0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19];
  const len = bytes.length;
  const bitLen = len * 8;
  const total = ((len + 9 + 63) >> 6) << 6;
  const buf = new Uint8Array(total);
  buf.set(bytes);
  buf[len] = 0x80;
  const hi = Math.floor(bitLen / 0x100000000);
  const lo = bitLen >>> 0;
  buf[total - 8] = (hi >>> 24) & 255;
  buf[total - 7] = (hi >>> 16) & 255;
  buf[total - 6] = (hi >>> 8) & 255;
  buf[total - 5] = hi & 255;
  buf[total - 4] = (lo >>> 24) & 255;
  buf[total - 3] = (lo >>> 16) & 255;
  buf[total - 2] = (lo >>> 8) & 255;
  buf[total - 1] = lo & 255;

  const w = new Uint32Array(64);
  for (let off = 0; off < total; off += 64) {
    for (let i = 0; i < 16; i++) {
      w[i] = ((buf[off + i * 4] << 24) | (buf[off + i * 4 + 1] << 16) | (buf[off + i * 4 + 2] << 8) | buf[off + i * 4 + 3]) >>> 0;
    }
    for (let i = 16; i < 64; i++) {
      const s0 = (rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3)) >>> 0;
      const s1 = (rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10)) >>> 0;
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) >>> 0;
    }
    let a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
    for (let i = 0; i < 64; i++) {
      const S1 = (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) >>> 0;
      const ch = ((e & f) ^ (~e & g)) >>> 0;
      const t1 = (h + S1 + ch + SHA256_K[i] + w[i]) >>> 0;
      const S0 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) >>> 0;
      const maj = ((a & b) ^ (a & c) ^ (b & c)) >>> 0;
      const t2 = (S0 + maj) >>> 0;
      h = g; g = f; f = e; e = (d + t1) >>> 0;
      d = c; c = b; b = a; a = (t1 + t2) >>> 0;
    }
    H[0] = (H[0] + a) >>> 0; H[1] = (H[1] + b) >>> 0;
    H[2] = (H[2] + c) >>> 0; H[3] = (H[3] + d) >>> 0;
    H[4] = (H[4] + e) >>> 0; H[5] = (H[5] + f) >>> 0;
    H[6] = (H[6] + g) >>> 0; H[7] = (H[7] + h) >>> 0;
  }
  const out = new Uint8Array(32);
  for (let i = 0; i < 8; i++) {
    out[i * 4] = (H[i] >>> 24) & 255;
    out[i * 4 + 1] = (H[i] >>> 16) & 255;
    out[i * 4 + 2] = (H[i] >>> 8) & 255;
    out[i * 4 + 3] = H[i] & 255;
  }
  return out;
}

export function toHex(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i++) s += (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16);
  return s;
}

export function sha256Hex(input) {
  return toHex(sha256(input));
}

/* ------------------------------------------------------------------ */
/* semver                                                              */
/* ------------------------------------------------------------------ */

export function parseVersion(v) {
  if (typeof v !== "string") return null;
  const m = /^\s*[v=\s]*(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?\s*$/.exec(v);
  if (!m) return null;
  return {
    major: +m[1],
    minor: +m[2],
    patch: +m[3],
    prerelease: m[4] ? m[4].split(".") : [],
    build: m[5] || "",
  };
}

function cmpNum(a, b) {
  return a < b ? -1 : a > b ? 1 : 0;
}

function cmpPrerelease(a, b) {
  if (a.length === 0 && b.length === 0) return 0;
  if (a.length === 0) return 1; // release > prerelease
  if (b.length === 0) return -1;
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    if (i >= a.length) return -1;
    if (i >= b.length) return 1;
    const x = a[i], y = b[i];
    const xn = /^\d+$/.test(x), yn = /^\d+$/.test(y);
    if (xn && yn) {
      const c = cmpNum(+x, +y);
      if (c) return c;
    } else if (xn) return -1;
    else if (yn) return 1;
    else {
      const c = x < y ? -1 : x > y ? 1 : 0;
      if (c) return c;
    }
  }
  return 0;
}

export function compareVersions(a, b) {
  if (typeof a === "string") a = parseVersion(a);
  if (typeof b === "string") b = parseVersion(b);
  if (!a || !b) fail("invalid version in comparison");
  return (
    cmpNum(a.major, b.major) ||
    cmpNum(a.minor, b.minor) ||
    cmpNum(a.patch, b.patch) ||
    cmpPrerelease(a.prerelease, b.prerelease)
  );
}

function parsePartial(v) {
  const m = /^\s*[v=\s]*(\d+|[xX*])(?:\.(\d+|[xX*]))?(?:\.(\d+|[xX*]))?(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?\s*$/.exec(v);
  if (!m) return null;
  const num = (x) => (x === undefined || /^[xX*]$/.test(x) ? null : +x);
  return {
    major: num(m[1]),
    minor: num(m[2]),
    patch: num(m[3]),
    prerelease: m[4] ? m[4].split(".") : [],
    build: m[5] || "",
  };
}

function fullVersion(p) {
  return {
    major: p.major === null ? 0 : p.major,
    minor: p.minor === null ? 0 : p.minor,
    patch: p.patch === null ? 0 : p.patch,
    prerelease: p.prerelease || [],
    build: p.build || "",
  };
}

function bumpMajor(v) {
  return { major: v.major + 1, minor: 0, patch: 0, prerelease: [], build: "" };
}
function bumpMinor(v) {
  return { major: v.major, minor: v.minor + 1, patch: 0, prerelease: [], build: "" };
}
function bumpPatch(v) {
  return { major: v.major, minor: v.minor, patch: v.patch + 1, prerelease: [], build: "" };
}

function comparatorsFor(token) {
  token = token.trim();
  if (token === "" || token === "*" || /^[xX]$/.test(token)) return [];
  let m = /^(\^|~|>=|<=|>|<|=)?\s*(.+)$/.exec(token);
  let op = m[1] || "";
  const p = parsePartial(m[2]);
  if (!p) fail("invalid range token: " + token);

  const lower = fullVersion(p);
  const anyUpper = p.major === null;
  if (anyUpper) return [];

  if (op === "^") {
    let upper;
    if (p.major > 0 || p.minor === null) upper = bumpMajor(lower);
    else if (p.minor > 0 || p.patch === null) upper = bumpMinor(lower);
    else upper = bumpPatch(lower);
    return [{ op: ">=", v: lower }, { op: "<", v: upper }];
  }
  if (op === "~") {
    const upper = p.minor === null ? bumpMajor(lower) : bumpMinor(lower);
    return [{ op: ">=", v: lower }, { op: "<", v: upper }];
  }
  if (op === "" || op === "=") {
    if (p.minor === null) return [{ op: ">=", v: lower }, { op: "<", v: bumpMajor(lower) }];
    if (p.patch === null) return [{ op: ">=", v: lower }, { op: "<", v: bumpMinor(lower) }];
    return [{ op: "=", v: lower }];
  }
  return [{ op, v: lower }];
}

export function rangeToComparatorSets(range) {
  if (range === undefined || range === null) range = "*";
  range = String(range).trim();
  if (range === "" || range === "*" || range === "latest") return [[]];
  const sets = [];
  for (const orPart of range.split("||")) {
    let part = orPart.trim();
    if (part === "") continue;
    // hyphen range: "1.2.3 - 2.3.4"
    const hy = /^(\S+)\s+-\s+(\S+)$/.exec(part);
    let comparators = [];
    if (hy) {
      const lo = parsePartial(hy[1]);
      const hiRaw = parsePartial(hy[2]);
      if (!lo || !hiRaw) fail("invalid hyphen range: " + part);
      const loV = fullVersion(lo);
      comparators.push({ op: ">=", v: loV });
      if (hiRaw.minor === null) comparators.push({ op: "<", v: bumpMajor(fullVersion(hiRaw)) });
      else if (hiRaw.patch === null) comparators.push({ op: "<", v: bumpMinor(fullVersion(hiRaw)) });
      else comparators.push({ op: "<=", v: fullVersion(hiRaw) });
    } else {
      // allow comma as an AND separator in addition to whitespace
      part = part.replace(/,/g, " ");
      for (const tok of part.split(/\s+/)) {
        if (!tok) continue;
        comparators = comparators.concat(comparatorsFor(tok));
      }
    }
    sets.push(comparators);
  }
  return sets.length ? sets : [[]];
}

function testComparator(v, c) {
  const r = compareVersions(v, c.v);
  switch (c.op) {
    case ">": return r > 0;
    case ">=": return r >= 0;
    case "<": return r < 0;
    case "<=": return r <= 0;
    case "=": return r === 0;
    default: fail("unknown comparator " + c.op);
  }
}

export function satisfies(version, range) {
  const v = typeof version === "string" ? parseVersion(version) : version;
  if (!v) return false;
  for (const set of rangeToComparatorSets(range)) {
    if (set.every((c) => testComparator(v, c))) return true;
  }
  return false;
}

export function maxSatisfying(versions, range) {
  let best = null;
  for (const ver of versions) {
    if (!parseVersion(ver)) continue;
    if (!satisfies(ver, range)) continue;
    if (best === null || compareVersions(ver, best) > 0) best = ver;
  }
  return best;
}

/* ------------------------------------------------------------------ */
/* conditional exports                                                 */
/* ------------------------------------------------------------------ */

export function defaultConditions() {
  return ["mbjs", "import", "default"];
}

function resolveTarget(target, conditions) {
  if (target === null || target === undefined) return null;
  if (typeof target === "string") return target;
  if (Array.isArray(target)) {
    for (const t of target) {
      const r = resolveTarget(t, conditions);
      if (r !== null) return r;
    }
    return null;
  }
  if (typeof target === "object") {
    for (const key of Object.keys(target)) {
      if (key === "default" || conditions.includes(key)) {
        const r = resolveTarget(target[key], conditions);
        if (r !== null) return r;
      }
    }
    return null;
  }
  return null;
}

export function resolveExports(pkg, subpath, conditions) {
  if (subpath === undefined) subpath = ".";
  if (conditions === undefined) conditions = defaultConditions();
  if (!subpath.startsWith(".")) fail("invalid subpath: " + subpath);

  const ex = pkg.exports;
  if (ex === undefined || ex === null) {
    if (subpath === ".") return pkg.main || "./index.js";
    return null;
  }
  if (typeof ex === "string" || Array.isArray(ex)) {
    if (subpath !== ".") return null;
    return resolveTarget(ex, conditions);
  }
  if (typeof ex === "object") {
    const keys = Object.keys(ex);
    const hasSubpaths = keys.some((k) => k.startsWith("."));
    if (!hasSubpaths) {
      if (subpath !== ".") return null;
      return resolveTarget(ex, conditions);
    }
    if (Object.prototype.hasOwnProperty.call(ex, subpath)) {
      return resolveTarget(ex[subpath], conditions);
    }
    const patterns = keys.filter((k) => k.includes("*")).sort((a, b) => b.length - a.length);
    for (const key of patterns) {
      const star = key.indexOf("*");
      const pre = key.slice(0, star);
      const post = key.slice(star + 1);
      if (subpath.length >= pre.length + post.length && subpath.startsWith(pre) && subpath.endsWith(post)) {
        const matched = subpath.slice(pre.length, subpath.length - post.length);
        const r = resolveTarget(ex[key], conditions);
        if (r !== null) return r.replace(/\*/g, matched);
      }
    }
  }
  return null;
}

/* ------------------------------------------------------------------ */
/* filesystem                                                          */
/* ------------------------------------------------------------------ */

export function exists(p) {
  const r = os.stat(p);
  return !!(Array.isArray(r) ? r[0] : r);
}

function stat(p) {
  const r = os.stat(p);
  return Array.isArray(r) ? r[0] : r;
}

export function isDir(p) {
  const st = stat(p);
  return !!st && (st.mode & os.S_IFMT) === os.S_IFDIR;
}

export function isFile(p) {
  const st = stat(p);
  return !!st && (st.mode & os.S_IFMT) === os.S_IFREG;
}

export function readText(p) {
  const s = std.loadFile(p);
  return s === null ? null : s;
}

export function writeText(p, s) {
  mkdirp(dirname(p));
  std.writeFile(p, s);
}

export function readBytes(p) {
  return std.loadFile(p, { binary: true }) || null;
}

export function writeBytes(p, b) {
  mkdirp(dirname(p));
  std.writeFile(p, b);
}

export function mkdirp(p) {
  p = normalize(p);
  if (p === "." || p === "" || isDir(p)) return;
  const segs = toSlash(p).split("/");
  let cur = "";
  for (let i = 0; i < segs.length; i++) {
    cur = i === 0 ? segs[i] : cur + "/" + segs[i];
    if (cur === "" || /^[A-Za-z]:$/.test(cur)) continue;
    if (isDir(cur)) continue;
    os.mkdir(cur, 511);
  }
}

export function readdir(p) {
  const r = os.readdir(p);
  const names = Array.isArray(r) && Array.isArray(r[0]) ? r[0] : r;
  return (names || []).filter((n) => n !== "." && n !== "..");
}

export function rmrf(p) {
  if (!exists(p)) return;
  if (isDir(p)) {
    for (const n of readdir(p)) rmrf(join(p, n));
    os.remove(p);
  } else {
    os.remove(p);
  }
}

export function copyTree(src, dst, filter) {
  if (filter && !filter(src, dst)) return;
  if (isDir(src)) {
    mkdirp(dst);
    for (const n of readdir(src).sort()) copyTree(join(src, n), join(dst, n), filter);
  } else if (isFile(src)) {
    writeBytes(dst, readBytes(src));
  }
}

export function shell(cmd) {
  const f = std.popen(cmd, "r");
  if (!f) return { ok: false, code: -1, out: "" };
  let out = "";
  try {
    out = f.readAsString() || "";
  } catch (e) {
    out = "";
  }
  let code = 0;
  try {
    const c = f.close();
    if (typeof c === "number") code = c;
  } catch (e) {
    code = -1;
  }
  return { ok: code === 0, code, out };
}

/**
 * Run a shell command with the working directory changed to `dir`.
 * Used for tar because GNU tar treats an absolute `C:/...` argument as a
 * remote host ("C:"); using relative paths avoids that entirely and keeps
 * the command portable between GNU tar and bsdtar.
 */
export function cdCmd(dir, rest) {
  return isWindows()
    ? 'cd /d "' + winPath(dir) + '" && ' + rest
    : 'cd "' + dir + '" && ' + rest;
}

/* ------------------------------------------------------------------ */
/* content-addressed store                                             */
/* ------------------------------------------------------------------ */

function treeRecords(dir, prefix, records) {
  for (const name of readdir(dir).sort()) {
    if (name === ".tgpm-store.json") continue;
    const full = join(dir, name);
    const rel = prefix ? prefix + "/" + name : name;
    if (isDir(full)) {
      treeRecords(full, rel, records);
    } else if (isFile(full)) {
      const bytes = readBytes(full);
      records.push("F " + rel + "\0" + sha256Hex(bytes || new Uint8Array(0)) + "\n");
    }
  }
  return records;
}

export function treeHash(dir) {
  const records = treeRecords(dir, "", []).sort();
  return sha256Hex(utf8Bytes(records.join("")));
}

export function defaultHome() {
  const env = std.getenv("TAKEGAWA_HOME");
  if (env) return resolvePath(env);
  const user = std.getenv("USERPROFILE") || std.getenv("HOME") || ".";
  return join(user, ".takegawa");
}

export function storePaths(home) {
  home = home || defaultHome();
  return {
    home,
    store: join(home, "store"),
    cache: join(home, "cache"),
    globalModules: join(home, "global", MODULES_DIR),
    bin: join(home, "bin"),
    registry: join(home, "registry"),
    index: join(home, "index.json"),
  };
}

export function storePackage(dir, home) {
  const paths = storePaths(home);
  const integrity = treeHash(dir);
  const storeDir = join(paths.store, integrity);
  if (!isDir(storeDir)) {
    mkdirp(paths.store);
    copyTree(dir, storeDir);
    writeText(join(storeDir, ".tgpm-store.json"), stableStringify({ format: STORE_FORMAT, integrity }) + "\n");
  }
  return { integrity, storeDir };
}

function linkDir(target, link) {
  if (exists(link)) rmrf(link);
  mkdirp(dirname(link));
  if (typeof os.symlink === "function") {
    try {
      const r = os.symlink(target, link);
      if ((typeof r === "number" ? r : 0) === 0 && exists(link)) return "symlink";
    } catch (e) {
      /* fall through */
    }
  }
  if (isWindows()) {
    const res = shell('cmd /c mklink /J "' + winPath(link) + '" "' + winPath(target) + '"');
    if (exists(link)) return "junction";
    void res;
  }
  copyTree(target, link, (src) => basename(src) !== ".tgpm-store.json");
  return "copy";
}

function winPath(p) {
  return normalize(p).replace(/\//g, "\\");
}

/* ------------------------------------------------------------------ */
/* manifests & lockfile                                                */
/* ------------------------------------------------------------------ */

export function parseManifest(text, file) {
  let obj;
  try {
    obj = JSON.parse(text);
  } catch (e) {
    fail("invalid JSON in " + (file || "manifest") + ": " + e.message);
  }
  if (!obj || typeof obj !== "object" || Array.isArray(obj)) fail("manifest must be a JSON object: " + (file || ""));
  return obj;
}

export function findManifest(dir) {
  for (const name of MANIFEST_NAMES) {
    const p = join(dir, name);
    if (isFile(p)) return p;
  }
  return null;
}

export function readManifest(dir) {
  const file = findManifest(dir);
  if (!file) fail("no takegawa.json/package.json in " + dir);
  const m = parseManifest(readText(file), file);
  m.__file = file;
  m.__dir = dir;
  return m;
}

export function validateManifest(m) {
  const errs = [];
  if (!m.name || typeof m.name !== "string") errs.push("name is required");
  if (!m.version || !parseVersion(m.version)) errs.push("version must be a valid semver string");
  if (m.exports !== undefined && typeof m.exports !== "string" && typeof m.exports !== "object") {
    errs.push("exports must be a string, object or array");
  }
  if (m.dependencies !== undefined && (typeof m.dependencies !== "object" || Array.isArray(m.dependencies))) {
    errs.push("dependencies must be an object");
  }
  return errs;
}

export function dependenciesOf(m, opts) {
  opts = opts || {};
  const deps = {};
  const add = (src) => {
    if (!src) return;
    for (const k of Object.keys(src)) deps[k] = src[k];
  };
  add(m.dependencies);
  add(m.optionalDependencies);
  if (!opts.production) add(m.devDependencies);
  return deps;
}

/* Runtime deps of a package pulled in transitively: never its devDeps. */
export function runtimeDependencies(m) {
  const deps = {};
  if (m.dependencies) for (const k of Object.keys(m.dependencies)) deps[k] = m.dependencies[k];
  if (m.optionalDependencies) for (const k of Object.keys(m.optionalDependencies)) deps[k] = m.optionalDependencies[k];
  return deps;
}

export function stableStringify(obj, indent) {
  if (indent === undefined) indent = 2;
  const pad = (n) => " ".repeat(n);
  function enc(v, depth) {
    if (v === null || v === undefined) return "null";
    if (typeof v === "number" || typeof v === "boolean") return String(v);
    if (typeof v === "string") return JSON.stringify(v);
    if (Array.isArray(v)) {
      if (v.length === 0) return "[]";
      const inner = v.map((x) => pad(depth + indent) + enc(x, depth + indent));
      return "[\n" + inner.join(",\n") + "\n" + pad(depth) + "]";
    }
    if (typeof v === "object") {
      const keys = Object.keys(v).sort();
      if (keys.length === 0) return "{}";
      const inner = keys.map((k) => pad(depth + indent) + JSON.stringify(k) + ": " + enc(v[k], depth + indent));
      return "{\n" + inner.join(",\n") + "\n" + pad(depth) + "}";
    }
    return "null";
  }
  return enc(obj, 0);
}

export function readLockfile(dir) {
  const p = join(dir, LOCKFILE);
  if (!isFile(p)) return null;
  return parseManifest(readText(p), p);
}

export function writeLockfile(dir, lock) {
  writeText(join(dir, LOCKFILE), stableStringify(lock) + "\n");
}

/* ------------------------------------------------------------------ */
/* registry / resolution                                               */
/* ------------------------------------------------------------------ */

function registryVersions(registry, name) {
  const d = join(registry, name);
  if (!isDir(d)) return [];
  return readdir(d)
    .filter((n) => parseVersion(n) && isDir(join(d, n)))
    .sort(compareVersions);
}

export function sourceSpec(spec) {
  if (typeof spec !== "string") return null;
  if (spec.startsWith("file:")) {
    const p = spec.slice(5);
    if (/\.tgz$|\.tar\.gz$/i.test(p)) return { type: "remote", url: p };
    return { type: "dir", path: p };
  }
  if (/^https?:\/\//i.test(spec)) return { type: "remote", url: spec };
  if (/\.tgz$|\.tar\.gz$/i.test(spec)) return { type: "remote", url: spec };
  if (spec.startsWith(".") || spec.startsWith("/") || /^[A-Za-z]:[\\/]/.test(spec)) {
    return { type: "dir", path: spec };
  }
  return null;
}

export function readPackageAt(dir) {
  const m = readManifest(dir);
  return { dir, manifest: m, name: m.name, version: m.version };
}

function findPackageRoot(dir) {
  if (findManifest(dir)) return dir;
  const entries = readdir(dir).filter((n) => n !== "." && n !== "..");
  for (const name of entries) {
    const sub = join(dir, name);
    if (isDir(sub) && findManifest(sub)) return sub;
  }
  fail("no takegawa.json/package.json found in archive");
  return dir;
}

/**
 * Fetch a remote package: download (http/https) or read a local tarball,
 * cache it by content hash, extract it, and return the package root.
 * Archives are cached at $TAKEGAWA_HOME/cache/<sha256>.tgz.
 */
export function fetchPackage(spec, opts) {
  opts = opts || {};
  const home = opts.home || defaultHome();
  const paths = storePaths(home);
  const src = sourceSpec(spec);
  if (!src || src.type !== "remote") fail("not a remote package: " + spec);
  mkdirp(paths.cache);

  let archive;
  if (/^https?:\/\//i.test(src.url)) {
    const tmpName = "download-" + Math.floor(Math.random() * 1e9).toString(36) + ".tgz";
    const tmp = join(paths.cache, tmpName);
    const r = shell(cdCmd(paths.cache, 'curl -fsSL -o "' + tmpName + '" "' + src.url + '"'));
    if (!r.ok || !isFile(tmp)) fail("download failed: " + src.url);
    archive = tmp;
  } else {
    archive = resolvePath(src.url, opts.baseDir || cwd());
    if (!isFile(archive)) fail("archive not found: " + src.url);
  }

  const bytes = readBytes(archive);
  if (!bytes) fail("cannot read archive: " + src.url);
  const sha = sha256Hex(bytes);
  const cached = join(paths.cache, sha + ".tgz");
  if (archive !== cached && !isFile(cached)) writeBytes(cached, bytes);
  if (archive !== cached && /download-/.test(basename(archive))) rmrf(archive);

  const dir = join(paths.cache, sha);
  if (!isDir(dir)) {
    mkdirp(dir);
    const base = basename(cached);
    const r = shell(cdCmd(paths.cache, 'tar -xzf "' + base + '" -C "' + sha + '"'));
    if (!r.ok) {
      rmrf(dir);
      fail("failed to extract archive: " + src.url);
    }
  }
  return { ...readPackageAt(findPackageRoot(dir)), spec, source: "remote", archive: cached };
}

/**
 * Resolve a single dependency to a concrete package directory.
 * Search order for version ranges:
 *   1. project's existing node_modules
 *   2. configured registry (offline/local)
 *   3. global store index
 */
export function resolvePackage(name, spec, opts) {
  opts = opts || {};
  const base = opts.baseDir || cwd();
  const src = sourceSpec(spec);
  if (src && src.type === "dir") {
    const dir = resolvePath(src.path, base);
    if (!isDir(dir)) fail("local package not found: " + src.path);
    const pkg = readPackageAt(dir);
    return { ...pkg, spec, source: "local" };
  }
  if (src && src.type === "remote") {
    return fetchPackage(spec, { ...opts, baseDir: base });
  }

  const range = spec === undefined || spec === null || spec === "" ? "*" : String(spec);

  // 1. node_modules
  const nm = join(base, NODE_MODULES_DIR, name);
  if (isDir(nm)) {
    const pkg = readPackageAt(nm);
    if (satisfies(pkg.version, range)) return { ...pkg, spec, source: "node_modules" };
  }

  // 2. registry
  const reg = opts.registry || join(defaultHome(), "registry");
  const versions = registryVersions(reg, name);
  const best = maxSatisfying(versions, range);
  if (best) {
    const dir = join(reg, name, best);
    const pkg = readPackageAt(dir);
    return { ...pkg, spec, source: "registry" };
  }

  // 3. global store index
  const idx = readIndex(opts.home);
  if (idx && idx[name]) {
    const candidates = Object.keys(idx[name]).filter((v) => satisfies(v, range));
    const bestV = maxSatisfying(candidates, range);
    if (bestV) {
      const rec = idx[name][bestV];
      const dir = join(storePaths(opts.home).store, rec.integrity);
      if (isDir(dir)) {
        const pkg = readPackageAt(dir);
        return { ...pkg, spec, source: "store" };
      }
    }
  }

  fail("cannot resolve " + name + "@" + range);
  return null;
}

export function readIndex(home) {
  const p = storePaths(home).index;
  if (!isFile(p)) return {};
  try {
    return JSON.parse(readText(p));
  } catch (e) {
    return {};
  }
}

export function updateIndex(home, name, version, integrity) {
  const paths = storePaths(home);
  const idx = readIndex(home);
  if (!idx[name]) idx[name] = {};
  idx[name][version] = { integrity };
  writeText(paths.index, stableStringify(idx) + "\n");
}

/**
 * Build the full dependency graph.  Returns { packages: Map, order: [names] }.
 * Emits conflicts when one name is required at incompatible ranges.
 */
export function resolveTree(rootManifest, opts) {
  opts = opts || {};
  const baseDir = opts.baseDir || rootManifest.__dir || cwd();
  const packages = {};
  const requirements = {};
  const queue = [];

  const addReq = (name, spec, parent) => {
    (requirements[name] = requirements[name] || []).push({ spec, parent });
    queue.push({ name, spec, baseDir: parent ? parent.dir : baseDir });
  };

  const rootDeps = dependenciesOf(rootManifest, opts);
  for (const name of Object.keys(rootDeps)) addReq(name, rootDeps[name], null);

  while (queue.length) {
    const { name, spec, baseDir: bd } = queue.shift();
    // Reproducible installs: if the lockfile pins a version that still
    // satisfies the requested range, use it instead of the newest match.
    const locked = opts.lock ? opts.lock[name] : null;
    const isSource = !!sourceSpec(spec);
    const wantLocked = !isSource && locked && satisfies(locked.version, spec === undefined ? "*" : spec);
    const resolved = resolvePackage(name, wantLocked ? locked.version : spec, { ...opts, baseDir: bd });
    const existing = packages[name];
    if (!existing) {
      packages[name] = { ...resolved, requirements: [] };
    } else if (compareVersions(existing.version, resolved.version) !== 0) {
      // Keep the highest version that satisfies every requirement seen so far.
      const highest = compareVersions(existing.version, resolved.version) > 0 ? existing : resolved;
      packages[name] = { ...highest, requirements: existing.requirements, dir: highest.dir, manifest: highest.manifest };
    }
    packages[name].requirements.push(spec);
    if (!existing) {
      const deps = runtimeDependencies(resolved.manifest);
      for (const dep of Object.keys(deps)) {
        addReq(dep, deps[dep], resolved);
      }
    }
  }

  // validate every requirement against the selected version
  for (const name of Object.keys(packages)) {
    const pkg = packages[name];
    for (const req of requirements[name] || []) {
      if (sourceSpec(req.spec)) continue;
      if (!satisfies(pkg.version, req.spec === undefined ? "*" : req.spec)) {
        fail(
          "version conflict for " + name + ": selected " + pkg.version +
          " does not satisfy " + req.spec + (req.parent ? " required by " + req.parent.name : "")
        );
      }
    }
  }

  return { packages, requirements };
}

/* ------------------------------------------------------------------ */
/* install                                                             */
/* ------------------------------------------------------------------ */

function materialize(pkg, home, dest, mode) {
  const stored = storePackage(pkg.dir, home);
  updateIndex(home, pkg.name, pkg.version, stored.integrity);
  mkdirp(dirname(dest));
  let how;
  if (mode === "copy") {
    if (exists(dest)) rmrf(dest);
    copyTree(stored.storeDir, dest, (src) => basename(src) !== ".tgpm-store.json");
    how = "copy";
  } else {
    how = linkDir(stored.storeDir, dest);
    if (how === "copy") {
      // linkDir already copied from the store.
    }
  }
  return { ...stored, how };
}

export function normalizeBin(bin, defaultName) {
  const out = [];
  if (!bin) return out;
  if (typeof bin === "string") {
    out.push([basename(defaultName), bin]);
  } else if (typeof bin === "object") {
    for (const key of Object.keys(bin)) out.push([key, bin[key]]);
  }
  return out;
}

/** Locate a MochiBerry runtime to embed in bin shims. */
export function findRuntime() {
  if (typeof os.exePath === "function") {
    const exe = os.exePath();
    if (/^mbjs(\.exe)?$/i.test(basename(exe))) return exe;
    const dir = dirname(exe);
    const names = isWindows() ? ["mbjs.exe", "mbjs.cmd", "mbjs.bat"] : ["mbjs"];
    for (const n of names) {
      const p = join(dir, n);
      if (isFile(p)) return p;
    }
  }
  const env = std.getenv("MBJS") || std.getenv("MBJS_BIN");
  if (env) return env;
  return "mbjs";
}

/**
 * Create executable shims for packages that declare a `bin` field.
 * Entries: [{ name, manifest, dest }]. Shims run the target with the
 * MochiBerry runtime (absolute when it can be located).
 */
export function writeBinShims(entries, binDir) {
  const created = [];
  const work = [];
  for (const e of entries) {
    for (const pair of normalizeBin(e.manifest && e.manifest.bin, e.name)) work.push({ e, cmd: pair[0], rel: pair[1] });
  }
  if (!work.length) return created;
  mkdirp(binDir);
  const runtime = findRuntime();
  for (const w of work) {
    const script = resolvePath(w.rel, w.e.dest);
    const target = join(binDir, isWindows() ? w.cmd + ".cmd" : w.cmd);
    const body = isWindows()
      ? "@echo off\r\n\"" + winPath(runtime) + "\" \"" + winPath(script) + "\" %*\r\n"
      : "#!/bin/sh\nexec \"" + runtime + "\" \"" + script + "\" \"$@\"\n";
    writeText(target, body);
    if (!isWindows()) shell("chmod +x \"" + target + "\"");
    created.push(w.cmd);
  }
  return created;
}

export function install(rootDir, opts) {
  opts = opts || {};
  const home = opts.home || defaultHome();
  const mode = opts.mode || "linked";
  const rootManifest = opts.manifest || readManifest(rootDir);
  const existingLock = opts.update ? null : readLockfile(rootDir);
  const lockMap = existingLock && existingLock.packages ? existingLock.packages : null;

  const tree = resolveTree(rootManifest, { ...opts, baseDir: rootDir, home, lock: lockMap });
  const modulesDir = join(rootDir, mode === "copy" ? NODE_MODULES_DIR : MODULES_DIR);

  const lock = {
    lockfileVersion: 1,
    format: STORE_FORMAT,
    name: rootManifest.name || basename(rootDir),
    version: rootManifest.version || "0.0.0",
    mode,
    packages: {},
  };

  for (const name of Object.keys(tree.packages).sort()) {
    const pkg = tree.packages[name];
    lock.packages[name] = {
      version: pkg.version,
      source: pkg.source,
      link: mode,
    };
  }

  if (opts.frozen) {
    if (!existingLock) fail("--frozen-lockfile requires an existing " + LOCKFILE);
    const cur = existingLock.packages || {};
    const newKeys = Object.keys(lock.packages).sort();
    let same = Object.keys(cur).length === newKeys.length;
    if (same) {
      for (const k of newKeys) {
        if (!cur[k] || cur[k].version !== lock.packages[k].version) {
          same = false;
          break;
        }
      }
    }
    if (!same) fail("--frozen-lockfile: " + LOCKFILE + " is out of date; run `takegawa install`");
  }

  const installed = [];
  const binEntries = [];
  for (const name of Object.keys(tree.packages).sort()) {
    const pkg = tree.packages[name];
    const dest = join(modulesDir, name);
    const info = materialize(pkg, home, dest, mode);
    lock.packages[name].integrity = info.integrity;
    installed.push({ name, version: pkg.version, how: info.how, store: info.storeDir });
    binEntries.push({ name, manifest: pkg.manifest, dest });
  }
  const bins = writeBinShims(binEntries, join(modulesDir, ".bin"));

  if (!opts.frozen) writeLockfile(rootDir, lock);
  return { installed, lock, tree, modulesDir, bins };
}

export function installGlobal(pkgSpec, opts) {
  opts = opts || {};
  const home = opts.home || defaultHome();
  const modules = storePaths(home).globalModules;
  const tree = resolveTree(pkgSpec.manifest, { ...opts, baseDir: pkgSpec.dir, home, production: true });
  const all = {};
  for (const name of Object.keys(tree.packages)) all[name] = tree.packages[name];
  all[pkgSpec.name] = pkgSpec;
  const installed = [];
  const binEntries = [];
  for (const name of Object.keys(all).sort()) {
    const p = all[name];
    const dest = join(modules, name);
    const info = materialize(p, home, dest, opts.mode || "linked");
    installed.push({ name, version: p.version, how: info.how, store: info.storeDir, dest });
    binEntries.push({ name, manifest: p.manifest, dest });
  }
  const bins = writeBinShims(binEntries, storePaths(home).bin);
  return { installed, bins };
}

export function verify(rootDir, opts) {
  opts = opts || {};
  const home = opts.home || defaultHome();
  const lock = readLockfile(rootDir);
  if (!lock) return { ok: false, errors: ["no " + LOCKFILE] };
  const errors = [];
  for (const name of Object.keys(lock.packages || {})) {
    const rec = lock.packages[name];
    const storeDir = join(storePaths(home).store, rec.integrity);
    if (!isDir(storeDir)) {
      errors.push("missing store entry for " + name + "@" + rec.version);
      continue;
    }
    const actual = treeHash(storeDir);
    if (actual !== rec.integrity) errors.push("integrity mismatch for " + name + "@" + rec.version);
  }
  return { ok: errors.length === 0, errors };
}

/* ------------------------------------------------------------------ */
/* lifecycle: outdated / info / gc                                     */
/* ------------------------------------------------------------------ */

/** Versions available for `name` from every resolution source. */
export function availableVersions(name, opts) {
  opts = opts || {};
  const seen = {};
  const reg = opts.registry || join(defaultHome(), "registry");
  for (const v of registryVersions(reg, name)) seen[v] = true;
  const idx = readIndex(opts.home);
  if (idx && idx[name]) for (const v of Object.keys(idx[name])) seen[v] = true;
  const nm = join(opts.baseDir || cwd(), NODE_MODULES_DIR, name);
  if (isDir(nm)) {
    try {
      const pkg = readPackageAt(nm);
      if (parseVersion(pkg.version)) seen[pkg.version] = true;
    } catch (e) {
      /* ignore */
    }
  }
  return Object.keys(seen).sort(compareVersions);
}

/** Compare the locked tree against the newest versions the ranges allow. */
export function outdated(rootDir, opts) {
  opts = opts || {};
  const home = opts.home || defaultHome();
  const manifest = readManifest(rootDir);
  const lock = readLockfile(rootDir);
  const locked = lock && lock.packages ? lock.packages : {};
  const deps = dependenciesOf(manifest, opts);
  const rows = [];
  for (const name of Object.keys(deps).sort()) {
    const range = deps[name];
    const versions = availableVersions(name, { ...opts, home, baseDir: rootDir });
    const wanted = maxSatisfying(versions, range);
    const current = locked[name] ? locked[name].version : null;
    if (wanted && (!current || compareVersions(wanted, current) > 0)) {
      rows.push({ name, current: current || "-", wanted, latest: versions[versions.length - 1] || wanted, range });
    }
  }
  return rows;
}

/** Describe a package spec: resolution source, version, exports and deps. */
export function info(spec, opts) {
  opts = opts || {};
  const { name, range } = (function (s) {
    const at = s.lastIndexOf("@");
    if (at > 0) return { name: s.slice(0, at), range: s.slice(at + 1) || "*" };
    return { name: s, range: "*" };
  })(spec);
  const pkg = resolvePackage(name, range, opts);
  const conditions = opts.conditions || defaultConditions();
  const exportsMain = resolveExports(pkg.manifest, ".", conditions);
  return {
    name: pkg.name,
    version: pkg.version,
    source: pkg.source,
    dir: pkg.dir,
    main: exportsMain,
    conditions,
    dependencies: runtimeDependencies(pkg.manifest),
    exports: pkg.manifest.exports,
  };
}

/** Collect every store integrity referenced by lockfiles under `root`. */
export function collectReferences(root, home) {
  const refs = {};
  const skip = { [MODULES_DIR]: 1, [NODE_MODULES_DIR]: 1, ".git": 1, ".hg": 1, ".svn": 1, node_modules: 1 };
  const visit = (dir) => {
    const lockPath = join(dir, LOCKFILE);
    if (isFile(lockPath)) {
      try {
        const lock = parseManifest(readText(lockPath), lockPath);
        for (const name of Object.keys(lock.packages || {})) {
          const rec = lock.packages[name];
          if (rec && rec.integrity) refs[rec.integrity] = true;
        }
      } catch (e) {
        /* ignore malformed lockfiles during gc */
      }
    }
    for (const name of readdir(dir)) {
      if (skip[name]) continue;
      const full = join(dir, name);
      if (isDir(full)) visit(full);
    }
  };
  if (root) visit(root);

  // Globally installed packages are linked from the store.
  const gm = storePaths(home).globalModules;
  if (isDir(gm)) {
    for (const name of readdir(gm)) {
      const full = join(gm, name);
      if (isDir(full)) {
        try {
          refs[treeHash(full)] = true;
        } catch (e) {
          /* ignore */
        }
      }
    }
  }
  return refs;
}

/** Remove store entries nobody references. Returns { removed, kept }. */
export function garbageCollect(opts) {
  opts = opts || {};
  const home = opts.home || defaultHome();
  const paths = storePaths(home);
  if (!isDir(paths.store)) return { removed: [], kept: [] };
  const refs = collectReferences(opts.root || cwd(), home);
  const removed = [];
  const kept = [];
  for (const entry of readdir(paths.store)) {
    const full = join(paths.store, entry);
    if (!isDir(full)) continue;
    if (refs[entry]) kept.push(entry);
    else {
      removed.push(entry);
      if (!opts.dryRun) rmrf(full);
    }
  }
  // prune the index of entries that no longer exist
  if (!opts.dryRun) {
    const idx = readIndex(home);
    let changed = false;
    for (const name of Object.keys(idx)) {
      for (const ver of Object.keys(idx[name])) {
        const integ = idx[name][ver].integrity;
        if (!isDir(join(paths.store, integ))) {
          delete idx[name][ver];
          changed = true;
        }
      }
      if (Object.keys(idx[name]).length === 0) {
        delete idx[name];
        changed = true;
      }
    }
    if (changed) writeText(paths.index, stableStringify(idx) + "\n");
  }
  return { removed, kept };
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */

function usage() {
  println("Takegawa TGPM " + VERSION + " — the MochiBerry package manager");
  println("");
  println("usage: takegawa <command> [options]");
  println("");
  println("commands:");
  println("  install [pkg]      install dependencies (or add pkg)");
  println("  add <pkg>[@range]  add a dependency and install");
  println("  fetch <url|tgz>    download/extract a package into the cache");
  println("  init               create a takegawa.json in the current directory");
  println("  exec <cmd...>      run a command with package bin directories on PATH");
  println("  remove <pkg>       remove a dependency and its link");
  println("  update             re-resolve against the newest allowed versions");
  println("  resolve            print the resolved dependency tree");
  println("  outdated           list dependencies with newer allowed versions");
  println("  info <pkg>         show resolved package metadata");
  println("  verify             verify the lockfile against the global store");
  println("  gc                 remove unreferenced entries from the global store");
  println("  link               link the current package into the store");
  println("  pack               pack the current package (tgpm tarball)");
  println("  run <script>       run a script from the manifest");
  println("  store              print global store locations");
  println("  doctor             environment / manifest checks");
  println("  version            print the version");
  println("  help               show this help");
  println("");
  println("options:");
  println("  -g, --global       operate on the global store");
  println("  --node-modules     copy into node_modules instead of linking");
  println("  --copy             alias for --node-modules");
  println("  --production       skip devDependencies");
  println("  --frozen-lockfile  fail instead of updating the lockfile");
  println("  --update           ignore the lockfile and re-resolve");
  println("  --registry <dir>   use <dir> as the package registry");
  println("  --home <dir>       override TAKEGAWA_HOME");
  println("  --conditions <c>   comma-separated export conditions");
}

function parseArgs(argv) {
  const out = { _: [], flags: {}, options: {} };
  const takesValue = new Set(["registry", "home", "conditions", "mode"]);
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === "--") {
      out._.push(...argv.slice(i + 1));
      break;
    }
    if (a.startsWith("--")) {
      let key = a.slice(2);
      let val;
      const eq = key.indexOf("=");
      if (eq >= 0) {
        val = key.slice(eq + 1);
        key = key.slice(0, eq);
      }
      if (takesValue.has(key)) {
        if (val === undefined) val = argv[++i];
        out.options[key] = val;
      } else {
        out.flags[key] = val === undefined ? true : val;
      }
    } else if (a === "-g") {
      out.flags.global = true;
    } else {
      out._.push(a);
    }
  }
  return out;
}

function resolveOpts(args) {
  const home = args.options.home ? resolvePath(args.options.home) : defaultHome();
  const mode = args.flags["node-modules"] || args.flags.copy || args.options.mode === "copy" ? "copy" : "linked";
  return {
    home,
    mode,
    production: !!args.flags.production,
    frozen: !!args.flags["frozen-lockfile"] || !!args.flags.frozen,
    update: !!args.flags.update,
    registry: args.options.registry ? resolvePath(args.options.registry) : join(home, "registry"),
  };
}

function parseSpec(spec) {
  let name = spec;
  let range = "*";
  const at = spec.lastIndexOf("@");
  if (at > 0) {
    name = spec.slice(0, at);
    range = spec.slice(at + 1) || "*";
  }
  return { name, range };
}

function cmdInstall(args) {
  const opts = resolveOpts(args);
  const target = args._[0];
  const dir = cwd();
  if (args.flags.global) {
    if (!target) fail("install -g requires <pkg>");
    const { name, range } = parseSpec(target);
    const pkg = resolvePackage(name, range, { ...opts, baseDir: dir });
    const res = installGlobal(pkg, opts);
    const infos = res.installed;
    println("installed " + name + "@" + pkg.version + " (+" + (infos.length - 1) + " deps) into " + storePaths(opts.home).globalModules);
    if (res.bins.length) println("linked bin: " + res.bins.join(", ") + " -> " + storePaths(opts.home).bin);
    return 0;
  }
  if (target) return cmdAdd(args);

  const manifest = readManifest(dir);
  const errs = validateManifest(manifest);
  if (errs.length) fail("invalid manifest: " + errs.join("; "));
  const res = install(dir, { ...opts, manifest });
  for (const p of res.installed) {
    println("  + " + p.name + "@" + p.version + "  [" + p.how + "]");
  }
  println(res.installed.length + " package(s) installed into " + res.modulesDir);
  if (res.bins.length) println("linked bin: " + res.bins.join(", ") + " -> " + join(res.modulesDir, ".bin"));
  return 0;
}

function cmdAdd(args) {
  const opts = resolveOpts(args);
  const spec = args._[0];
  if (!spec) fail("add requires <pkg>[@range]");
  const dir = cwd();
  const manifest = readManifest(dir);

  const src = sourceSpec(spec);
  let name, range, pkg;
  if (src) {
    pkg = resolvePackage("", spec, { ...opts, baseDir: dir });
    name = pkg.name;
    if (src.type === "dir") {
      const from = toSlash(normalize(resolvePath(dir))).slice(0, 2).toLowerCase();
      const to = toSlash(normalize(pkg.dir)).slice(0, 2).toLowerCase();
      range = "file:" + (from === to ? relativePath(dir, pkg.dir) : pkg.dir);
    } else {
      range = src.url;
    }
  } else {
    const parsed = parseSpec(spec);
    name = parsed.name;
    range = parsed.range;
    pkg = resolvePackage(name, range, { ...opts, baseDir: dir });
  }
  if (opts.mode === "copy") mkdirp(join(dir, NODE_MODULES_DIR));

  manifest.dependencies = manifest.dependencies || {};
  manifest.dependencies[name] = range;
  delete manifest.__file;
  delete manifest.__dir;
  writeText(join(dir, MANIFEST_NAMES[0]), stableStringify(manifest) + "\n");

  const res = install(dir, { ...opts, manifest });
  void res;
  println("added " + name + "@" + pkg.version + (src ? " (" + range + ")" : ""));
  if (res.bins.length) println("linked bin: " + res.bins.join(", ") + " -> " + join(res.modulesDir, ".bin"));
  return 0;
}

function cmdRemove(args) {
  const opts = resolveOpts(args);
  const name = args._[0];
  if (!name) fail("remove requires <pkg>");
  const dir = cwd();
  const manifest = readManifest(dir);
  let removed = false;
  for (const key of ["dependencies", "devDependencies", "optionalDependencies"]) {
    if (manifest[key] && manifest[key][name] !== undefined) {
      delete manifest[key][name];
      removed = true;
    }
  }
  delete manifest.__file;
  delete manifest.__dir;
  writeText(join(dir, MANIFEST_NAMES[0]), stableStringify(manifest) + "\n");
  rmrf(join(dir, MODULES_DIR, name));
  rmrf(join(dir, NODE_MODULES_DIR, name));
  const lock = readLockfile(dir) || { lockfileVersion: 1, packages: {} };
  if (lock.packages) delete lock.packages[name];
  writeLockfile(dir, lock);
  println(removed ? "removed " + name : name + " was not a dependency");
  return 0;
}

function cmdResolve(args) {
  const opts = resolveOpts(args);
  const dir = cwd();
  const manifest = readManifest(dir);
  const tree = resolveTree(manifest, { ...opts, baseDir: dir });
  for (const name of Object.keys(tree.packages).sort()) {
    const p = tree.packages[name];
    println(name + "@" + p.version + "  (" + p.source + ")");
  }
  return 0;
}

function cmdUpdate(args) {
  const opts = resolveOpts(args);
  const dir = cwd();
  const manifest = readManifest(dir);
  const res = install(dir, { ...opts, manifest, update: true });
  for (const p of res.installed) println("  + " + p.name + "@" + p.version + "  [" + p.how + "]");
  println(res.installed.length + " package(s) updated in " + res.modulesDir);
  return 0;
}

function cmdOutdated(args) {
  const opts = resolveOpts(args);
  const rows = outdated(cwd(), opts);
  if (rows.length === 0) {
    println("all dependencies are up to date");
    return 0;
  }
  println(pad("package", 24) + pad("current", 12) + pad("wanted", 12) + "latest");
  for (const r of rows) {
    println(pad(r.name, 24) + pad(r.current, 12) + pad(r.wanted, 12) + r.latest);
  }
  return 0;
}

function cmdInfo(args) {
  const opts = resolveOpts(args);
  const spec = args._[0];
  if (!spec) fail("info requires <pkg>[@range]");
  const meta = info(spec, { ...opts, baseDir: cwd() });
  println(meta.name + "@" + meta.version);
  println("  source:     " + meta.source);
  println("  dir:        " + meta.dir);
  println("  main:       " + meta.main);
  println("  conditions: " + meta.conditions.join(", "));
  const deps = Object.keys(meta.dependencies);
  println("  deps:       " + (deps.length ? deps.join(", ") : "(none)"));
  return 0;
}

function cmdGc(args) {
  const opts = resolveOpts(args);
  const root = args._[0] ? resolvePath(args._[0]) : cwd();
  const dryRun = !!args.flags["dry-run"];
  const res = garbageCollect({ home: opts.home, root, dryRun });
  for (const e of res.removed) println((dryRun ? "would remove " : "removed ") + e);
  println(res.removed.length + " removed, " + res.kept.length + " kept");
  return 0;
}

function pad(s, n) {
  s = String(s);
  return s.length >= n ? s : s + " ".repeat(n - s.length);
}

function cmdVerify(args) {
  const opts = resolveOpts(args);
  const res = verify(cwd(), opts);
  if (res.ok) {
    println("ok: lockfile matches the global store");
    return 0;
  }
  for (const e of res.errors) warn(e);
  return 1;
}

function cmdLink(args) {
  const opts = resolveOpts(args);
  const dir = cwd();
  const manifest = readManifest(dir);
  const stored = storePackage(dir, opts.home);
  updateIndex(opts.home, manifest.name, manifest.version, stored.integrity);
  const dest = join(storePaths(opts.home).globalModules, manifest.name);
  const how = linkDir(stored.storeDir, dest);
  println("linked " + manifest.name + "@" + manifest.version + " -> " + stored.storeDir + " [" + how + "]");
  return 0;
}

function cmdFetch(args) {
  const opts = resolveOpts(args);
  const spec = args._[0];
  if (!spec) fail("fetch requires <url|file.tgz>");
  const pkg = fetchPackage(spec, { ...opts, baseDir: cwd() });
  println("fetched " + pkg.name + "@" + pkg.version);
  println("  dir:     " + pkg.dir);
  println("  archive: " + pkg.archive);
  return 0;
}

function cmdInit(args) {
  void args;
  const dir = cwd();
  const found = findManifest(dir);
  if (found) {
    println("manifest already exists: " + found);
    return 0;
  }
  let name = basename(dir).toLowerCase().replace(/[^a-z0-9._-]/g, "-");
  if (!name) name = "package";
  const manifest = {
    name,
    version: "0.1.0",
    type: "module",
    exports: { ".": { mbjs: "./index.js", default: "./index.js" } },
    dependencies: {},
    scripts: {},
  };
  writeText(join(dir, MANIFEST_NAMES[0]), stableStringify(manifest) + "\n");
  if (!isFile(join(dir, "index.js"))) writeText(join(dir, "index.js"), "export default {};\n");
  println("created " + MANIFEST_NAMES[0] + " for " + name);
  return 0;
}

function findCommand(cmd, dirs) {
  if (isAbsolutePath(cmd) || cmd.indexOf("/") >= 0 || cmd.indexOf("\\") >= 0) return cmd;
  const exts = isWindows() ? [".cmd", ".bat", ".exe", ""] : [""];
  for (const d of dirs) {
    for (const ext of exts) {
      const p = join(d, cmd + ext);
      if (isFile(p)) return p;
    }
  }
  return null;
}

function cmdExec(args) {
  if (!args._.length) fail("exec requires a command");
  const opts = resolveOpts(args);
  const dir = cwd();
  const paths = storePaths(opts.home);
  const localBin = join(dir, MODULES_DIR, ".bin");
  const found = findCommand(args._[0], [localBin, paths.bin]);
  const argv = args._.slice();
  if (found) argv[0] = found;
  let line;
  if (isWindows()) {
    line = argv.map((s) => (/[\s"]/.test(s) ? '"' + s.replace(/"/g, '""') + '"' : s)).join(" ");
  } else {
    line = argv.map((s) => '"' + s.replace(/"/g, '\\"') + '"').join(" ");
  }
  const res = shell(line);
  std.printf("%s", res.out);
  return res.ok ? 0 : 1;
}

function cmdPack(args) {
  const opts = resolveOpts(args);
  void opts;
  const dir = cwd();
  const manifest = readManifest(dir);
  const packName = (manifest.name || basename(dir)).replace(/^@/, "").replace(/\//g, "-");
  const outName = packName + "-" + (manifest.version || "0.0.0") + ".tgz";
  const parent = dirname(dir);
  const excludes = ["takegawa_modules", "node_modules", ".git", ".tgpm-store.json"]
    .map((n) => '--exclude="' + n + '"')
    .join(" ");
  const r = shell(cdCmd(parent, 'tar -czf "' + outName + '" ' + excludes + ' "' + basename(dir) + '"'));
  if (!isFile(join(parent, outName))) {
    warn("tar unavailable; writing a directory snapshot instead");
    const snap = join(parent, outName.replace(/\.tgz$/, ""));
    rmrf(snap);
    copyTree(dir, snap);
    println("packed " + snap);
    return 0;
  }
  void r;
  println("packed " + join(parent, outName));
  return 0;
}

function cmdRun(args) {
  const dir = cwd();
  const manifest = readManifest(dir);
  const script = args._[0];
  if (!script) fail("run requires a script name");
  const scripts = manifest.scripts || {};
  if (!scripts[script]) fail("no such script: " + script);
  const extra = args._.slice(1).join(" ");
  const cmd = scripts[script] + (extra ? " " + extra : "");
  const line = isWindows() ? 'cmd /c "' + cmd + '"' : "sh -c '" + cmd.replace(/'/g, "'\\''") + "'";
  const old = cwd();
  os.chdir(dir);
  const res = shell(line);
  os.chdir(old);
  std.printf("%s", res.out);
  return res.ok ? 0 : 1;
}

function cmdStore(args) {
  const opts = resolveOpts(args);
  const p = storePaths(opts.home);
  println("home:      " + p.home);
  println("store:     " + p.store);
  println("cache:     " + p.cache);
  println("global:    " + p.globalModules);
  println("registry:  " + p.registry);
  if (isDir(p.store)) println("entries:   " + readdir(p.store).length);
  return 0;
}

function cmdDoctor(args) {
  const opts = resolveOpts(args);
  let problems = 0;
  println("takegawa " + VERSION);
  println("platform  " + os.platform);
  println("home      " + opts.home);
  println("mode      " + opts.mode);
  const mf = findManifest(cwd());
  if (mf) {
    const m = parseManifest(readText(mf), mf);
    const errs = validateManifest(m);
    if (errs.length) {
      problems += errs.length;
      for (const e of errs) warn(e);
    } else {
      println("manifest  ok (" + m.name + "@" + m.version + ")");
    }
  } else {
    println("manifest  none (not inside a package)");
  }
  if (typeof os.symlink !== "function" && !isWindows()) println("symlink   missing");
  println(problems ? problems + " problem(s)" : "no problems found");
  return problems ? 1 : 0;
}

export function main(argv) {
  if (!argv) argv = typeof scriptArgs !== "undefined" ? scriptArgs.slice(1) : [];
  const args = parseArgs(argv);
  const cmd = args._.shift() || "help";
  try {
    switch (cmd) {
      case "install":
      case "i": return cmdInstall(args);
      case "add": return cmdAdd(args);
      case "fetch": return cmdFetch(args);
      case "init": return cmdInit(args);
      case "exec":
      case "x": return cmdExec(args);
      case "remove":
      case "rm":
      case "uninstall": return cmdRemove(args);
      case "update":
      case "upgrade": return cmdUpdate(args);
      case "resolve":
      case "ls": return cmdResolve(args);
      case "outdated": return cmdOutdated(args);
      case "info":
      case "view": return cmdInfo(args);
      case "verify": return cmdVerify(args);
      case "gc":
      case "prune": return cmdGc(args);
      case "link": return cmdLink(args);
      case "pack": return cmdPack(args);
      case "run": return cmdRun(args);
      case "store": return cmdStore(args);
      case "doctor": return cmdDoctor(args);
      case "version":
      case "--version":
      case "-v": println(VERSION); return 0;
      case "help":
      case "--help":
      case "-h": usage(); return 0;
      default:
        warn("unknown command: " + cmd);
        usage();
        return 1;
    }
  } catch (e) {
    warn(e && e.message ? e.message : String(e));
    return 1;
  }
}

if (import.meta.main || (typeof globalThis !== "undefined" && globalThis.__TGPM_MAIN__)) {
  const argv = globalThis.__TGPM_ARGV__ || (typeof scriptArgs !== "undefined" ? scriptArgs.slice(1) : []);
  const code = main(argv);
  if (typeof globalThis !== "undefined") globalThis.__TGPM_EXIT_CODE__ = code;
  if (typeof globalThis.__TGPM_SET_EXIT__ === "function") globalThis.__TGPM_SET_EXIT__(code);
}
