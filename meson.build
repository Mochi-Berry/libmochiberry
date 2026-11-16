project(
  'libmochiberry',
  'c',
  version: '0.0.3_alpha',
  default_options: [
    'c_std=gnu11,c11',
    'warning_level=3',
    'buildtype=release',
    'b_ndebug=if-release',
  ],
  license: 'MIT',
  license_files: 'LICENSE',
  meson_version: '>=1.3.0',
)

host_system = host_machine.system()
cc = meson.get_compiler('c')

add_project_arguments('-DMBJS_NG_BUILD', language: 'c')

mbjs_gcc_warning_args = [
  '-Wno-unsafe-buffer-usage',
  '-Wno-sign-conversion',
  '-Wno-nonportable-system-include-path',
  '-Wno-implicit-int-conversion',
  '-Wno-shorten-64-to-32',
  '-Wno-reserved-macro-identifier',
  '-Wno-reserved-identifier',
  '-Wdeprecated-declarations',

  '-Wno-implicit-fallthrough',
  '-Wno-sign-compare',
  '-Wno-missing-field-initializers',
  '-Wno-unused-parameter',
  '-Wno-unused-but-set-variable',
  '-Wno-array-bounds',
  '-Wno-format-truncation',
]
mbjs_gcc_args = []

if host_system != 'sunos'
  mbjs_gcc_args += '-funsigned-char'
endif

if host_system == 'darwin'
  # https://github.com/mbjs-ng/mbjs/issues/453
  mbjs_gcc_warning_args += '-Wno-maybe-uninitialized'
endif

# https://github.com/microsoft/cpp-docs/tree/main/docs/error-messages/compiler-warnings
mbjs_msvc_warning_args = [
  '/wd4018', # -Wno-sign-conversion
  '/wd4061', # -Wno-implicit-fallthrough
  '/wd4100', # -Wno-unused-parameter
  '/wd4200', # -Wno-zero-length-array
  '/wd4242', # -Wno-shorten-64-to-32
  '/wd4244', # -Wno-shorten-64-to-32
  '/wd4245', # -Wno-sign-compare
  '/wd4267', # -Wno-shorten-64-to-32
  '/wd4388', # -Wno-sign-compare
  '/wd4389', # -Wno-sign-compare
  '/wd4710', # Function not inlined
  '/wd4711', # Function was inlined
  '/wd4820', # Padding added after construct
  '/wd4996', # -Wdeprecated-declarations
  '/wd5045', # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
]
mbjs_msvc_args = [
  '/experimental:c11atomics',
  '/J', # -funsigned-char
]

if cc.get_argument_syntax() == 'msvc'
  add_project_arguments(
    cc.get_supported_arguments(mbjs_msvc_warning_args),
    cc.get_id().contains('clang') ? cc.get_supported_arguments(mbjs_gcc_warning_args) : [],
    mbjs_msvc_args,
    language: 'c',
  )
else
  add_project_arguments(
    cc.get_supported_arguments(mbjs_gcc_warning_args),
    mbjs_gcc_args,
    language: 'c',
  )
endif

if host_system == 'windows'
  # Set a 8MB default stack size on Windows.
  # It defaults to 1MB on MSVC, which is the same as our current JS stack size,
  # so it will overflow and crash otherwise.
  # On MinGW it defaults to 2MB.
  stack_size = 8 * 1024 * 1024
  if cc.get_argument_syntax() == 'msvc'
    add_project_link_arguments(f'/STACK:@stack_size@', language: 'c')
  else
    add_project_link_arguments(f'-Wl,--stack,@stack_size@', language: 'c')
  endif
endif

if meson.is_cross_build()
  native_cc = meson.get_compiler('c', native: true)

  if native_cc.get_argument_syntax() == 'msvc'
    # https://github.com/microsoft/cpp-docs/tree/main/docs/error-messages/compiler-warnings
    add_project_arguments(
      native_cc.get_supported_arguments(mbjs_msvc_warning_args),
      native_cc.get_id().contains('clang') ? native_cc.get_supported_arguments(mbjs_gcc_warning_args) : [],
      mbjs_msvc_args,

      language: 'c',
      native: true,
    )
  else
    add_project_arguments(
      native_cc.get_supported_arguments(mbjs_gcc_warning_args),
      mbjs_gcc_args,

      language: 'c',
      native: true,
    )
  endif
endif
if get_option('debug')
  add_project_arguments(
    cc.get_supported_arguments('-fno-omit-frame-pointer'),
    language: 'c',
  )
endif

mbjs_sys_deps = []

m_dep = cc.find_library('m', required: false)
mbjs_sys_deps += m_dep
mbjs_sys_deps += dependency('threads', required: false)
mbjs_sys_deps += dependency('dl', required: false)

mbjs_srcs = files(
  'dtoa.c',
  'libregexp.c',
  'libunicode.c',
  'mbjs.c',
)
mbjs_hdrs = files(
  'mbjs.h',
  'mbjs-version.h',
)

mbjs_libc = get_option('libc')
mbjs_libc_srcs = files('mbjs-libc.c')
mbjs_libc_hdrs = files('mbjs-libc.h')

if mbjs_libc
  mbjs_hdrs += mbjs_libc_hdrs
  add_project_arguments(
    '-DMBJS_BUILD_LIBC',
    language: 'c'
  )
endif

mbjs_parser = get_option('parser')

mbjs_c_args = ['-D_GNU_SOURCE']

if host_system == 'windows'
  mbjs_c_args += ['-DWIN32_LEAN_AND_MEAN', '-D_WIN32_WINNT=0x0601']
endif

if not mbjs_parser
  mbjs_c_args += ['-DMBJS_DISABLE_PARSER']
endif

mbjs_lib = library(
  'mbjs',
  mbjs_srcs,
  mbjs_libc ? mbjs_libc_srcs : [],

  dependencies: mbjs_sys_deps,
  c_args: mbjs_c_args,
  c_shared_args: ['-DBUILDING_MBJS_SHARED'],
  gnu_symbol_visibility: 'hidden',

  install: true,
  version: meson.project_version(),
)

mbjs_export_variables = [
  f'have_parser=@mbjs_parser@'
]

# This note is only of interest on Windows.
# Be conservative here: only pass -DUSING_MBJS_SHARED iff default_library ==
# 'shared', thus mbjs is guaranteed to be a DLL. Not passing -DUSING_MBJS_SHARED if
# mbjs is a DLL is not harmful, but passing the definition for DLL mbjs is desired.
# Conversely, if -DUSING_MBJS_SHARED is passed for static mbjs, the consumers
# will not compile.
mbjs_dep_args = get_option('default_library') == 'shared' ? ['-DUSING_MBJS_SHARED'] : []

mbjs_dep = declare_dependency(
  compile_args: mbjs_dep_args,
  link_with: mbjs_lib,
  dependencies: mbjs_sys_deps,
  include_directories: include_directories('.'),
  variables: mbjs_export_variables,
)

if host_system != 'windows'
  mbjs_module_dep = declare_dependency(
    compile_args: [mbjs_dep_args, '-DMBJS_NG_MODULE_BUILD'],
    include_directories: include_directories('.'),
  )
else
  mbjs_module_dep = declare_dependency(
    compile_args: [mbjs_dep_args, '-DMBJS_NG_MODULE_BUILD'],
    include_directories: include_directories('.'),
    # Windows can't resolve symbols at runtime thus we explicitly link to mbjs.
    dependencies: mbjs_dep,
  )
endif

if mbjs_libc
  mbjs_libc_dep = declare_dependency(
    include_directories: include_directories('.'),
    dependencies: mbjs_dep,
  )
else
  mbjs_libc_lib = static_library(
    'mbjs-libc',
    mbjs_libc_srcs,

    c_args: mbjs_c_args,
    dependencies: [mbjs_sys_deps, mbjs_dep],
    gnu_symbol_visibility: 'hidden',
  )
  mbjs_libc_dep = declare_dependency(
    link_with: mbjs_libc_lib,
    # Pick up mbjs-libc.h.
    include_directories: include_directories('.'),
  )
endif

if host_system == 'emscripten'
  mbjs_wasm_export_name = 'getQuickJs'
  executable(
    'mbjs_wasm',
    mbjs_srcs,
    link_args: cc.get_supported_link_arguments(
      # in emscripten 3.x, this will be set to 16k which is too small for mbjs.
      '-sSTACK_SIZE=@0@'.format(2 * 1024 * 1024), # let it be 2m = 2 * 1024 * 1024, otherwise, stack overflow may be occured at bootstrap
      '-sNO_INVOKE_RUN',
      '-sNO_EXIT_RUNTIME',
      '-sMODULARIZE', # do not mess the global
      '-sEXPORT_ES6', # export js file to morden es module
      '-sEXPORT_NAME=@0@'.format(mbjs_wasm_export_name), # give a name
      '-sTEXTDECODER=1', # it will be 2 if we use -Oz, and that will cause js -> c string convertion fail
      '-sNO_DEFAULT_TO_CXX', # this project is pure c project, no need for c plus plus handle
      '-sEXPORTED_RUNTIME_METHODS=ccall,cwrap',
    ),
    dependencies: m_dep,
    c_args: mbjs_c_args,
  )
endif

install_headers(mbjs_hdrs, subdir: 'mbjs-ng')

if not meson.is_subproject()
  docdir = get_option('docdir')
  datadir = get_option('datadir')

  if docdir == ''
    docdir = datadir / 'doc' / meson.project_name()
  endif
  install_data(
    'LICENSE',
    install_dir: docdir,

    install_tag: 'doc',
  )
  install_subdir(
    'examples',
    install_dir: docdir,

    strip_directory: false,
    install_tag: 'doc',
  )
endif

meson.override_dependency('mbjs-ng', mbjs_dep)

import('pkgconfig').generate(
  mbjs_lib,
  subdirs: 'mbjs-ng',
  name: 'mbjs-ng',
  description: 'mbjs, the Next Generation: a mighty JavaScript engine',
  url: 'https://github.com/mbjs-ng/mbjs',
  version: meson.project_version(),
  variables: mbjs_export_variables,
  # Export -DUSING_MBJS_SHARED, if needed, in the pkgconfig file.
  extra_cflags: mbjs_dep_args,
)

if not mbjs_parser
  subdir_done()
endif

# mbjs bytecode compiler
mbjsc_srcs = files(
  'mbjsc.c',
)
mbjsc_exe = executable(
  'mbjsc',
  mbjsc_srcs,

  c_args: mbjs_c_args,
  dependencies: [mbjs_dep, mbjs_libc_dep],
  install: true,
)

mimalloc_dep = []
mimalloc_sys_dep = dependency('mimalloc', required: get_option('cli_mimalloc'))
if mimalloc_sys_dep.found()
  mimalloc_dep = declare_dependency(
    dependencies: mimalloc_sys_dep,
    compile_args: '-DMBJS_USE_MIMALLOC',
  )
endif

# mbjs CLI
mbjs_exe_srcs = files(
  'gen/repl.c',
  'gen/standalone.c',
  'mbjs-cli.c',
)
mbjs_exe = executable(
  'mbjs',
  mbjs_exe_srcs,

  c_args: mbjs_c_args,
  dependencies: [mbjs_dep, mbjs_libc_dep, mimalloc_dep],
  export_dynamic: true,

  install: true,
)

if meson.is_cross_build()
  mimalloc_native_dep = []
  mimalloc_sys_native_dep = dependency('mimalloc', required: get_option('cli_mimalloc'), native: true)
  if mimalloc_sys_dep.found()
    mimalloc_native_dep = declare_dependency(
      dependencies: mimalloc_sys_native_dep,
      compile_args: '-DMBJS_USE_MIMALLOC',
    )
  endif

  mbjs_sys_native_deps = [
    native_cc.find_library('m', required: false),
    dependency('threads', required: false, native: true),
    dependency('dl', required: false, native: true),
  ]
  mbjs_native_lib = static_library(
    'mbjs_native',
    mbjs_srcs,
    mbjs_libc_srcs,

    dependencies: mbjs_sys_native_deps,
    c_args: mbjs_c_args,
    gnu_symbol_visibility: 'hidden',

    build_by_default: false,
    native: true,
    install: false,
  )

  meson.override_find_program(
    'mbjsc',
    executable(
      'mbjsc_native',
      mbjsc_srcs,

      c_args: mbjs_c_args,
      link_with: mbjs_native_lib,
      dependencies: mbjs_sys_native_deps,

      build_by_default: false,
      native: true,
      install: false,
    ),
  )
  meson.override_find_program(
    'mbjs',
    executable(
      'mbjs_native',
      mbjs_exe_srcs,

      c_args: mbjs_c_args,
      link_with: mbjs_native_lib,
      dependencies: [mbjs_sys_native_deps, mimalloc_native_dep],
      export_dynamic: true,

      build_by_default: false,
      native: true,
      install: false,
    ),
  )
else
  meson.override_find_program('mbjsc', mbjsc_exe)
  meson.override_find_program('mbjs', mbjs_exe)
endif

tests = get_option('tests').disable_auto_if(meson.is_subproject())
examples = get_option('examples').disable_auto_if(meson.is_subproject())

if tests.allowed()
  if host_system != 'emscripten'
    # Test262 runner
    run262_exe = executable(
      'run-test262',
      'run-test262.c',

      c_args: mbjs_c_args,
      dependencies: [mbjs_dep, mbjs_libc_dep]
    )

    test(
      'test',
      run262_exe,
      args: ['-c', files('tests.conf')],
      workdir: meson.current_source_dir(),
    )

    foreach bench : [
      'empty_loop',

      'date_now',

      'prop_read',
      'prop_write',
      'prop_create',
      'prop_delete',

      'array_read',
      'array_write',
      'array_prop_create',
      'array_length_decr',
      'array_hole_length_decr',
      'array_push',
      'array_pop',

      'typed_array_read',
      'typed_array_write',

      'global_read',
      'global_write',
      'global_write_strict',

      'local_destruct',
      'global_destruct',
      'global_destruct_strict',

      'func_call',
      'closure_var',

      'int_arith',
      'float_arith',

      'map_set',
      'map_delete',
      'weak_map_set',
      'weak_map_delete',

      'array_for',
      'array_for_in',
      'array_for_of',

      'math_min',

      'object_null',

      'regexp_ascii',
      'regexp_utf16',

      'string_build1',
      'string_build2',

      'string_slice1',
      'string_slice2',
      'string_slice3',

      'sort_bench',

      'int_to_string',
      'int_toString',

      'float_to_string',
      'float_toString',
      'float_toFixed',
      'float_toPrecision',
      'float_toExponential',

      'string_to_int',
      'string_to_float',

      'bigint64_arith',
      'bigint256_arith',
    ]
      benchmark(
        bench,
        mbjs_exe,
        args: [files('tests/microbench.js'), bench],
      )
    endforeach
  endif

  # API test
  test(
    'api',
    executable(
      'api-test',
      'api-test.c',

      c_args: mbjs_c_args,
      dependencies: mbjs_dep,
      build_by_default: false,
    ),
  )

  # LRE test
  test(
    'lre',
    executable(
      'lre-test',
      'lre-test.c',
      'libregexp.c',
      'libunicode.c',

      c_args: mbjs_c_args,
      build_by_default: false,
    ),
  )

  # Function.toString() test
  test(
    'function_source',
    executable(
      'function_source',
      'gen/function_source.c',

      c_args: mbjs_c_args,
      dependencies: [mbjs_dep, mbjs_libc_dep],
      build_by_default: false,
    ),
  )
endif

# Unicode generator
unicode_gen = executable(
  'unicode_gen',
  'unicode_gen.c',

  c_args: mbjs_c_args,
  build_by_default: false,
)

run_target(
  'libunicode-table.h',
  command: [
    unicode_gen,
    meson.current_source_dir() / 'unicode',
    files('libunicode-table.h'),
  ],
)

# bytecode to c source code for builtin and examples
alias_target('codegen',
  run_target(
    'codegen_repl.c',
    command: [
      mbjsc_exe,
      '-ss',
      '-o', files('gen/repl.c'),
      '-m',
      '-n', 'repl.js',
      files('repl.js'),
    ],
  ),
  run_target(
    'codegen_standalone.c',
    command: [
      mbjsc_exe,
      '-ss',
      '-o', files('gen/standalone.c'),
      '-m',
      '-n', 'standalone.js',
      files('standalone.js'),
    ],
  ),
  run_target(
    'codegen_function_source.c',
    command: [
      mbjsc_exe,
      '-e',
      '-o', files('gen/function_source.c'),
      '-n', 'tests/function_source.js',
      files('tests/function_source.js'),
    ],
  ),
  run_target(
    'codegen_hello.c',
    command: [
      mbjsc_exe,
      '-e',
      '-o', files('gen/hello.c'),
      '-n', 'examples/hello.js',
      files('examples/hello.js'),
    ],
  ),
  run_target(
    'codegen_hello_module.c',
    command: [
      mbjsc_exe,
      '-e',
      '-o', files('gen/hello_module.c'),
      '-m',
      '-n', 'examples/hello_module.js',
      files('examples/hello_module.js'),
    ],
  ),
  run_target(
    'codegen_test_fib.c',
    command: [
      mbjsc_exe,
      '-e',
      '-o', files('gen/test_fib.c'),
      '-m',
      '-n', 'examples/test_fib.js',
      files('examples/test_fib.js'),
    ],
  ),
  run_target(
    'codegen_builtin-array-fromasync.h',
    command: [
      mbjsc_exe,
      '-C',
      '-ss',
      '-o', files('builtin-array-fromasync.h'),
      '-n', 'builtin-array-fromasync.js',
      files('builtin-array-fromasync.js'),
    ],
  ),
)

if examples.allowed()
  if host_system == 'windows' and get_option('default_library') != 'shared'
    warning('Binary modules used with static mbjs on Windows.')
    warning(
      'There might be runtime errors when the modules are used due to',
      'two copies of mbjs in memory.'
    )
    warning('Please use "-Ddefault_library=shared" if possible.')
  endif
  executable(
    'hello',
    'gen/hello.c',

    c_args: mbjs_c_args,
    dependencies: [mbjs_dep, mbjs_libc_dep]
  )

  executable(
    'hello_module',
    'gen/hello_module.c',

    c_args: mbjs_c_args,
    dependencies: [mbjs_dep, mbjs_libc_dep]
  )

  subdir('examples')

  executable(
    'test_fib',
    'gen/test_fib.c',

    c_args: mbjs_c_args,
    dependencies: [mbjs_dep, mbjs_libc_dep],
    export_dynamic: true,
  )
endif
