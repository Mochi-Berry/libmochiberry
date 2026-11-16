/* ═══════════════════════════════════════════════════════════════════════════
 * host_guest_demo.js — Host-Guest Selection & Profiling Demo
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Demonstrates the Host-Guest target selection system:
 *   1. Host profiling (OS, arch, CPU features, cache, memory)
 *   2. Pragma-based target selection (@target, @arch, @opt)
 *   3. Runtime API (berry.getTarget(), berry.profileHost())
 *   4. Automatic pipeline selection (interpreter/JIT/AOT/BEFB)
 *   5. SIMD detection and optimization recommendations
 *
 * Run with:
 *   build/mbjs -m platform/host_profile.js -m platform/target_select.js \
 *              -m platform/berry_runtime.js examples/host_guest_demo.js
 * ═══════════════════════════════════════════════════════════════════════════ */

// @target auto
// @arch auto
// @opt balanced
// @profile host

import * as std from "mbjs:std";

// ─── Load Host Profiling Engine ─────────────────────────────────────────
var hp_code = std.loadFile("platform/host_profile.js");
std.evalScript(hp_code);

var ts_code = std.loadFile("platform/target_select.js");
std.evalScript(ts_code);

var br_code = std.loadFile("platform/berry_runtime.js");
std.evalScript(br_code);

// ─── 1. Host Profiling ──────────────────────────────────────────────────
print("=== Host Profiling ===");
print("");

var profile = HostProfile.profileHost();

print("OS:             " + profile.os);
print("Architecture:   " + profile.arch);
print("Cores:          " + profile.cores);
print("Memory:         " + (profile.memory / (1024*1024*1024)).toFixed(1) + " GB");
print("Page size:      " + profile.page_size + " bytes");
print("Cache line:     " + profile.cache.line + " bytes");
print("L1 data:        " + (profile.cache.l1d / 1024) + " KB");
print("L1 instr:       " + (profile.cache.l1i / 1024) + " KB");
print("L2:             " + (profile.cache.l2 / 1024) + " KB");
print("L3:             " + (profile.cache.l3 / (1024*1024)) + " MB");
print("Berry OS:       " + profile.is_berry);
print("");

// ─── 2. CPU Features ────────────────────────────────────────────────────
print("=== CPU Features ===");
print("Features:       " + profile.features.join(", "));
print("SIMD:           " + (profile.simd.has_simd ? profile.simd.simd_type : "none"));
print("SIMD width:     " + profile.simd.width_bytes + " bytes");
print("");

// ─── 3. Benchmarks ──────────────────────────────────────────────────────
print("=== Benchmarks ===");
print("CPU ops/sec:    " + (profile.benchmark.cpu_ops_sec / 1000000).toFixed(1) + " M");
print("Mem bandwidth:  " + (profile.benchmark.mem_bandwidth / (1024*1024*1024)).toFixed(1) + " GB/s");
print("");

// ─── 4. Compilation Recommendation ──────────────────────────────────────
print("=== Compilation Recommendation ===");
var rec = profile.recommendation;
print("Target:         " + rec.target);
print("Pipeline:       " + rec.pipeline);
print("Architecture:   " + rec.arch);
print("Opt level:      " + rec.opt_level);
print("SIMD:           " + (rec.use_simd ? rec.simd_type : "none"));
print("Rationale:");
for (var i = 0; i < rec.rationale.length; i++) {
    print("  - " + rec.rationale[i]);
}
print("");

// ─── 5. Runtime API Demo ────────────────────────────────────────────────
print("=== Runtime API Demo ===");
print("berry.getTarget():   " + berry.getTarget());
print("berry.getArch():     " + berry.getArch());
print("berry.getPipeline(): " + berry.getPipeline());
print("berry.getOptLevel(): " + berry.getOptLevel());
print("berry.getOS():       " + berry.getOS());
print("berry.isBerryOS():   " + berry.isBerryOS());
print("berry.getCores():    " + berry.getCores());
print("berry.supportsSIMD(): " + berry.supportsSIMD());
print("berry.getSIMDType(): " + berry.getSIMDType());
print("");

// ─── 6. Target Selection from Pragmas ───────────────────────────────────
print("=== Target Selection from Pragmas ===");

var test_sources = [
    "// @target guest\n// @arch arm64\nfunction arm64_guest() {}",
    "// @target host\n// @arch x86_64\n// @opt speed\nfunction fast_x64() {}",
    "// @target auto\n// @opt size\nfunction small_func() {}",
    "// @pipeline befb\n// @arch riscv64\nfunction riscv_func() {}",
];

for (var i = 0; i < test_sources.length; i++) {
    var cfg = TargetSelect.resolveTarget(test_sources[i], profile);
    print("Source " + (i+1) + ":");
    print("  target:   " + cfg.target);
    print("  arch:     " + cfg.arch);
    print("  pipeline: " + cfg.pipeline);
    print("  format:   " + cfg.format);
    print("");
}

// ─── 7. Architecture to BeASM Mapping ───────────────────────────────────
print("=== Architecture Mapping ===");
var archs = ["x86_64", "x86_32", "x86_16", "arm64", "arm32", "riscv32", "riscv64", "riscv128"];
for (var i = 0; i < archs.length; i++) {
    print(archs[i] + " → BeASM: " + TargetSelect.archToBeASM(archs[i]) + ", GAS: " + TargetSelect.archToGASFlag(archs[i]));
}
print("");

// ─── 8. Best Target Selection ───────────────────────────────────────────
print("=== Best Target Auto-Selection ===");
var best = berry.selectBestTarget();
print("Best target:   " + best.target);
print("Best pipeline: " + best.pipeline);
print("Best arch:     " + best.arch);
print("Best opt:      " + best.opt_level);
print("Best SIMD:     " + best.simd);
print("Rationale:");
for (var i = 0; i < best.rationale.length; i++) {
    print("  - " + best.rationale[i]);
}
print("");

print("=== Host-Guest Demo Complete ===");
