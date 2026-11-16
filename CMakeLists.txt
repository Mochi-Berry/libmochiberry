cmake_minimum_required(VERSION 3.10)

project(mbjs LANGUAGES C)

include(CheckCCompilerFlag)
include(GNUInstallDirs)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)

macro(xoption OPTION_NAME OPTION_TEXT OPTION_DEFAULT)
    option(${OPTION_NAME} ${OPTION_TEXT} ${OPTION_DEFAULT})
    if(DEFINED ENV{${OPTION_NAME}})
        # Allow setting the option through an environment variable.
        set(${OPTION_NAME} $ENV{${OPTION_NAME}})
    endif()
    if(${OPTION_NAME})
        add_definitions(-D${OPTION_NAME})
    endif()
    message(STATUS "  ${OPTION_NAME}: ${${OPTION_NAME}}")
endmacro()

# note: MBJS_ENABLE_TSAN is currently incompatible with the other sanitizers but we
# don't explicitly check for that because who knows what the future will bring?
# MBJS_ENABLE_MSAN only works with clang at the time of writing; also not checked
# for the same reason
xoption(BUILD_SHARED_LIBS "Build a shared library" OFF)
# if we ever require CMake 3.21, we can automatically detect this setting by looking at PROJECT_IS_TOP_LEVEL
# similar to how glslang does
xoption(MBJS_ENABLE_INSTALL "Enable mbjs installation" ON)
xoption(MBJS_BUILD_WERROR "Build with -Werror" OFF)
xoption(MBJS_BUILD_EXAMPLES "Build examples" OFF)
xoption(MBJS_BUILD_CLI_STATIC "Build a static mbjs executable" OFF)
xoption(MBJS_BUILD_CLI_WITH_MIMALLOC "Build the mbjs executable with mimalloc" OFF)
xoption(MBJS_BUILD_CLI_WITH_STATIC_MIMALLOC "Build the mbjs executable with mimalloc (statically linked)" OFF)
xoption(MBJS_DISABLE_PARSER "Disable JS source code parser" OFF)
xoption(MBJS_ENABLE_ASAN "Enable AddressSanitizer (ASan)" OFF)
xoption(MBJS_ENABLE_MSAN "Enable MemorySanitizer (MSan)" OFF)
xoption(MBJS_ENABLE_TSAN "Enable ThreadSanitizer (TSan)" OFF)
xoption(MBJS_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer (UBSan)" OFF)
xoption(MBJS_ENABLE_GC_STRESS "Force a full GC before every allocation (GC stress test)" OFF)

# Used to properly define MBJS_LIBC_EXTERN.
add_compile_definitions(MBJS_NG_BUILD)

# MINGW doesn't exist in older cmake versions, newer versions don't know
# about CMAKE_COMPILER_IS_MINGW, and there is no unique CMAKE_C_COMPILER_ID
# for mingw-based compilers...
if(MINGW)
    # do nothing
elseif(CMAKE_C_COMPILER MATCHES "mingw")
    set(MINGW TRUE)
else()
    set(MINGW FALSE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
    set(TVOS TRUE)
else()
    set(TVOS FALSE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "watchOS")
    set(WATCHOS TRUE)
else()
    set(WATCHOS FALSE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "SunOS")
    set(SUNOS TRUE)
else()
    set(SUNOS FALSE)
endif()

if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "No build type selected, default to Release")
    set(CMAKE_BUILD_TYPE "Release")
endif()

message(STATUS "Building in ${CMAKE_BUILD_TYPE} mode")
message(STATUS "Building with ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION} on ${CMAKE_SYSTEM}")

macro(xcheck_add_c_compiler_flag FLAG)
    string(REPLACE "-" "" FLAG_NO_HYPHEN ${FLAG})
    check_c_compiler_flag(${FLAG} COMPILER_SUPPORTS_${FLAG_NO_HYPHEN})
    if(COMPILER_SUPPORTS_${FLAG_NO_HYPHEN})
        add_compile_options(${FLAG})
    endif()
endmacro()

xcheck_add_c_compiler_flag(-Wall)
if(NOT MSVC AND NOT IOS AND NOT TVOS AND NOT WATCHOS)
    if(MBJS_BUILD_WERROR)
        xcheck_add_c_compiler_flag(-Werror)
    endif()
    xcheck_add_c_compiler_flag(-Wextra)
endif()
xcheck_add_c_compiler_flag(-Wformat=2)
xcheck_add_c_compiler_flag(-Wno-implicit-fallthrough)
xcheck_add_c_compiler_flag(-Wno-sign-compare)
xcheck_add_c_compiler_flag(-Wno-missing-field-initializers)
xcheck_add_c_compiler_flag(-Wno-unused-parameter)
xcheck_add_c_compiler_flag(-Wno-unused-but-set-variable)
xcheck_add_c_compiler_flag(-Wno-unused-result)
xcheck_add_c_compiler_flag(-Wno-stringop-truncation)
xcheck_add_c_compiler_flag(-Wno-array-bounds)
if(NOT SUNOS)
xcheck_add_c_compiler_flag(-funsigned-char)
endif()

# Clang on Windows without MSVC command line fails because the codebase uses
# functions like strcpy over strcpy_s
if(CMAKE_C_COMPILER_ID STREQUAL "Clang" AND WIN32 AND NOT MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE)
endif()

# ClangCL is command line compatible with MSVC, so 'MSVC' is set.
if(MSVC)
    xcheck_add_c_compiler_flag(-Wno-unsafe-buffer-usage)
    xcheck_add_c_compiler_flag(-Wno-sign-conversion)
    xcheck_add_c_compiler_flag(-Wno-nonportable-system-include-path)
    xcheck_add_c_compiler_flag(-Wno-implicit-int-conversion)
    xcheck_add_c_compiler_flag(-Wno-shorten-64-to-32)
    xcheck_add_c_compiler_flag(-Wno-reserved-macro-identifier)
    xcheck_add_c_compiler_flag(-Wno-reserved-identifier)
    xcheck_add_c_compiler_flag(-Wdeprecated-declarations)
    xcheck_add_c_compiler_flag(/experimental:c11atomics)
    xcheck_add_c_compiler_flag(/wd4018) # -Wno-sign-conversion
    xcheck_add_c_compiler_flag(/wd4061) # -Wno-implicit-fallthrough
    xcheck_add_c_compiler_flag(/wd4100) # -Wno-unused-parameter
    xcheck_add_c_compiler_flag(/wd4200) # -Wno-zero-length-array
    xcheck_add_c_compiler_flag(/wd4242) # -Wno-shorten-64-to-32
    xcheck_add_c_compiler_flag(/wd4244) # -Wno-shorten-64-to-32
    xcheck_add_c_compiler_flag(/wd4245) # -Wno-sign-compare
    xcheck_add_c_compiler_flag(/wd4267) # -Wno-shorten-64-to-32
    xcheck_add_c_compiler_flag(/wd4388) # -Wno-sign-compare
    xcheck_add_c_compiler_flag(/wd4389) # -Wno-sign-compare
    xcheck_add_c_compiler_flag(/wd4456) # Hides previous local declaration
    xcheck_add_c_compiler_flag(/wd4457) # Hides function parameter
    xcheck_add_c_compiler_flag(/wd4710) # Function not inlined
    xcheck_add_c_compiler_flag(/wd4711) # Function was inlined
    xcheck_add_c_compiler_flag(/wd4820) # Padding added after construct
    xcheck_add_c_compiler_flag(/wd4996) # -Wdeprecated-declarations
    xcheck_add_c_compiler_flag(/wd5045) # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
endif()

# Set a 8MB default stack size on Windows.
# It defaults to 1MB on MSVC, which is the same as our current JS stack size,
# so it will overflow and crash otherwise.
# On MinGW it defaults to 2MB.
if(WIN32)
    if(MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /STACK:8388608")
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang" AND NOT MINGW)
        # Clang frontend (clang.exe) targeting the MSVC ABI (using lld-link)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Xlinker /STACK:8388608")
    else()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--stack,8388608")
    endif()
endif()

# MacOS and GCC 11 or later need -Wno-maybe-uninitialized
# https://github.com/mbjs-ng/mbjs/issues/453
if(APPLE AND CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 11)
    xcheck_add_c_compiler_flag(-Wno-maybe-uninitialized)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    add_compile_definitions(
        _WASI_EMULATED_PROCESS_CLOCKS
        _WASI_EMULATED_SIGNAL
    )
    add_link_options(
        -lwasi-emulated-process-clocks
        -lwasi-emulated-signal
    )
endif()

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    xcheck_add_c_compiler_flag(/Od)
    xcheck_add_c_compiler_flag(-O0)
    xcheck_add_c_compiler_flag(-ggdb)
    xcheck_add_c_compiler_flag(-fno-omit-frame-pointer)
endif()

if(BUILD_SHARED_LIBS)
    message(STATUS "Building a shared library")
endif()

if(MBJS_ENABLE_ASAN)
message(STATUS "Building with ASan")
add_compile_options(
    -fsanitize=address
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=address
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()

if(MBJS_ENABLE_MSAN)
message(STATUS "Building with MSan")
add_compile_options(
    -fsanitize=memory
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=memory
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()

if(MBJS_ENABLE_TSAN)
message(STATUS "Building with TSan")
add_compile_options(
    -fsanitize=thread
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=thread
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()

if(MBJS_ENABLE_UBSAN)
message(STATUS "Building with UBSan")
add_compile_options(
    -fsanitize=undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
add_link_options(
    -fsanitize=undefined
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer
)
endif()

if(MBJS_ENABLE_GC_STRESS)
message(STATUS "Building with GC stress (FORCE_GC_AT_MALLOC)")
add_compile_definitions(FORCE_GC_AT_MALLOC)
endif()


# mbjs library
#

xoption(MBJS_BUILD_LIBC "Build standard library modules as part of the library" OFF)
macro(add_mbjs_libc_if_needed target)
    if(NOT MBJS_BUILD_LIBC)
        target_link_libraries(${target} PRIVATE mbjs-libc)
    endif()
endmacro()
macro(add_static_if_needed target)
    if(MBJS_BUILD_CLI_STATIC OR MINGW)
        target_link_options(${target} PRIVATE -static)
        if(MINGW)
            target_link_options(${target} PRIVATE -static-libgcc)
        endif()
    endif()
endmacro()

set(mbjs_sources
    dtoa.c
    libregexp.c
    libunicode.c
    mbjs.c
    mbjs-mem.c
    mbjs-jit.c
    mbjs-llvm.c
    mbjs-mulch.c
    mbjs-superset.c
)

if(MBJS_BUILD_LIBC)
    list(APPEND mbjs_sources mbjs-libc.c)
    # The definition must be added to the entire project.
    add_compile_definitions(MBJS_BUILD_LIBC)
endif()
list(APPEND mbjs_defines _GNU_SOURCE)
if(WIN32)
    # NB: Windows 7 is EOL and we are only supporting in so far as it doesn't interfere with progress.
    list(APPEND mbjs_defines WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
if(TVOS)
    list(APPEND mbjs_defines _TVOS)
endif()
if(WATCHOS)
    list(APPEND mbjs_defines _WATCHOS)
endif()
list(APPEND mbjs_libs ${CMAKE_DL_LIBS})
find_package(Threads)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    list(APPEND mbjs_libs ${CMAKE_THREAD_LIBS_INIT})
endif()

# try to find libm
find_library(M_LIBRARIES m)
if(M_LIBRARIES OR CMAKE_C_COMPILER_ID STREQUAL "TinyCC")
    list(APPEND mbjs_libs m)
endif()

add_library(mbjs ${mbjs_sources})
target_compile_definitions(mbjs PRIVATE ${mbjs_defines})
target_include_directories(mbjs PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(mbjs PUBLIC ${mbjs_libs})

# Pass a compiler definition so that Windows gets its declspec's right.
get_target_property(MBJS_LIB_TYPE mbjs TYPE)
if(MBJS_LIB_TYPE STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(mbjs
        PRIVATE BUILDING_MBJS_SHARED
        PUBLIC  USING_MBJS_SHARED
    )
endif()

# An interface library for modules.
add_library(mbjs_module_lib INTERFACE)
target_include_directories(mbjs_module_lib INTERFACE
    $<TARGET_PROPERTY:mbjs,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(mbjs_module_lib INTERFACE
    MBJS_NG_MODULE_BUILD
    $<TARGET_PROPERTY:mbjs,INTERFACE_COMPILE_DEFINITIONS>
)
if(WIN32)
    # Since Windows cannot resolve symbols at load time, we need to
    # explicitly link it to mbjs.
    target_link_libraries(mbjs_module_lib INTERFACE
        mbjs
    )
endif()

if(NOT MBJS_BUILD_LIBC)
    add_library(mbjs-libc STATIC mbjs-libc.c)
    target_compile_definitions(mbjs-libc PRIVATE ${mbjs_defines})
    target_link_libraries(mbjs-libc PRIVATE ${mbjs_libs} mbjs)
endif()

if(EMSCRIPTEN)
    add_executable(mbjs_wasm ${mbjs_sources})
    target_link_options(mbjs_wasm PRIVATE
        # in emscripten 3.x, this will be set to 16k which is too small for mbjs. #write sth. to force github rebuild
        -sSTACK_SIZE=2097152 # let it be 2m = 2 * 1024 * 1024 = 2097152, otherwise, stack overflow may be occured at bootstrap
        -sNO_INVOKE_RUN
        -sNO_EXIT_RUNTIME
        -sMODULARIZE # do not mess the global
        -sEXPORT_ES6 # export js file to morden es module
        -sEXPORT_NAME=getQuickJs # give a name
        -sTEXTDECODER=1 # it will be 2 if we use -Oz, and that will cause js -> c string convertion fail
        -sNO_DEFAULT_TO_CXX # this project is pure c project, no need for c plus plus handle
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap
    )
    target_compile_definitions(mbjs_wasm PRIVATE ${mbjs_defines})
    target_link_libraries(mbjs_wasm PRIVATE m)
endif()


# mbjs bytecode compiler
#

add_executable(mbjsc
    mbjsc.c
)
add_mbjs_libc_if_needed(mbjsc)
add_static_if_needed(mbjsc)
target_compile_definitions(mbjsc PRIVATE ${mbjs_defines})
target_link_libraries(mbjsc PRIVATE mbjs)


# TGPM (Takegawa package manager) native launcher
#

add_executable(takegawa
    takegawa.c
)
add_mbjs_libc_if_needed(takegawa)
add_static_if_needed(takegawa)
target_compile_definitions(takegawa PRIVATE ${mbjs_defines})
target_link_libraries(takegawa PRIVATE mbjs)


# mbjs CLI
#

add_executable(mbjs_exe
    mbjs-repl.c
    gen/standalone.c
    mbjs-cli.c
)
add_mbjs_libc_if_needed(mbjs_exe)
add_static_if_needed(mbjs_exe)
set_target_properties(mbjs_exe PROPERTIES
    OUTPUT_NAME "mbjs"
    PDB_NAME "mbjs_cli"
    COMPILE_PDB_NAME "mbjs_cli"
)
target_compile_definitions(mbjs_exe PRIVATE ${mbjs_defines})
target_link_libraries(mbjs_exe PRIVATE mbjs)
if (NOT WIN32)
    set_target_properties(mbjs_exe PROPERTIES ENABLE_EXPORTS TRUE)
endif()

# WASI Reactor
#

if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    option(MBJS_WASI_REACTOR "Build WASI reactor (exports library functions, no _start)" OFF)
    if(MBJS_WASI_REACTOR)
        add_executable(mbjs_wasi
            mbjs-libc.c
            mbjs-wasi-reactor.c
        )
        set_target_properties(mbjs_wasi PROPERTIES
            OUTPUT_NAME "mbjs"
            SUFFIX ".wasm"
        )
        target_compile_definitions(mbjs_wasi PRIVATE ${mbjs_defines})
        target_link_libraries(mbjs_wasi mbjs)
        target_link_options(mbjs_wasi PRIVATE
            -mexec-model=reactor
            # Export all symbols with default visibility (MBJS_EXTERN functions)
            -Wl,--export-dynamic
            # Memory management (libc symbols need explicit export)
            -Wl,--export=malloc
            -Wl,--export=free
            -Wl,--export=realloc
            -Wl,--export=calloc
        )
    endif()
endif()

if(MBJS_BUILD_CLI_WITH_MIMALLOC OR MBJS_BUILD_CLI_WITH_STATIC_MIMALLOC)
    find_package(mimalloc REQUIRED)
    target_compile_definitions(mbjs_exe PRIVATE MBJS_USE_MIMALLOC)
    # Upstream mimalloc doesn't provide a way to know if both libraries are supported.
    if(MBJS_BUILD_CLI_WITH_STATIC_MIMALLOC)
        target_link_libraries(mbjs_exe PRIVATE mimalloc-static)
    else()
        target_link_libraries(mbjs_exe PRIVATE mimalloc)
    endif()
endif()

# Test262 runner
#

if(NOT EMSCRIPTEN)
    add_executable(run-test262
        run-test262.c
    )
    add_mbjs_libc_if_needed(run-test262)
    target_compile_definitions(run-test262 PRIVATE ${mbjs_defines})
    target_link_libraries(run-test262 PRIVATE mbjs)
endif()

# Interrupt test
#

add_executable(api-test
    api-test.c
)
target_compile_definitions(api-test PRIVATE ${mbjs_defines})
target_link_libraries(api-test PRIVATE mbjs)

add_executable(lre-test
    lre-test.c
    libregexp.c
    libunicode.c
)
target_compile_definitions(lre-test PRIVATE ${mbjs_defines})

# Non-GC memory management foundation tests
#

add_executable(test-mem
    tests/test_mem.c
    mbjs-mem.c
)
target_include_directories(test-mem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test-mem PRIVATE ${mbjs_defines})
target_link_libraries(test-mem PRIVATE mbjs)

# libmulchberry (mbjs-mulch) 4-mode memory manager tests

add_executable(test-mulch
    tests/test_mulch.c
    mbjs-mulch.c
)
target_include_directories(test-mulch PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test-mulch PRIVATE ${mbjs_defines})
target_link_libraries(test-mulch PRIVATE mbjs)

# MochiRoutine (fiber-based, cooperative, zero-pause green threads) tests

add_executable(test-routine
    tests/test_routine.c
    mbjs-routine.c
)
target_include_directories(test-routine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test-routine PRIVATE ${mbjs_defines})
target_link_libraries(test-routine PRIVATE mbjs)

# Takegawa TGPM superset registry tests

add_executable(test-superset
    tests/test_superset.c
)
target_include_directories(test-superset PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(test-superset PRIVATE ${mbjs_defines})
target_link_libraries(test-superset PRIVATE mbjs)

# JIT foundation tests
#
# test-jit links the shared engine (dist/libmochiberry.so) so the process
# contains exactly one copy of the engine: the host and the JIT shim (.so
# compiled at runtime by mbjs-jit) resolve MBJS_* from the same library.
# `make jittest` builds libmochiberry first.

add_executable(test-jit
    tests/test_jit.c
    mbjs-jit.c
)
target_include_directories(test-jit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/dist/include)
target_compile_definitions(test-jit PRIVATE ${mbjs_defines})
target_link_libraries(test-jit PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dist/libmochiberry.so ${CMAKE_DL_LIBS})
set_target_properties(test-jit PROPERTIES
    BUILD_RPATH ${CMAKE_CURRENT_SOURCE_DIR}/dist)

# Unicode generator
#

# Note: do not link libunicode.c here, it needs the generated libunicode-table.h,
# which would make it impossible to regenerate the table from scratch.
add_executable(unicode_gen EXCLUDE_FROM_ALL
    unicode_gen.c
)
target_compile_definitions(unicode_gen PRIVATE ${mbjs_defines})

add_executable(function_source
    gen/function_source.c
)
add_mbjs_libc_if_needed(function_source)
target_include_directories(function_source PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(function_source PRIVATE ${mbjs_defines})
target_link_libraries(function_source PRIVATE mbjs)

# Examples
#

if(MBJS_BUILD_EXAMPLES)
    if(WIN32 AND NOT (MBJS_LIB_TYPE STREQUAL "SHARED_LIBRARY"))
        message(AUTHOR_WARNING
            "Binary modules used with static mbjs on Windows. There might be\n"
            "runtime errors when the module is used due to two copies of mbjs\n"
            "in memory.\n"
            "Please use \"-DBUILD_SHARED_LIBS=true\" if possible."
        )
    endif()

    add_executable(hello
        gen/hello.c
    )
    add_mbjs_libc_if_needed(hello)
    target_include_directories(hello PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(hello PRIVATE ${mbjs_defines})
    target_link_libraries(hello PRIVATE mbjs)

    add_executable(hello_module
        gen/hello_module.c
    )
    add_mbjs_libc_if_needed(hello_module)
    target_include_directories(hello_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(hello_module PRIVATE ${mbjs_defines})
    target_link_libraries(hello_module PRIVATE mbjs)

    add_library(fib MODULE examples/fib.c)
    set_target_properties(fib PROPERTIES
        PREFIX ""
    )
    target_link_libraries(fib PRIVATE mbjs_module_lib)
    if(APPLE)
        target_link_options(fib PRIVATE -undefined dynamic_lookup)
    endif()

    add_library(point MODULE examples/point.c)
    set_target_properties(point PROPERTIES
        PREFIX ""
    )
    target_link_libraries(point PRIVATE mbjs_module_lib)
    if(APPLE)
        target_link_options(point PRIVATE -undefined dynamic_lookup)
    endif()

    add_executable(test_fib
        gen/test_fib.c
    )
    add_mbjs_libc_if_needed(test_fib)
    target_include_directories(test_fib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_fib PRIVATE ${mbjs_defines})
    target_link_libraries(test_fib PRIVATE mbjs)
    # Loads fib.so which depends on symbols from libmbjs.
    set_target_properties(test_fib PROPERTIES ENABLE_EXPORTS TRUE)
endif()

# Install target
#

if (MBJS_ENABLE_INSTALL)
    file(STRINGS mbjs-version.h mbjs_h REGEX MBJS_VERSION)
    string(REGEX MATCH "MBJS_VERSION_MAJOR ([0-9]*)" _ "${mbjs_h}")
    set(MBJS_VERSION_MAJOR ${CMAKE_MATCH_1})
    string(REGEX MATCH "MBJS_VERSION_MINOR ([0-9]*)" _ "${mbjs_h}")
    set(MBJS_VERSION_MINOR ${CMAKE_MATCH_1})
    string(REGEX MATCH "MBJS_VERSION_PATCH ([0-9]*)" _ "${mbjs_h}")
    set(MBJS_VERSION_PATCH ${CMAKE_MATCH_1})
    set_target_properties(mbjs PROPERTIES
        VERSION ${MBJS_VERSION_MAJOR}.${MBJS_VERSION_MINOR}.${MBJS_VERSION_PATCH}
        SOVERSION ${MBJS_VERSION_MAJOR}
    )
    install(FILES mbjs.h mbjs-version.h mbjs-mem.h mbjs-jit.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    if(MBJS_BUILD_LIBC)
        install(FILES mbjs-libc.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
    if(NOT IOS AND NOT TVOS AND NOT WATCHOS)
        install(TARGETS mbjs_exe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(TARGETS mbjsc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(TARGETS takegawa RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
        install(FILES takegawa.js DESTINATION ${CMAKE_INSTALL_DATADIR}/takegawa)
        install(FILES mbjs.man DESTINATION ${CMAKE_INSTALL_DATADIR}/man/man1 RENAME mbjs.1)
        install(FILES mbjsc.man DESTINATION ${CMAKE_INSTALL_DATADIR}/man/man1 RENAME mbjsc.1)
        install(FILES takegawa.man DESTINATION ${CMAKE_INSTALL_DATADIR}/man/man1 RENAME takegawa.1)
    endif()
    install(TARGETS mbjs EXPORT mbjsConfig
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT mbjsConfig DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mbjs)
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY examples DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()
