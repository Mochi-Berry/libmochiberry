/* ═══════════════════════════════════════════════════════════════════════════
 * host_profile.js — Host Profiling Engine for MochiBerry
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Detects host OS, architecture, CPU features, cache topology,
 * memory bandwidth, and provides compilation target recommendations.
 *
 * Usage:
 *   import * as host from './platform/host_profile.js';
 *   const profile = host.profileHost();
 *   print(`OS: ${profile.os}, Arch: ${profile.arch}`);
 *   print(`Recommended target: ${profile.recommendation.target}`);
 *   print(`Recommended pipeline: ${profile.recommendation.pipeline}`);
 * ═══════════════════════════════════════════════════════════════════════════ */

// ─── OS Detection ───────────────────────────────────────────────────────
function detectOS() {
    // Try /etc/os-release first
    try {
        var f = std.loadFile("/etc/os-release");
        if (f) {
            if (f.indexOf("UBerryNix") >= 0 || f.indexOf("uberrynix") >= 0) return "uberrynix";
            if (f.indexOf("Berry") >= 0 || f.indexOf("berry") >= 0) return "berry";
            if (f.indexOf("Linux") >= 0) return "linux";
        }
    } catch(e) {}

    // Try uname
    try {
        var p = std.popen("uname -s 2>/dev/null", "r");
        if (p) {
            var line = p.getline();
            p.close();
            if (line) {
                line = line.trim();
                if (line === "Linux") return "linux";
                if (line === "Darwin") return "macos";
                if (line === "FreeBSD") return "freebsd";
                if (line === "UBerryNix") return "uberrynix";
                return line.toLowerCase();
            }
        }
    } catch(e) {}

    // Try os module
    try {
        var os_mod = std.loadFile("/proc/sys/kernel/ostype");
        if (os_mod && os_mod.indexOf("Linux") >= 0) return "linux";
    } catch(e) {}

    return "unknown";
}

// ─── Architecture Detection ─────────────────────────────────────────────
function detectArch() {
    // Try uname -m
    try {
        var p = std.popen("uname -m 2>/dev/null", "r");
        if (p) {
            var line = p.getline();
            p.close();
            if (line) {
                line = line.trim();
                if (line === "x86_64" || line === "amd64") return "x86_64";
                if (line === "i386" || line === "i486" || line === "i586" || line === "i686") return "x86_32";
                if (line === "aarch64" || line === "arm64") return "arm64";
                if (line.indexOf("arm") === 0) return "arm32";
                if (line === "riscv32" || line === "riscv64" || line === "riscv128") return line;
                if (line === "x86_16") return "x86_16";
                return line;
            }
        }
    } catch(e) {}

    // Try /proc/cpuinfo
    try {
        var cpuinfo = std.loadFile("/proc/cpuinfo");
        if (cpuinfo) {
            if (cpuinfo.indexOf("GenuineIntel") >= 0 || cpuinfo.indexOf("AuthenticAMD") >= 0) {
                if (cpuinfo.indexOf("lm") >= 0) return "x86_64";
                return "x86_32";
            }
            if (cpuinfo.indexOf("ARMv7") >= 0 || cpuinfo.indexOf("ARMv6") >= 0) return "arm32";
            if (cpuinfo.indexOf("aarch64") >= 0) return "arm64";
            if (cpuinfo.indexOf("riscv") >= 0) return "riscv64";
        }
    } catch(e) {}

    return "unknown";
}

// ─── CPU Features Detection ─────────────────────────────────────────────
function detectCPUFeatures() {
    var features = [];

    // Try /proc/cpuinfo flags
    try {
        var cpuinfo = std.loadFile("/proc/cpuinfo");
        if (cpuinfo) {
            var flagsMatch = cpuinfo.match(/flags\s*:\s*(.+)/);
            if (flagsMatch) {
                var flags = flagsMatch[1].split(/\s+/);
                for (var i = 0; i < flags.length; i++) {
                    features.push(flags[i]);
                }
            }
            // ARM features
            var featuresMatch = cpuinfo.match(/Features\s*:\s*(.+)/);
            if (featuresMatch) {
                var feats = featuresMatch[1].split(/\s+/);
                for (var i = 0; i < feats.length; i++) {
                    features.push(feats[i]);
                }
            }
        }
    } catch(e) {}

    // Check for specific features
    var arch = detectArch();
    var featureMap = {
        "sse":       "sse",
        "sse2":      "sse2",
        "sse3":      "sse3",
        "ssse3":     "ssse3",
        "sse4_1":    "sse4.1",
        "sse4_2":    "sse4.2",
        "avx":       "avx",
        "avx2":      "avx2",
        "avx512f":   "avx512f",
        "avx512bw":  "avx512bw",
        "avx512vl":  "avx512vl",
        "fma":       "fma",
        "fma4":      "fma4",
        "aes":       "aes-ni",
        "pclmulqdq": "pclmulqdq",
        "popcnt":    "popcnt",
        "lzcnt":     "lzcnt",
        "bmi1":      "bmi1",
        "bmi2":      "bmi2",
        "sha_ni":    "sha-ni",
        "rdseed":    "rdseed",
        "rdrand":    "rdrand",
        "erms":      "erms",
        "rep_movsb": "rep-movsb",
        "neon":      "neon",
        "asimd":     "asimd",
        "fp":        "fp",
        "atomics":   "atomics",
        "zba":       "zba",
        "zbb":       "zbb",
        "zbc":       "zbc",
        "zbs":       "zbs",
        "rvv":       "rvv",
        "i_":        "rv32i",
        "m_":        "rv32m",
        "a_":        "rv32a",
        "c_":        "rv32c",
    };

    var normalized = [];
    for (var key in featureMap) {
        for (var i = 0; i < features.length; i++) {
            if (features[i] === key || features[i].indexOf(key) === 0) {
                if (normalized.indexOf(featureMap[key]) < 0) {
                    normalized.push(featureMap[key]);
                }
            }
        }
    }

    return normalized;
}

// ─── Core Count Detection ───────────────────────────────────────────────
function detectCores() {
    // nproc
    try {
        var p = std.popen("nproc 2>/dev/null", "r");
        if (p) {
            var line = p.getline();
            p.close();
            if (line) {
                var n = parseInt(line.trim());
                if (n > 0) return n;
            }
        }
    } catch(e) {}

    // /proc/cpuinfo count
    try {
        var cpuinfo = std.loadFile("/proc/cpuinfo");
        if (cpuinfo) {
            return (cpuinfo.match(/^processor\s*:/gm) || []).length || 1;
        }
    } catch(e) {}

    return 1;
}

// ─── Cache Detection ────────────────────────────────────────────────────
function detectCache() {
    var cache = {
        l1d: 0,  // L1 data cache bytes
        l1i: 0,  // L1 instruction cache bytes
        l2: 0,   // L2 cache bytes
        l3: 0,   // L3 cache bytes
        line: 64 // cache line size (default)
    };

    // Try sysconf-like detection via /proc/cpuinfo or lscpu
    try {
        var p = std.popen("lscpu 2>/dev/null", "r");
        if (p) {
            var line;
            while ((line = p.getline()) !== null) {
                if (line.indexOf("L1d cache:") === 0) {
                    cache.l1d = parseCacheSize(line.split(":")[1].trim());
                } else if (line.indexOf("L1i cache:") === 0) {
                    cache.l1i = parseCacheSize(line.split(":")[1].trim());
                } else if (line.indexOf("L2 cache:") === 0) {
                    cache.l2 = parseCacheSize(line.split(":")[1].trim());
                } else if (line.indexOf("L3 cache:") === 0) {
                    cache.l3 = parseCacheSize(line.split(":")[1].trim());
                } else if (line.indexOf("Line size:") === 0 || line.indexOf("cache line") === 0) {
                    var val = parseInt(line.split(":")[1].trim());
                    if (val > 0) cache.line = val;
                }
            }
            p.close();
        }
    } catch(e) {}

    return cache;
}

function parseCacheSize(str) {
    if (!str) return 0;
    str = str.trim().toLowerCase();
    var val = parseFloat(str);
    if (isNaN(val)) return 0;
    if (str.indexOf("k") >= 0 || str.indexOf("kib") >= 0) return Math.floor(val * 1024);
    if (str.indexOf("m") >= 0 || str.indexOf("mib") >= 0) return Math.floor(val * 1024 * 1024);
    return Math.floor(val);
}

// ─── Memory Size Detection ──────────────────────────────────────────────
function detectMemory() {
    try {
        var p = std.popen("free -b 2>/dev/null | grep Mem", "r");
        if (p) {
            var line = p.getline();
            p.close();
            if (line) {
                var parts = line.split(/\s+/);
                if (parts.length >= 2) {
                    var total = parseInt(parts[1]);
                    if (total > 0) return total;
                }
            }
        }
    } catch(e) {}

    try {
        var meminfo = std.loadFile("/proc/meminfo");
        if (meminfo) {
            var match = meminfo.match(/MemTotal:\s+(\d+)\s+kB/);
            if (match) return parseInt(match[1]) * 1024;
        }
    } catch(e) {}

    return 0;
}

// ─── Page Size Detection ────────────────────────────────────────────────
function detectPageSize() {
    try {
        var p = std.popen("getconf PAGE_SIZE 2>/dev/null", "r");
        if (p) {
            var line = p.getline();
            p.close();
            if (line) {
                var val = parseInt(line.trim());
                if (val > 0) return val;
            }
        }
    } catch(e) {}

    return 4096; // default
}

// ─── Is Berry OS Detection ──────────────────────────────────────────────
function isBerryOS() {
    try {
        var f = std.loadFile("/etc/os-release");
        if (f && (f.indexOf("UBerryNix") >= 0 || f.indexOf("Berry") >= 0)) return true;
    } catch(e) {}
    try {
        var f = std.loadFile("/proc/sys/kernel/hostname");
        if (f && f.indexOf("berry") >= 0) return true;
    } catch(e) {}
    return false;
}

// ─── Benchmark Functions ────────────────────────────────────────────────
function benchmarkMemoryBandwidth() {
    // Simple sequential read benchmark
    var size = 4 * 1024 * 1024; // 4MB
    var buf = new ArrayBuffer(size);
    var view = new Uint8Array(buf);
    for (var i = 0; i < size; i += 64) view[i] = 1;

    var start = Date.now();
    var sum = 0;
    for (var iter = 0; iter < 10; iter++) {
        for (var i = 0; i < size; i += 64) {
            sum += view[i];
        }
    }
    var elapsed = (Date.now() - start) / 1000;
    if (elapsed < 0.001) elapsed = 0.001;

    return (size * 10) / elapsed; // bytes/sec
}

function benchmarkCPU() {
    // Simple integer arithmetic benchmark
    var iterations = 10000000;
    var start = Date.now();
    var result = 0;
    for (var i = 0; i < iterations; i++) {
        result += i;
        result ^= (i * 7);
        result = (result << 1) | (result >>> 31);
    }
    var elapsed = (Date.now() - start) / 1000;
    if (elapsed < 0.001) elapsed = 0.001;

    return iterations / elapsed; // ops/sec
}

// ─── SIMD Benchmark ─────────────────────────────────────────────────────
function benchmarkSIMD() {
    // Test if SIMD operations are faster than scalar
    // This is a heuristic — we can't actually run AVX from JS
    // but we can estimate based on CPU features
    var features = detectCPUFeatures();
    var hasSIMD = false;
    var simdType = "scalar";

    if (features.indexOf("avx512f") >= 0) { hasSIMD = true; simdType = "avx512"; }
    else if (features.indexOf("avx2") >= 0) { hasSIMD = true; simdType = "avx2"; }
    else if (features.indexOf("avx") >= 0) { hasSIMD = true; simdType = "avx"; }
    else if (features.indexOf("sse4.2") >= 0) { hasSIMD = true; simdType = "sse4.2"; }
    else if (features.indexOf("sse4.1") >= 0) { hasSIMD = true; simdType = "sse4.1"; }
    else if (features.indexOf("ssse3") >= 0) { hasSIMD = true; simdType = "ssse3"; }
    else if (features.indexOf("sse2") >= 0) { hasSIMD = true; simdType = "sse2"; }
    else if (features.indexOf("neon") >= 0) { hasSIMD = true; simdType = "neon"; }
    else if (features.indexOf("rvv") >= 0) { hasSIMD = true; simdType = "rvv"; }

    return {
        has_simd: hasSIMD,
        simd_type: simdType,
        width_bytes: simdType === "avx512" ? 64 :
                     simdType === "avx2" || simdType === "avx" ? 32 :
                     simdType === "neon" || simdType === "sse2" ||
                     simdType === "sse4.1" || simdType === "sse4.2" ||
                     simdType === "ssse3" ? 16 :
                     simdType === "rvv" ? 16 : 1
    };
}

// ─── Compilation Recommendation Engine ──────────────────────────────────
function recommend(profile) {
    var rec = {
        target: "auto",
        pipeline: "interpreter",
        arch: profile.arch,
        opt_level: 1,
        use_simd: false,
        simd_type: "scalar",
        rationale: []
    };

    // Detect Berry OS
    if (profile.os === "uberrynix" || profile.os === "berry") {
        rec.target = "guest";
        rec.pipeline = "befb";
        rec.rationale.push("Running on Berry OS — target guest (BEFB)");
    } else {
        rec.target = "host";
        rec.pipeline = "aot";
        rec.rationale.push("Running on " + profile.os + " — target host (ELF)");
    }

    // Pipeline selection based on workload
    if (rec.target === "host") {
        // Host: prefer JIT for long-running, AOT for startup
        rec.pipeline = "jit";
        rec.rationale.push("Host pipeline: JIT for adaptive optimization");
    } else {
        // Guest: always AOT to BEFB (Ring-0, no runtime overhead)
        rec.pipeline = "befb";
        rec.rationale.push("Guest pipeline: AOT to BEFB (Ring-0 native)");
    }

    // Optimization level based on resources
    if (profile.memory > 4 * 1024 * 1024 * 1024) {
        rec.opt_level = 3;
        rec.rationale.push("4GB+ RAM detected — max optimization");
    } else if (profile.memory > 1 * 1024 * 1024 * 1024) {
        rec.opt_level = 2;
        rec.rationale.push("1GB+ RAM detected — balanced optimization");
    } else {
        rec.opt_level = 1;
        rec.rationale.push("Limited RAM — conservative optimization");
    }

    // SIMD recommendation
    var simd = profile.simd;
    if (simd.has_simd) {
        rec.use_simd = true;
        rec.simd_type = simd.simd_type;
        rec.rationale.push("SIMD available: " + simd.simd_type + " (" + simd.width_bytes + " bytes)");
    } else {
        rec.rationale.push("No SIMD detected — scalar mode");
    }

    // Multi-core optimization
    if (profile.cores > 1) {
        rec.rationale.push(profile.cores + " cores detected — parallel-friendly optimizations enabled");
    }

    // Architecture-specific recommendations
    if (profile.arch === "x86_64") {
        if (profile.features.indexOf("bmi1") >= 0 && profile.features.indexOf("bmi2") >= 0) {
            rec.rationale.push("BMI1/BMI2 available — use BMI instructions for bit manipulation");
        }
        if (profile.features.indexOf("popcnt") >= 0) {
            rec.rationale.push("POPCNT available — use for population count");
        }
        if (profile.features.indexOf("lzcnt") >= 0) {
            rec.rationale.push("LZCNT available — use for leading zero count");
        }
    }

    return rec;
}

// ─── Main Profile Function ──────────────────────────────────────────────
function profileHost() {
    var arch = detectArch();
    var os = detectOS();
    var features = detectCPUFeatures();
    var cores = detectCores();
    var cache = detectCache();
    var memory = detectMemory();
    var pageSize = detectPageSize();
    var simd = benchmarkSIMD();
    var is_berry = isBerryOS();

    var profile = {
        os: os,
        arch: arch,
        features: features,
        cores: cores,
        cache: cache,
        memory: memory,
        page_size: pageSize,
        simd: simd,
        is_berry: is_berry,
        benchmark: {
            cpu_ops_sec: 0,
            mem_bandwidth: 0
        }
    };

    // Run benchmarks (optional, can be slow)
    try {
        profile.benchmark.cpu_ops_sec = benchmarkCPU();
        profile.benchmark.mem_bandwidth = benchmarkMemoryBandwidth();
    } catch(e) {}

    // Generate recommendation
    profile.recommendation = recommend(profile);

    return profile;
}

// ─── Quick Profile (no benchmarks) ──────────────────────────────────────
function quickProfile() {
    return {
        os: detectOS(),
        arch: detectArch(),
        features: detectCPUFeatures(),
        cores: detectCores(),
        is_berry: isBerryOS()
    };
}

// ─── Architecture to BeASM .arch mapping ────────────────────────────────
function archToBeASM(arch) {
    var map = {
        "x86_64":  "x86_64",
        "x86_32":  "x86_32",
        "x86_16":  "x86_16",
        "arm64":   "arm64",
        "aarch64": "arm64",
        "arm32":   "arm32",
        "arm":     "arm32",
        "riscv32": "riscv32",
        "rv32":    "riscv32",
        "riscv64": "riscv64",
        "rv64":    "riscv64",
        "riscv128":"riscv128",
        "rv128":   "riscv128",
    };
    return map[arch] || "x86_64";
}

// ─── Export ──────────────────────────────────────────────────────────────
if (typeof globalThis !== "undefined") {
    globalThis.HostProfile = {
        profileHost: profileHost,
        quickProfile: quickProfile,
        detectOS: detectOS,
        detectArch: detectArch,
        detectCPUFeatures: detectCPUFeatures,
        detectCores: detectCores,
        detectCache: detectCache,
        detectMemory: detectMemory,
        detectPageSize: detectPageSize,
        isBerryOS: isBerryOS,
        archToBeASM: archToBeASM,
        benchmarkCPU: benchmarkCPU,
        benchmarkMemoryBandwidth: benchmarkMemoryBandwidth,
        benchmarkSIMD: benchmarkSIMD,
    };
}
