/*
 * mbjs-llvm.h — LLVM connector / codegen for MBJS (libmochiberry).
 *
 * MochiLLVM: a JIT + AOT tier that lowers MBJS bytecode to LLVM IR and uses
 * LLVM's native backends for execution. Unlike the CC backend (mbjs-jit),
 * this tier does NOT shell out to a compiler and does NOT require a C
 * toolchain at runtime:
 *
 *   - LLVM itself is loaded *dynamically* (dlopen / LoadLibrary) from
 *     libLLVM.so / libLLVM-*.dll / libLLVM.dylib. The engine never links
 *     against LLVM statically, so the core stays tiny, embeddable and
 *     bare-metal friendly; on hosts without LLVM the connector reports
 *     "unavailable" and the caller falls back to the interpreter — same
 *     contract as MBJS_JIT_BACKEND_NONE.
 *
 *   - mbjs_llvm_jit() lowers a JS source to LLVM IR, runs it through LLVM's
 *     MCJIT/ORC engine and returns a native function pointer with the same
 *     signature as MBJS_Call(). The generated code runs in-process with no
 *     dlopen of a temp .so and no temp files.
 *
 *   - mbjs_llvm_aot() emits an object file (.o) or assembly (.s) for the
 *     function. This is the AOT path: mbjsc -aot can then link the object
 *     into a final binary (MochiAOT), with the generated code being real
 *     native code, not interpreted bytecode.
 *
 *   - mbjs_llvm_emit_ir() returns the textual IR (for debugging / the
 *     "Syntax JS decompiled to C + Assembly" MochiAOT introspection).
 *
 * Codegen subset: the lowerer currently handles the core opcode set
 * (constants, stack, arithmetic, jumps, calls, simple variable access). Any
 * opcode it does not recognize is reported via mbjs_llvm_last_error() and
 * the caller falls back to the interpreter — never a wrong result.
 */

#ifndef MBJS_LLVM_H
#define MBJS_LLVM_H

#include <stddef.h>
#include <stdint.h>

#include "mbjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Same contract as MBJSJitFn (mbjs-jit.h). Declared locally so the LLVM
 * connector does not require the CC backend header; identical typedefs may be
 * redeclared in C11, so including both headers stays valid. */
#if !defined(MBJS_JIT_H) && !defined(MBJS_LLVM_JITFN_DEFINED)
#define MBJS_LLVM_JITFN_DEFINED
typedef MBJSValue (*MBJSJitFn)(MBJSContext *ctx, MBJSValue this_val,
                               int argc, MBJSValue *argv);
#endif

typedef enum MBJSLLvmBackend {
    MBJS_LLVM_BACKEND_AUTO = 0, /* pick native JIT when libLLVM loads        */
    MBJS_LLVM_BACKEND_JIT,      /* in-process MCJIT/ORC execution            */
    MBJS_LLVM_BACKEND_AOT,      /* emit object/assembly only (no execution)  */
} MBJSLLvmBackend;

typedef struct MBJSLLvm MBJSLLvm;

/* Load libLLVM and resolve the C API. Returns NULL (with a message available
 * through mbjs_llvm_last_error()) when LLVM is not installed. May be called
 * multiple times; the library is reference-counted. */
MBJSLLvm *mbjs_llvm_new(MBJSLLvmBackend backend);

void mbjs_llvm_free(MBJSLLvm *llvm);

/* "available", "jit", "aot" or "unavailable". */
const char *mbjs_llvm_backend_name(const MBJSLLvm *llvm);

/* Last error message (empty when the last call succeeded). */
const char *mbjs_llvm_last_error(const MBJSLLvm *llvm);

/* Enable/disable the "berry technique" optimization addon (assembler/register/
 * bytecode specializations). It is enabled by default and is typically faster
 * than -O3 for the generated code; disable it for debugging or when a host
 * assembler cannot handle the specialized forms (mbjsc --no-opt-addon). */
void mbjs_llvm_set_opt_addon(MBJSLLvm *llvm, int enabled);

/* Set/get the O0..O10 optimization ladder level (see mbjs-opt.h). */
void mbjs_llvm_set_opt_level(MBJSLLvm *llvm, int level);
int mbjs_llvm_get_opt_level(const MBJSLLvm *llvm);

/* --native: target the host CPU (LLVMGetHostCPUName/Features) instead of
 * the generic x86-64 baseline. 0 = generic (default). */
void mbjs_llvm_set_native(MBJSLLvm *llvm, int enabled);

/* Resolved CPU name after mbjs_llvm_set_native(1) ("generic" or a real CPU
 * like "haswell"); the features string is returned via `feat`. */
const char *mbjs_llvm_get_cpu(const MBJSLLvm *llvm, const char **feat);

/* --lto: run an LLVM ThinLTO pass pipeline over the module before code
 * emission (via LLVMRunPasses). 0 = off (default). */
void mbjs_llvm_set_lto(MBJSLLvm *llvm, int enabled);

/* JIT: lower `source` (an expression that evaluates to a callable) to LLVM IR
 * and run it natively. Returns the native function, or NULL on any failure
 * (check mbjs_llvm_last_error()). The pointer is owned by `llvm`. */
MBJSJitFn mbjs_llvm_jit(MBJSLLvm *llvm, MBJSContext *ctx,
                        const char *source, const char *name);

/* AOT: lower `source` and emit native object code to `out_path`
 * (".o" object or ".s" assembly, chosen by suffix). `symbol` is the exported
 * symbol name (default "mbjs_aot_entry"). Returns 0 on success. */
int mbjs_llvm_aot(MBJSLLvm *llvm, MBJSContext *ctx,
                  const char *source, const char *name,
                  const char *out_path, const char *symbol);

/* Emit the textual LLVM IR for `source` into a caller-owned buffer.
 * `size` is the buffer size in bytes; returns the needed size (>= size means
 * truncated) or -1 on failure. */
int mbjs_llvm_emit_ir(MBJSLLvm *llvm, MBJSContext *ctx,
                      const char *source, const char *name,
                      char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MBJS_LLVM_H */
