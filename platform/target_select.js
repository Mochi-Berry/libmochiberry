/* ═══════════════════════════════════════════════════════════════════════════
 * target_select.js — Host-Guest Target Selection & Pragma Parser
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Parses compile-time pragmas from JS source and resolves the optimal
 * compilation pipeline (interpreter / JIT / AOT / BeASM→BEFB).
 *
 * Supported pragmas (in comments at top of file):
 *   // @target host|guest|auto
 *   // @arch x86_64|arm64|riscv64|...
 *   // @opt level=0..3|size|speed|balanced
 *   // @profile host|guest
 *   // @pipeline interpreter|jit|aot|befb
 *   // @debug on|off
 *   // @gc none|region|incremental
 *
 * Usage:
 *   import * as ts from './platform/target_select.js';
 *   const config = ts.resolveTarget(sourceCode);
 *   print(config); // { target, arch, opt, pipeline, ... }
 * ═══════════════════════════════════════════════════════════════════════════ */

// ─── Pragma Regex Patterns ──────────────────────────────────────────────
var PRAGMA_PATTERNS = {
    target:   /\/\/\s*@target\s+(host|guest|auto)\s*$/m,
    arch:     /\/\/\s*@arch\s+(x86_64|x86_32|x86_16|arm64|aarch64|arm32|arm|riscv32|rv32|riscv64|rv64|riscv128|rv128)\s*$/m,
    opt:      /\/\/\s*@opt\s+(level=[0-3]|size|speed|balanced)\s*$/m,
    profile:  /\/\/\s*@profile\s+(host|guest)\s*$/m,
    pipeline: /\/\/\s*@pipeline\s+(interpreter|jit|aot|befb)\s*$/m,
    debug:    /\/\/\s*@debug\s+(on|off)\s*$/m,
    gc:       /\/\/\s*@gc\s+(none|region|incremental|boehm)\s*$/m,
    stack:    /\/\/\s*@stack\s+(\d+)\s*$/m,
    memory:   /\/\/\s*@memory\s+(\d+)\s*$/m,
    entry:    /\/\/\s*@entry\s+(\w+)\s*$/m,
    format:   /\/\/\s*@format\s+(befb|bex|bdl|elf|pe|macho)\s*$/m,
};

// ─── Parse All Pragmas from Source ──────────────────────────────────────
function parsePragmas(source) {
    var pragmas = {};

    for (var key in PRAGMA_PATTERNS) {
        var match = source.match(PRAGMA_PATTERNS[key]);
        if (match) {
            pragmas[key] = match[1];
        }
    }

    // Parse @opt level=N specially
    if (pragmas.opt && pragmas.opt.indexOf("level=") === 0) {
        pragmas.opt_level = parseInt(pragmas.opt.split("=")[1]);
        pragmas.opt = "level";
    }

    // Parse @stack and @memory as integers
    if (pragmas.stack) pragmas.stack_size = parseInt(pragmas.stack);
    if (pragmas.memory) pragmas.memory_limit = parseInt(pragmas.memory);

    return pragmas;
}

// ─── Resolve Compilation Target ─────────────────────────────────────────
function resolveTarget(source, hostProfile) {
    var pragmas = parsePragmas(source);

    var config = {
        // Target platform
        target: pragmas.target || "auto",      // host, guest, auto
        arch: pragmas.arch || "auto",          // x86_64, arm64, etc.

        // Optimization
        opt: pragmas.opt || "balanced",        // size, speed, balanced, level=N
        opt_level: pragmas.opt_level || 2,     // 0-3

        // Pipeline selection
        pipeline: pragmas.pipeline || "auto",  // interpreter, jit, aot, befb, auto

        // Runtime configuration
        debug: pragmas.debug || "off",
        gc: pragmas.gc || "region",
        stack_size: pragmas.stack_size || 0,
        memory_limit: pragmas.memory_limit || 0,
        entry: pragmas.entry || "_start",
        format: pragmas.format || "auto",

        // Profiling
        profile: pragmas.profile || "auto",

        // Internal
        _pragmas: pragmas,
        _resolved: false
    };

    // Resolve auto values based on host profile
    if (hostProfile) {
        resolveAuto(config, hostProfile);
    } else {
        // Use minimal defaults
        if (config.arch === "auto") config.arch = "x86_64";
        if (config.target === "auto") config.target = "host";
        if (config.pipeline === "auto") config.pipeline = "jit";
    }

    config._resolved = true;
    return config;
}

// ─── Resolve Auto Settings ──────────────────────────────────────────────
function resolveAuto(config, profile) {
    // Resolve arch
    if (config.arch === "auto") {
        config.arch = profile.arch;
    }

    // Resolve target
    if (config.target === "auto") {
        if (profile.is_berry || profile.os === "uberrynix" || profile.os === "berry") {
            config.target = "guest";
        } else {
            config.target = "host";
        }
    }

    // Resolve pipeline
    if (config.pipeline === "auto") {
        if (config.target === "guest") {
            config.pipeline = "befb";  // Berry OS → BEFB binary
        } else {
            // Host: choose based on opt level
            if (config.opt === "speed") {
                config.pipeline = "jit";  // JIT for maximum speed
            } else if (config.opt === "size") {
                config.pipeline = "aot";  // AOT for minimal size
            } else {
                config.pipeline = "jit";  // Default: JIT for flexibility
            }
        }
    }

    // Resolve format
    if (config.format === "auto") {
        if (config.target === "guest") {
            config.format = "befb";
        } else {
            config.format = "elf";
        }
    }

    // Resolve opt_level from named options
    if (config.opt_level === 2) {
        if (config.opt === "speed") config.opt_level = 3;
        else if (config.opt === "size") config.opt_level = 1;
        else if (config.opt === "balanced") config.opt_level = 2;
    }

    // Apply profile recommendation
    if (config.profile !== "auto" && profile.recommendation) {
        if (config.profile === "host") {
            // Use host profiling results
            if (profile.recommendation.use_simd) {
                config._simd = profile.recommendation.simd_type;
            }
            config._cores = profile.cores;
            config._cache = profile.cache;
        }
    }

    // Architecture to BeASM mapping
    config.beasm_arch = archToBeASM(config.arch);

    return config;
}

// ─── Architecture Mapping ───────────────────────────────────────────────
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

// ─── Architecture to GAS Output Flag ────────────────────────────────────
function archToGASFlag(arch) {
    var map = {
        "x86_64":  "--gas",
        "x86_32":  "--gas",
        "x86_16":  "--gas",
        "arm64":   "--gas-arm64",
        "arm32":   "--gas-arm32",
        "riscv32": "--gas-rv32",
        "riscv64": "--gas-rv64",
        "riscv128":"--gas-rv128",
    };
    return map[arch] || "--gas";
}

// ─── Build Compilation Command ──────────────────────────────────────────
function buildCompileCommand(config) {
    var cmd = [];

    switch (config.pipeline) {
        case "interpreter":
            // No compilation needed — just run with mbjs
            cmd.push("build/mbjs");
            if (config._pragmas.stack_size) cmd.push("--stack-size " + config._pragmas.stack_size);
            break;

        case "jit":
            // JIT via mbjs_jit_compile
            cmd.push("build/mbjs");
            cmd.push("-e");
            cmd.push('"import * as jit from \'mbjs:jit\'; jit.enable({cache_dir:\'./.mbjso\',cc_flags:\'-O2\'})"');
            break;

        case "aot":
            // AOT native compilation
            cmd.push("sh scripts/build-native.sh");
            cmd.push("${INPUT_FILE}");
            break;

        case "befb":
            // JS → .basm → BEFB
            if (config.beasm_arch === "x86_64") {
                cmd.push("build/mbjs -m transpilers/mbjs-to-native.js ${INPUT_FILE} --basm -o ${OUTPUT_BASE}.basm");
            } else {
                cmd.push("build/mbjs -m transpilers/mbjs-to-native.js ${INPUT_FILE} --basm -o ${OUTPUT_BASE}.basm");
            }
            cmd.push("beasm/beasm ${OUTPUT_BASE}.basm -o ${OUTPUT_BASE}.befb --format befb --entry " + config.entry);
            break;

        default:
            cmd.push("build/mbjs ${INPUT_FILE}");
    }

    return cmd;
}

// ─── Generate Compilation Manifest ──────────────────────────────────────
function generateManifest(config) {
    var manifest = {
        target: config.target,
        arch: config.arch,
        beasm_arch: config.beasm_arch,
        pipeline: config.pipeline,
        format: config.format,
        opt_level: config.opt_level,
        debug: config.debug,
        gc: config.gc,
        entry: config.entry,
        stack_size: config.stack_size,
        memory_limit: config.memory_limit,
        commands: buildCompileCommand(config),
    };

    if (config._simd) manifest.simd = config._simd;
    if (config._cores) manifest.cores = config._cores;

    return manifest;
}

// ─── Export ──────────────────────────────────────────────────────────────
if (typeof globalThis !== "undefined") {
    globalThis.TargetSelect = {
        parsePragmas: parsePragmas,
        resolveTarget: resolveTarget,
        archToBeASM: archToBeASM,
        archToGASFlag: archToGASFlag,
        buildCompileCommand: buildCompileCommand,
        generateManifest: generateManifest,
    };
}
