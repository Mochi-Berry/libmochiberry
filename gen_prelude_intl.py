import os
patches = ["patch_intl.js","patch_ecma2025.js","patch_ecma2026.js","patch_awaitdict.js","patch_temporal_full.js"]
# Path relatif: drive F: sudah hilang, repo kini di E:/bcf/mochiberry
HERE = os.path.dirname(os.path.abspath(__file__))
base = os.path.join(HERE, "patches")
parts = ["// prelude\n",
 'globalThis.__mbjs_patches = globalThis.__mbjs_patches || {};\n',
 'globalThis.js_registry = globalThis.__mbjs_patches;\n',
 'globalThis.js_record = function(name, how){ globalThis.__mbjs_patches[name] = how; };\n',
 'globalThis.__mbjs_prelude = {loaded:0, failed:[]};\n']
for p in patches:
    code = open(os.path.join(base,p),encoding="utf-8").read()
    parts.append("\n/* %s */\n" % p)
    parts.append('try { (function(){\n'); parts.append(code); parts.append('\n})(); globalThis.__mbjs_prelude.loaded++; }\n')
    parts.append('catch(e) { globalThis.__mbjs_prelude.failed.push(%r + ": " + (e && e.message)); }\n' % p)
open(os.path.join(HERE,"test262_prelude_intl.js"),"w",encoding="utf-8").write("".join(parts))
print("prelude written")
