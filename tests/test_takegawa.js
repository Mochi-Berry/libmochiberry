/*
 * test_takegawa.js — regression tests for TGPM (takegawa.js).
 *
 * Run:
 *   build/mbjs -m tests/test_takegawa.js
 */

import * as std from "mbjs:std";
import * as os from "mbjs:os";
import * as tg from "../takegawa.js";

let failures = 0;
function check(cond, msg) {
  if (cond) {
    console.log("PASS: " + msg);
  } else {
    console.log("FAIL: " + msg);
    failures++;
  }
}
function eq(a, b, msg) {
  check(a === b, msg + " (got " + JSON.stringify(a) + ", want " + JSON.stringify(b) + ")");
}

function mkTemp() {
  const base = std.getenv("TEMP") || std.getenv("TMP") || "/tmp";
  const dir = tg.join(base, "tgpm-test-" + Math.floor(Math.random() * 1e9).toString(36));
  tg.mkdirp(dir);
  return dir;
}

function put(path, text) {
  tg.writeText(path, text);
}

/* ---------------- SHA-256 ---------------- */

eq(tg.sha256Hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "sha256 empty");
eq(tg.sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256 abc");
eq(
  tg.sha256Hex("The quick brown fox jumps over the lazy dog"),
  "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
  "sha256 quick brown fox"
);

/* ---------------- semver ---------------- */

eq(tg.parseVersion("1.2.3").major, 1, "parse major");
eq(tg.parseVersion("1.2.3-alpha.1+build").prerelease.join("."), "alpha.1", "parse prerelease");
check(tg.parseVersion("nope") === null, "reject bad version");
eq(tg.compareVersions("1.2.3", "1.2.4"), -1, "compare patch");
eq(tg.compareVersions("2.0.0", "2.0.0"), 0, "compare equal");
eq(tg.compareVersions("1.0.0", "1.0.0-rc.1"), 1, "release > prerelease");
eq(tg.compareVersions("1.0.0-alpha.2", "1.0.0-alpha.10"), -1, "numeric prerelease compare");

check(tg.satisfies("1.2.3", "^1.0.0"), "caret in range");
check(!tg.satisfies("2.0.0", "^1.0.0"), "caret upper bound");
check(tg.satisfies("0.2.5", "^0.2.3"), "caret zero minor");
check(!tg.satisfies("0.3.0", "^0.2.3"), "caret zero minor bound");
check(tg.satisfies("1.2.9", "~1.2.3"), "tilde patch range");
check(!tg.satisfies("1.3.0", "~1.2.3"), "tilde upper bound");
check(tg.satisfies("1.5.0", ">=1.2.0 <2.0.0"), "comparator conjunction");
check(!tg.satisfies("2.0.0", ">=1.2.0 <2.0.0"), "comparator upper");
check(tg.satisfies("1.9.9", "1.x"), "x-range");
check(tg.satisfies("1.5.0", "1.2 - 1.8"), "hyphen range inside");
check(!tg.satisfies("1.9.9", "1.2 - 1.8"), "hyphen range outside");
check(!tg.satisfies("1.0.0-alpha", "^1.0.0"), "prerelease not matched by caret");
check(tg.satisfies("2.1.0", "1.0.0 || 2.x"), "or range");
eq(tg.maxSatisfying(["1.0.0", "1.2.0", "1.9.0", "2.0.0"], "^1.0.0"), "1.9.0", "maxSatisfying");

/* ---------------- conditional exports ---------------- */

const pkgEx = {
  exports: {
    ".": { mbjs: "./dist/mbjs.js", import: "./dist/esm.js", require: "./dist/cjs.js", default: "./dist/index.js" },
    "./feature": "./dist/feature.js",
    "./plugins/*": "./dist/plugins/*.js",
  },
};
eq(tg.resolveExports(pkgEx, ".", ["mbjs", "import", "default"]), "./dist/mbjs.js", "exports mbjs condition");
eq(tg.resolveExports(pkgEx, ".", ["import", "default"]), "./dist/esm.js", "exports import condition");
eq(tg.resolveExports(pkgEx, ".", ["require", "default"]), "./dist/cjs.js", "exports require condition");
eq(tg.resolveExports(pkgEx, ".", ["browser", "default"]), "./dist/index.js", "exports default fallback");
eq(tg.resolveExports(pkgEx, "./feature", ["default"]), "./dist/feature.js", "exports exact subpath");
eq(tg.resolveExports(pkgEx, "./plugins/a", ["default"]), "./dist/plugins/a.js", "exports wildcard subpath");
check(tg.resolveExports({ main: "./main.js" }, ".", ["default"]) === "./main.js", "exports main fallback");

/* ---------------- manifest / lockfile ---------------- */

eq(tg.validateManifest({ name: "x", version: "1.0.0" }).length, 0, "valid manifest");
check(tg.validateManifest({ name: "x" }).length > 0, "manifest requires version");
check(tg.validateManifest({ version: "1.0.0" }).length > 0, "manifest requires name");
eq(
  tg.stableStringify({ b: 1, a: { d: 2, c: 3 } }),
  tg.stableStringify({ a: { c: 3, d: 2 }, b: 1 }),
  "stableStringify is key-order independent"
);

/* ---------------- tree hashing ---------------- */

const t1 = mkTemp();
put(tg.join(t1, "a.txt"), "alpha\n");
put(tg.join(t1, "sub", "b.txt"), "beta\n");
const h1 = tg.treeHash(t1);
const h2 = tg.treeHash(t1);
eq(h1, h2, "treeHash deterministic");
put(tg.join(t1, "a.txt"), "changed\n");
check(tg.treeHash(t1) !== h1, "treeHash changes with content");

/* ---------------- registry fixtures ---------------- */

const root = mkTemp();
const reg = tg.join(root, "registry");
const home = tg.join(root, "home");
const store = tg.storePaths(home).store;

function makePkg(name, version, manifestExtra, files) {
  const dir = tg.join(reg, name, version);
  tg.mkdirp(dir);
  const m = { name, version, ...(manifestExtra || {}) };
  put(tg.join(dir, "takegawa.json"), JSON.stringify(m));
  const all = { "index.js": "export default " + JSON.stringify(name + "@" + version) + ";\n", ...(files || {}) };
  for (const f of Object.keys(all)) put(tg.join(dir, f), all[f]);
  return dir;
}

makePkg("left-pad", "1.0.0");
makePkg("left-pad", "1.1.0", {
  exports: { ".": { mbjs: "./mbjs.js", default: "./index.js" } },
}, { "mbjs.js": "export const via = 'mbjs';\n" });
makePkg("left-pad", "2.0.0");
makePkg("dev-only", "1.0.0");
makePkg("addon", "2.0.0", { dependencies: { "left-pad": "^1.0.0" }, devDependencies: { "dev-only": "*" } }, { "a.js": "export const a = 1;\n" });

/* ---------------- resolve tree ---------------- */

const proj = tg.join(root, "proj");
tg.mkdirp(proj);
put(
  tg.join(proj, "takegawa.json"),
  JSON.stringify({ name: "proj", version: "0.0.0", dependencies: { addon: "^2.0.0" } })
);

const rootManifest = tg.readManifest(proj);
const tree = tg.resolveTree(rootManifest, { baseDir: proj, registry: reg, home });
check(!!tree.packages["addon"], "resolve includes direct dep");
check(!!tree.packages["left-pad"], "resolve includes transitive dep");
eq(tree.packages["left-pad"].version, "1.1.0", "resolve picks highest satisfying");
eq(tree.packages["addon"].version, "2.0.0", "resolve addon version");
check(!tree.packages["dev-only"], "transitive devDependencies are ignored");

/* ---------------- install (linked, dedup) ---------------- */

const r1 = tg.install(proj, { registry: reg, home, mode: "linked" });
check(tg.isDir(tg.join(proj, tg.MODULES_DIR, "addon")), "install links addon into takegawa_modules");
check(tg.isDir(tg.join(proj, tg.MODULES_DIR, "left-pad")), "install links left-pad into takegawa_modules");
check(tg.isFile(tg.join(proj, tg.LOCKFILE)), "install writes lockfile");

const lock = tg.readLockfile(proj);
check(lock && lock.packages.addon, "lockfile records addon");
eq(lock.packages["left-pad"].version, "1.1.0", "lockfile records resolved version");

const storeCount1 = tg.readdir(store).length;
check(storeCount1 >= 2, "store holds content-addressed entries");

const ver1 = tg.verify(proj, { home });
check(ver1.ok, "verify passes after install");

/* second project shares the same store entries (no piling up) */
const proj2 = tg.join(root, "proj2");
tg.mkdirp(proj2);
put(
  tg.join(proj2, "takegawa.json"),
  JSON.stringify({ name: "proj2", version: "0.0.0", dependencies: { "left-pad": "^1.0.0" } })
);
tg.install(proj2, { registry: reg, home, mode: "linked" });
eq(tg.readdir(store).length, storeCount1, "shared store deduplicates across projects");
check(r1.installed.length === 2, "first install reports two packages");

/* ---------------- install (copy / node_modules) ---------------- */

const proj3 = tg.join(root, "proj3");
tg.mkdirp(proj3);
put(
  tg.join(proj3, "takegawa.json"),
  JSON.stringify({ name: "proj3", version: "0.0.0", dependencies: { "left-pad": "1.0.0" } })
);
tg.install(proj3, { registry: reg, home, mode: "copy" });
const nmFile = tg.join(proj3, tg.NODE_MODULES_DIR, "left-pad", "index.js");
check(tg.isFile(nmFile), "copy mode materialises node_modules");
eq(tg.readText(nmFile).replace(/\r\n/g, "\n"), "export default \"left-pad@1.0.0\";\n", "copy mode preserves file contents");
check(tg.verify(proj3, { home }).ok, "verify passes for copy mode");

/* ---------------- exports resolution from installed pkg ---------------- */

const lp = tg.resolvePackage("left-pad", "1.1.0", { baseDir: proj3, registry: reg, home });
eq(lp.version, "1.1.0", "resolvePackage from copy project");
eq(
  tg.resolveExports(lp.manifest, ".", ["mbjs", "default"]),
  "./mbjs.js",
  "conditional exports honoured from manifest"
);

/* ---------------- lockfile reproducibility / update / frozen ---------------- */

const pinned = tg.readLockfile(proj).packages["left-pad"].version;
makePkg("left-pad", "1.3.0");
tg.install(proj, { registry: reg, home, mode: "linked" });
eq(tg.readLockfile(proj).packages["left-pad"].version, pinned, "install honours the lockfile without --update");
tg.install(proj, { registry: reg, home, mode: "linked", update: true });
eq(tg.readLockfile(proj).packages["left-pad"].version, "1.3.0", "update re-resolves to the newest allowed version");

const frozenEmpty = tg.join(root, "frozen-empty");
tg.mkdirp(frozenEmpty);
put(tg.join(frozenEmpty, "takegawa.json"), JSON.stringify({ name: "frozen-empty", version: "0.0.0", dependencies: { "left-pad": "^1.0.0" } }));
let frozeFailed = false;
try {
  tg.install(frozenEmpty, { registry: reg, home, frozen: true });
} catch (e) {
  frozeFailed = /frozen/.test(String(e && e.message));
}
check(frozeFailed, "frozen install fails without a lockfile");
let frozeOk = true;
try {
  tg.install(proj, { registry: reg, home, frozen: true });
} catch (e) {
  frozeOk = false;
}
check(frozeOk, "frozen install succeeds with an up-to-date lockfile");

/* ---------------- outdated / info ---------------- */

const depProj = tg.join(root, "depproj");
tg.mkdirp(depProj);
put(tg.join(depProj, "takegawa.json"), JSON.stringify({ name: "depproj", version: "0.0.0", dependencies: { "left-pad": "^1.0.0" } }));
tg.writeLockfile(depProj, {
  lockfileVersion: 1,
  format: 1,
  name: "depproj",
  version: "0.0.0",
  mode: "linked",
  packages: { "left-pad": { version: "1.1.0", source: "registry", link: "linked", integrity: "stale" } },
});
const od = tg.outdated(depProj, { registry: reg, home });
check(od.length === 1 && od[0].name === "left-pad" && od[0].wanted === "1.3.0", "outdated reports a newer allowed version");
const mi = tg.info("left-pad@^1.0.0", { registry: reg, home, baseDir: depProj });
eq(mi.version, "1.3.0", "info resolves the newest allowed version");
eq(mi.source, "registry", "info reports the resolution source");
eq(mi.main, "./index.js", "info reports the resolved entry point");

/* ---------------- garbage collection ---------------- */

const stray = tg.join(root, "stray");
tg.mkdirp(stray);
put(tg.join(stray, "x.js"), "x\n");
const strayStore = tg.storePackage(stray, home);
check(tg.isDir(tg.join(store, strayStore.integrity)), "stray store entry exists before gc");
const gc = tg.garbageCollect({ home, root });
check(gc.removed.indexOf(strayStore.integrity) >= 0, "gc removes the unreferenced entry");
check(!tg.isDir(tg.join(store, strayStore.integrity)), "gc deleted the unreferenced entry from disk");
check(gc.kept.length >= 2, "gc keeps referenced entries");

/* ---------------- bin shims ---------------- */

eq(tg.normalizeBin("bin.js", "@scope/tool").length, 1, "normalizeBin handles a string bin");
eq(tg.normalizeBin({ "my-cli": "cli.js" }, "tool")[0][0], "my-cli", "normalizeBin handles an object bin");

makePkg("cli-tool", "1.0.0", { bin: { "hello-tool": "./cli.js" } }, { "cli.js": "console.log('hi');\n" });
const binProj = tg.join(root, "binproj");
tg.mkdirp(binProj);
put(tg.join(binProj, "takegawa.json"), JSON.stringify({ name: "binproj", version: "0.0.0", dependencies: { "cli-tool": "^1.0.0" } }));
const binRes = tg.install(binProj, { registry: reg, home, mode: "linked" });
check(binRes.bins.indexOf("hello-tool") >= 0, "install creates a bin shim");
const shimName = os.platform === "win32" ? "hello-tool.cmd" : "hello-tool";
const shim = tg.join(binProj, tg.MODULES_DIR, ".bin", shimName);
check(tg.isFile(shim), "bin shim exists on disk");
check(tg.readText(shim).indexOf("cli.js") >= 0, "bin shim points at the package entry");

/* ---------------- source specs / remote tarballs ---------------- */

eq(tg.sourceSpec("file:./x").type, "dir", "sourceSpec file: dir");
eq(tg.sourceSpec("./x").type, "dir", "sourceSpec relative dir");
eq(tg.sourceSpec("https://x/y.tgz").type, "remote", "sourceSpec http url");
eq(tg.sourceSpec("x/foo.tgz").type, "remote", "sourceSpec relative tgz is remote");
eq(tg.sourceSpec("C:/x/foo.tgz").type, "remote", "sourceSpec absolute tgz is remote");
eq(tg.sourceSpec("file:foo.tgz").type, "remote", "sourceSpec file: tgz is remote");
eq(tg.sourceSpec("^1.0.0"), null, "sourceSpec range is null");

const tarPkg = makePkg("remote-pkg", "3.2.1", { main: "./index.js" });
const tgzPath = tg.join(root, "remote-pkg-3.2.1.tgz");
{
  const r = tg.shell(tg.cdCmd(tg.dirname(tgzPath), 'tar -czf "' + tg.basename(tgzPath) + '" -C "' + tarPkg + '" .'));
  check(r.ok, "tar pack of a test package");
}
const fp = tg.fetchPackage(tgzPath, { home, baseDir: root });
eq(fp.name, "remote-pkg", "fetchPackage name");
eq(fp.version, "3.2.1", "fetchPackage version");
check(tg.isFile(tg.join(fp.dir, "takegawa.json")), "fetchPackage extracts a manifest");
check(tg.isFile(fp.archive), "fetchPackage caches the archive");

const tarProj = tg.join(root, "tarproj");
tg.mkdirp(tarProj);
put(tg.join(tarProj, "takegawa.json"), JSON.stringify({ name: "tarproj", version: "0.0.0", dependencies: { "remote-pkg": tgzPath } }));
const tarRes = tg.install(tarProj, { registry: reg, home, mode: "linked" });
eq(tg.readLockfile(tarProj).packages["remote-pkg"].version, "3.2.1", "tarball dep installs the pinned version");
check(tg.isFile(tg.join(tarProj, tg.MODULES_DIR, "remote-pkg", "index.js")), "tarball dep is materialized");

/* ---------------- conflict detection ---------------- */

const conflictRoot = tg.join(root, "conflict");
tg.mkdirp(conflictRoot);
makePkg("needs-old", "1.0.0", { dependencies: { "left-pad": "^1.0.0" } });
makePkg("needs-new", "1.0.0", { dependencies: { "left-pad": "^2.0.0" } });
put(
  tg.join(conflictRoot, "takegawa.json"),
  JSON.stringify({ name: "conflict", version: "0.0.0", dependencies: { "needs-old": "*", "needs-new": "*" } })
);
let conflicted = false;
try {
  tg.resolveTree(tg.readManifest(conflictRoot), { baseDir: conflictRoot, registry: reg, home });
} catch (e) {
  conflicted = /conflict/.test(String(e.message));
}
check(conflicted, "incompatible ranges detected as a conflict");

/* ---------------- cleanup ---------------- */

tg.rmrf(root);
tg.rmrf(t1);

if (failures === 0) {
  console.log("== ALL TGPM TESTS PASSED ==");
  std.exit(0);
} else {
  console.log("== " + failures + " TGPM TEST(S) FAILED ==");
  std.exit(1);
}
