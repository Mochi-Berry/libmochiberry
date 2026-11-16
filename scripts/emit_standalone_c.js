// Emit gen/standalone.c from gen/standalone.mbc (raw module bytecode).
// Run: mbjs scripts/emit_standalone_c.js
import * as std from "mbjs:std";

const inPath = "gen/standalone.mbc";
const outPath = "gen/standalone.c";
const name = "mbjsc_standalone";

const bytes = std.loadFile(inPath, { binary: true });
if (!bytes) throw new Error("cannot read " + inPath);

let hex = "";
for (let i = 0; i < bytes.length; i++) {
  hex += " 0x" + (bytes[i] < 16 ? "0" : "") + bytes[i].toString(16) + ",";
  if ((i + 1) % 12 === 0) hex += "\n";
}

const out =
  "/* File generated automatically by scripts/emit_standalone_c.js from " + inPath + " */\n" +
  "#include <stdint.h>\n\n" +
  "const uint32_t " + name + "_size = " + bytes.length + ";\n\n" +
  "const uint8_t " + name + "[" + bytes.length + "] = {\n" + hex + "\n};\n";

std.writeFile(outPath, out);
console.log("wrote " + outPath + " (" + bytes.length + " bytes)");
