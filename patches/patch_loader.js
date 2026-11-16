/*
 * patch_loader.js — OTA patch loader for libmochiberry.
 *
 * Loads every patches/patch_*.js file in order, lets each patch install only
 * what the engine lacks, and prints a report of what was patched vs. already
 * native. This is the boot-time step that makes language features upgradable
 * over the air without recompiling the engine.
 *
 * Run (as a module, so the mbjs:os/std imports resolve):
 *   build/mbjs -m patches/patch_loader.js
 */

import * as os from "mbjs:os";
import * as std from "mbjs:std";

/* Patch ABI: patch files are plain scripts evaluated in global scope, so
 * they cannot use import statements. The loader hands them the engine's
 * system modules through these global slots (patches must only read them). */
globalThis.__mbjs_os = os;
globalThis.__mbjs_std = std;

const js_patch_dir = "patches";
const js_patch_prefix = "patch_";

/* This fork's os.readdir returns [entries, err]; plain-array engines return
 * the entries directly. Normalize both shapes. */
const js_scan = os.readdir(js_patch_dir);
const js_entries = Array.isArray(js_scan[0]) ? js_scan[0] : js_scan;
const js_files = js_entries
    .filter((f) => typeof f === "string" &&
        f.startsWith(js_patch_prefix) && f.endsWith(".js") &&
        f !== "patch_loader.js")
    .sort();

if (js_files.length === 0) {
    console.log("patch_loader: no patches found in " + js_patch_dir);
}

let js_loaded = 0;
let js_errors = 0;

for (const file of js_files) {
    const path = js_patch_dir + "/" + file;
    let code;
    try {
        code = std.loadFile(path);
    } catch (e) {
        console.log("patch_loader: cannot read " + path + ": " + e.message);
        js_errors++;
        continue;
    }
    try {
        /* Patches are plain scripts with their own top-level const/function
         * names. Running them through indirect eval would hoist those into
         * global lexical scope and collide between patches, so each patch is
         * wrapped in an IIFE. The patches still touch globalThis.* and
         * install features with Object.defineProperty, which is exactly the
         * surface they need. */
        (0, eval)("(function () {\n" + code + "\n})();");
        js_loaded++;
        console.log("patch_loader: loaded " + path);
    } catch (e) {
        /* The engine's stack strings start at the first "at <input>:line"
         * frame and drop the exception message, so a failed patch used to
         * print only frames. Print name+message first, then the stack. */
        const js_err_name = (e && e.name) ? e.name : "Error";
        const js_err_msg = (e && e.message !== undefined) ? e.message : String(e);
        console.log("patch_loader: FAILED " + path + ": " + js_err_name + ": " + js_err_msg);
        if (e && e.stack)
            console.log(e.stack);
        js_errors++;
    }
}

/* Report --------------------------------------------------------------- */

const js_report = globalThis.__mbjs_patches || {};
const js_entries_report = Object.keys(js_report).sort();
let js_patched = 0;
let js_native = 0;

console.log("\npatch_loader: feature report (" + js_entries_report.length + " tracked):");
for (const name of js_entries_report) {
    const how = js_report[name];
    const kind = how === "native" ? "native" : "patched";
    if (how === "native")
        js_native++;
    else
        js_patched++;
    console.log("  " + kind.padEnd(8) + " " + name + "  [" + how + "]");
}

console.log("\npatch_loader: " + js_loaded + " patch file(s) loaded, " +
    js_errors + " error(s), " + js_patched + " feature(s) patched, " +
    js_native + " already native.");
if (js_errors > 0)
    std.exit(1);
