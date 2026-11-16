/* ═══════════════════════════════════════════════════════════════════════════
 * berry_runtime.js — Berry Runtime API for Host-Guest Target Selection
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Provides the runtime API that JS scripts use to query and control
 * compilation targets, host profiling, and pipeline selection.
 *
 * API:
 *   berry.getTarget()      → 'host' | 'guest' | 'auto'
 *   berry.setTarget(t)     → void
 *   berry.getArch()        → 'x86_64' | 'arm64' | 'riscv64' | ...
 *   berry.setArch(a)       → void
 *   berry.getPipeline()    → 'interpreter' | 'jit' | 'aot' | 'befb'
 *   berry.setPipeline(p)   → void
 *   berry.profileHost()    → { os, arch, features, cores, cache, ... }
 *   berry.getOptLevel()    → 0..3
 *   berry.setOptLevel(n)   → void
 *   berry.getProfile()     → { target, arch, pipeline, opt_level, ... }
 *   berry.compile(src)     → compile with current config
 *   berry.compileTo(src, target) → compile to specific target
 *   berry.supportsSIMD()   → boolean
 *   berry.getSIMDType()    → 'avx2' | 'neon' | 'sse4.2' | 'scalar' | ...
 *   berry.isBerryOS()      → boolean
 *   berry.getOS()          → 'linux' | 'macos' | 'uberrynix' | ...
 *   berry.getCores()       → number
 *   berry.getMemory()      → bytes
 *   berry.getCacheLine()   → bytes
 *   berry.getPageSize()    → bytes
 *   berry.benchmarkCPU()   → ops/sec
 *   berry.benchmarkMem()   → bytes/sec
 *   berry.selectBestTarget() → auto-detect best target
 * ═══════════════════════════════════════════════════════════════════════════ */

// ─── Internal State ─────────────────────────────────────────────────────
var _target = "auto";
var _arch = "auto";
var _pipeline = "auto";
var _opt_level = 2;
var _debug = false;
var _gc = "region";
var _cached_profile = null;
var _cached_quick = null;

// ─── Target Management ──────────────────────────────────────────────────
function getTarget() { return _target; }
function setTarget(t) {
    if (t === "host" || t === "guest" || t === "auto") _target = t;
    else throw new Error("Invalid target: " + t + " (use 'host', 'guest', or 'auto')");
}

function getArch() { return _arch; }
function setArch(a) {
    _arch = a;
}

function getPipeline() { return _pipeline; }
function setPipeline(p) {
    if (["interpreter", "jit", "aot", "befb", "auto"].indexOf(p) >= 0)
        _pipeline = p;
    else throw new Error("Invalid pipeline: " + p);
}

function getOptLevel() { return _opt_level; }
function setOptLevel(n) {
    if (n >= 0 && n <= 3) _opt_level = n;
    else throw new Error("Invalid opt level: " + n + " (use 0-3)");
}

function isDebug() { return _debug; }
function setDebug(d) { _debug = d; }

function getGC() { return _gc; }
function setGC(g) { _gc = g; }

// ─── Host Profiling ─────────────────────────────────────────────────────
function profileHost() {
    if (_cached_profile) return _cached_profile;
    try {
        var hp = require("./platform/host_profile.js");
        _cached_profile = hp.HostProfile.profileHost();
    } catch(e) {
        // Fallback: basic profile
        _cached_profile = {
            os: "unknown",
            arch: "x86_64",
            features: [],
            cores: 1,
            cache: { l1d: 32768, l1i: 32768, l2: 262144, l3: 0, line: 64 },
            memory: 0,
            page_size: 4096,
            simd: { has_simd: false, simd_type: "scalar", width_bytes: 1 },
            is_berry: false,
            benchmark: { cpu_ops_sec: 0, mem_bandwidth: 0 },
            recommendation: {
                target: "host",
                pipeline: "jit",
                arch: "x86_64",
                opt_level: 2,
                use_simd: false,
                simd_type: "scalar",
                rationale: ["fallback profile"]
            }
        };
    }
    return _cached_profile;
}

function quickProfile() {
    if (_cached_quick) return _cached_quick;
    try {
        var hp = require("./platform/host_profile.js");
        _cached_quick = hp.HostProfile.quickProfile();
    } catch(e) {
        _cached_quick = { os: "unknown", arch: "x86_64", features: [], cores: 1, is_berry: false };
    }
    return _cached_quick;
}

// ─── Convenience Queries ────────────────────────────────────────────────
function getOS() { return quickProfile().os; }
function isBerryOS() { return quickProfile().is_berry; }
function getCores() { return profileHost().cores; }
function getMemory() { return profileHost().memory; }
function getCacheLine() { return profileHost().cache.line; }
function getPageSize() { return profileHost().page_size; }

function supportsSIMD() { return profileHost().simd.has_simd; }
function getSIMDType() { return profileHost().simd.simd_type; }

function benchmarkCPU() { return profileHost().benchmark.cpu_ops_sec; }
function benchmarkMem() { return profileHost().benchmark.mem_bandwidth; }

// ─── Full Profile ───────────────────────────────────────────────────────
function getProfile() {
    var host = profileHost();
    var arch = _arch === "auto" ? host.arch : _arch;
    var target = _target === "auto" ?
        (host.is_berry ? "guest" : "host") : _target;
    var pipeline = _pipeline === "auto" ?
        (target === "guest" ? "befb" : "jit") : _pipeline;

    return {
        target: target,
        arch: arch,
        beasm_arch: require("./platform/target_select.js").TargetSelect.archToBeASM(arch),
        pipeline: pipeline,
        opt_level: _opt_level,
        debug: _debug,
        gc: _gc,
        host: host,
        simd: host.simd,
        cores: host.cores,
        memory: host.memory,
        cache_line: host.cache.line,
    };
}

// ─── Target Resolution ──────────────────────────────────────────────────
function selectBestTarget() {
    var profile = profileHost();
    var rec = profile.recommendation;
    return {
        target: rec.target,
        pipeline: rec.pipeline,
        arch: rec.arch,
        opt_level: rec.opt_level,
        simd: rec.simd_type,
        rationale: rec.rationale,
    };
}

// ─── Compile Functions ──────────────────────────────────────────────────
function compile(source) {
    var config = getProfile();
    return {
        source: source,
        config: config,
        command: buildCommand(config, source),
    };
}

function compileTo(source, target) {
    var oldTarget = _target;
    _target = target;
    var result = compile(source);
    _target = oldTarget;
    return result;
}

function buildCommand(config, source) {
    switch (config.pipeline) {
        case "interpreter":
            return "mbjs <script>";
        case "jit":
            return "mbjs -e \"import * as jit from 'mbjs:jit'; jit.enable()\" <script>";
        case "aot":
            return "sh scripts/build-native.sh <script>";
        case "befb":
            var flag = require("./platform/target_select.js").TargetSelect.archToGASFlag(config.arch);
            return [
                "mbjs -m transpilers/mbjs-to-native.js <script> " + flag + " -o <output>.s",
                "as --64 <output>.s -o <output>.o && ld <output>.o -o <output>",
                "mbjs -m transpilers/mbjs-to-native.js <script> --basm -o <output>.basm",
                "beasm <output>.basm -o <output>.befb --format befb --entry " + config.entry,
            ].join("\n");
        default:
            return "mbjs <script>";
    }
}

// ─── Berry OS Info ──────────────────────────────────────────────────────
function getOSInfo() {
    var info = { name: "unknown", version: "", arch: "unknown" };
    try {
        var os_release = std.loadFile("/etc/os-release");
        if (os_release) {
            var name_match = os_release.match(/PRETTY_NAME="?([^"\n]+)"?/);
            if (name_match) info.name = name_match[1].trim();
            var ver_match = os_release.match(/VERSION="?([^"\n]+)"?/);
            if (ver_match) info.version = ver_match[1].trim();
        }
    } catch(e) {}
    info.arch = detectArch();
    return info;
}

function detectArch() {
    try {
        var p = std.popen("uname -m 2>/dev/null", "r");
        if (p) {
            var line = p.getline();
            p.close();
            if (line) return line.trim();
        }
    } catch(e) {}
    return "unknown";
}

// ─── Export ──────────────────────────────────────────────────────────────
if (typeof globalThis !== "undefined") {
    globalThis.berry = {
        // Target management
        getTarget: getTarget,
        setTarget: setTarget,
        getArch: getArch,
        setArch: setArch,
        getPipeline: getPipeline,
        setPipeline: setPipeline,
        getOptLevel: getOptLevel,
        setOptLevel: setOptLevel,
        isDebug: isDebug,
        setDebug: setDebug,
        getGC: getGC,
        setGC: setGC,

        // Profiling
        profileHost: profileHost,
        quickProfile: quickProfile,
        getProfile: getProfile,
        selectBestTarget: selectBestTarget,

        // Host queries
        getOS: getOS,
        isBerryOS: isBerryOS,
        getCores: getCores,
        getMemory: getMemory,
        getCacheLine: getCacheLine,
        getPageSize: getPageSize,
        supportsSIMD: supportsSIMD,
        getSIMDType: getSIMDType,
        benchmarkCPU: benchmarkCPU,
        benchmarkMem: benchmarkMem,
        getOSInfo: getOSInfo,

        // Compilation
        compile: compile,
        compileTo: compileTo,
    };
}
