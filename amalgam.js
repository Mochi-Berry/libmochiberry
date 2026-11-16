import {loadFile, writeFile} from "mbjs:std"

const cutils_h = loadFile("cutils.h")
const dtoa_c = loadFile("dtoa.c")
const dtoa_h = loadFile("dtoa.h")
const libregexp_c = loadFile("libregexp.c")
const libregexp_h = loadFile("libregexp.h")
const libregexp_opcode_h = loadFile("libregexp-opcode.h")
const libunicode_c = loadFile("libunicode.c")
const libunicode_h = loadFile("libunicode.h")
const libunicode_table_h = loadFile("libunicode-table.h")
const list_h = loadFile("list.h")
const mbjs_atom_h = loadFile("mbjs-atom.h")
const mbjs_c = loadFile("mbjs.c")
const mbjs_c_atomics_h = loadFile("mbjs-c-atomics.h")
const mbjs_h = loadFile("mbjs.h")
const mbjs_version_h = loadFile("mbjs-version.h")
const mbjs_libc_c = loadFile("mbjs-libc.c")
const mbjs_libc_h = loadFile("mbjs-libc.h")
const mbjs_opcode_h = loadFile("mbjs-opcode.h")
const gen_builtin_array_fromasync_h = loadFile("builtin-array-fromasync.h")
const gen_builtin_iterator_zip_h = loadFile("builtin-iterator-zip.h")
const gen_builtin_iterator_zip_keyed_h = loadFile("builtin-iterator-zip-keyed.h")

let source = "#if defined(MBJS_BUILD_LIBC) && defined(__linux__) && !defined(_GNU_SOURCE)\n"
           + "#define _GNU_SOURCE\n"
           + "#endif\n"
           + mbjs_c_atomics_h
           + cutils_h
           + dtoa_h
           + list_h
           + libunicode_h // exports lre_is_id_start, used by libregexp.h
           + libregexp_h
           + libunicode_table_h
           + mbjs_version_h
           + mbjs_h
           + mbjs_c
           + dtoa_c
           + libregexp_c
           + libunicode_c
           + "#ifdef MBJS_BUILD_LIBC\n"
           + mbjs_libc_h
           + mbjs_libc_c
           + "#endif // MBJS_BUILD_LIBC\n"
source = source.replace(/#include "mbjs-atom.h"/g, mbjs_atom_h)
source = source.replace(/#include "mbjs-opcode.h"/g, mbjs_opcode_h)
source = source.replace(/#include "libregexp-opcode.h"/g, libregexp_opcode_h)
source = source.replace(/#include "builtin-array-fromasync.h"/g,
                        gen_builtin_array_fromasync_h)
source = source.replace(/#include "builtin-iterator-zip.h"/g,
                        gen_builtin_iterator_zip_h)
source = source.replace(/#include "builtin-iterator-zip-keyed.h"/g,
                        gen_builtin_iterator_zip_keyed_h)
source = source.replace(/#include "[^"]+"/g, "")
writeFile(execArgv[2] ?? "mbjs-amalgam.c", source)
