#
# mbjs Javascript Engine
#
# Copyright (c) 2017-2026 Fabrice Bellard
# Copyright (c) 2017-2024 Charlie Gordon
# Copyright (c) 2023-2026 Ben Noordhuis
# Copyright (c) 2023-2026 Saúl Ibarra Corretgé
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

BUILD_DIR=build
BUILD_TYPE?=Release
INSTALL_PREFIX?=/usr/local

MBJS=$(BUILD_DIR)/mbjs
MBJSC=$(BUILD_DIR)/mbjsc
RUN262=$(BUILD_DIR)/run-test262

JOBS?=$(shell getconf _NPROCESSORS_ONLN)
ifeq ($(JOBS),)
JOBS := $(shell sysctl -n hw.ncpu)
endif
ifeq ($(JOBS),)
JOBS := $(shell nproc)
endif
ifeq ($(JOBS),)
JOBS := 4
endif

all: $(MBJS)

amalgam: TEMP := $(shell mktemp -d)
amalgam: $(MBJS)
	$(MBJS) amalgam.js $(TEMP)/mbjs-amalgam.c
	cp mbjs.h mbjs-version.h mbjs-libc.h $(TEMP)
	cd $(TEMP) && zip -9 mbjs-amalgam.zip mbjs-amalgam.c mbjs.h mbjs-version.h mbjs-libc.h
	cp $(TEMP)/mbjs-amalgam.zip $(BUILD_DIR)
	cd $(TEMP) && $(RM) mbjs-amalgam.zip mbjs-amalgam.c mbjs.h mbjs-libc.h
	$(RM) -d $(TEMP)

fuzz:
	clang -g -O1 -fsanitize=address,undefined,fuzzer -o fuzz fuzz.c
	./fuzz

$(BUILD_DIR):
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(INSTALL_PREFIX)

$(MBJS): $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j $(JOBS)

$(MBJSC): $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target mbjsc -j $(JOBS)

install: $(MBJS) $(MBJSC)
	cmake --build $(BUILD_DIR) --target install

clean:
	cmake --build $(BUILD_DIR) --target clean

codegen: $(MBJSC) $(MBJS)
	$(MBJSC) -ss -o gen/repl.c -m repl.js
	$(MBJSC) -b -ss -m -o gen/standalone.mbc standalone.js
	$(MBJS) scripts/emit_standalone_c.js
	$(MBJSC) -e -o gen/function_source.c tests/function_source.js
	$(MBJSC) -e -o gen/hello.c examples/hello.js
	$(MBJSC) -e -o gen/hello_module.c -m examples/hello_module.js
	$(MBJSC) -e -o gen/test_fib.c -m examples/test_fib.js
	$(MBJSC) -C -ss -o builtin-array-fromasync.h builtin-array-fromasync.js
	$(MBJSC) -C -ss -o builtin-iterator-zip.h builtin-iterator-zip.js
	$(MBJSC) -C -ss -o builtin-iterator-zip-keyed.h builtin-iterator-zip-keyed.js

debug:
	BUILD_TYPE=Debug $(MAKE)

distclean:
	@rm -rf $(BUILD_DIR)

stats: $(MBJS)
	$(MBJS) -qd

jscheck: CFLAGS=-I. -D_GNU_SOURCE -DMBJS_CHECK_JSVALUE -Wall -Werror -fsyntax-only -c -o /dev/null
jscheck:
	$(CC) $(CFLAGS) api-test.c
	$(CC) $(CFLAGS) ctest.c
	$(CC) $(CFLAGS) fuzz.c
	$(CC) $(CFLAGS) gen/function_source.c
	$(CC) $(CFLAGS) gen/hello.c
	$(CC) $(CFLAGS) gen/hello_module.c
	$(CC) $(CFLAGS) gen/repl.c
	$(CC) $(CFLAGS) gen/standalone.c
	$(CC) $(CFLAGS) gen/test_fib.c
	$(CC) $(CFLAGS) mbjs.c
	$(CC) $(CFLAGS) mbjs-cli.c
	$(CC) $(CFLAGS) mbjsc.c
	$(CC) $(CFLAGS) mbjs-libc.c
	$(CC) $(CFLAGS) run-test262.c

# effectively .PHONY because it doesn't generate output
ctest: CFLAGS=-std=c11 -fsyntax-only -Wall -Wextra -Werror -pedantic
ctest: ctest.c mbjs.h
	$(CC) $(CFLAGS) -DMBJS_NAN_BOXING=0 $<
	$(CC) $(CFLAGS) -DMBJS_NAN_BOXING=1 $<

# effectively .PHONY because it doesn't generate output
cxxtest: CXXFLAGS=-std=c++11 -fsyntax-only -Wall -Wextra -Werror -pedantic
cxxtest: cxxtest.cc mbjs.h
	$(CXX) $(CXXFLAGS) -DMBJS_NAN_BOXING=0 $<
	$(CXX) $(CXXFLAGS) -DMBJS_NAN_BOXING=1 $<

test: $(MBJS)
	$(RUN262) -c tests.conf

test262: $(MBJS)
	$(RUN262) -m -c test262.conf -a

test262-fast: $(MBJS)
	$(RUN262) -m -c test262.conf -c test262-fast.conf -a

test262-update: $(MBJS)
	$(RUN262) -u -c test262.conf -a -t 1

test262-check: $(MBJS)
	$(RUN262) -m -c test262.conf -E -a

purify:
	sh scripts/purify.sh --check

purify-fix:
	sh scripts/purify.sh --fix

purity-test: purify

patchtest: $(MBJS)
	$(MBJS) -m tests/test_patches.js

# TGPM (Takegawa package manager): native launcher + JS regression tests.
takegawa: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target takegawa -j $(JOBS)

takegawatest: $(MBJS)
	$(MBJS) -m tests/test_takegawa.js

# V8-syntax parity battery: modern syntax + runtime surface vs V8/Node.
v8test: $(MBJS)
	$(MBJS) -m tests/test_v8syntax.js

memtest: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target test-mem -j $(JOBS)
	$(BUILD_DIR)/test-mem

jittest: $(BUILD_DIR) libmochiberry
	cmake --build $(BUILD_DIR) --target test-jit -j $(JOBS)
	$(BUILD_DIR)/test-jit

libmochiberry:
	sh scripts/build-libmochiberry.sh

# AOT: compile the demo app to a native executable (Bun/Clang style)
native: libmochiberry
	sh scripts/build-native.sh examples/app_mochiberry.js dist/mochiberry-native --static

nativetest: native
	dist/mochiberry-native

cross:
	sh scripts/cross-build.sh

# Transpilers: JS -> freestanding C, JS -> inline assembly (gas/llvm),
# JS -> Berry assembler (.basm for beasm).
transpile: libmochiberry
	sh scripts/transpile-c.sh examples/app_mochiberry.js dist/transpile/app.c --build
	dist/transpile/app-native
	sh scripts/transpile-asm.sh examples/app_mochiberry.js dist/transpile/app_asm.S --build
	dist/transpile/app_asm-native

basm: libmochiberry
	sh scripts/transpile-basm.sh examples/app_mochiberry.js dist/transpile/app.basm

# Sunat GC build: the garbage collector is circumcised out of the runtime;
# every allocation lives in one deterministic mbjs-mem arena (bump, no
# per-object free) that is reclaimed in bulk at teardown. Artifacts in
# dist/sunat/. The sunat library must never be linked together with the
# normal libmochiberry (same MBJS_* symbols, different memory semantics).
sunat: libmochiberry
	sh scripts/build-sunat.sh

sunattest: sunat
	@mkdir -p $(BUILD_DIR)
	$(CC) -O2 -DMBJS_SUNAT_GC -I. -Idist/sunat tests/test_sunat.c \
		-Ldist/sunat -lmochiberry -Wl,-rpath,'$(CURDIR)/dist/sunat' \
		-o $(BUILD_DIR)/test-sunat -lm
	$(BUILD_DIR)/test-sunat
	dist/sunat/mbjs-sunat -e 'var a={}; a.self=a; var s=""; for (var i=0;i<5000;i++) s+=i; "ok len="+s.length+" cycle-ok="+a.self.self.self.self.n===undefined ? "cycle" : "alive"'
	dist/sunat/mbjs-sunat --embed-arena -e '2+3*4'

# Build the Berry assembler (beasm/beasm.c) as a host tool.
beasm:
	@mkdir -p build
	$(CC) -O2 -o build/beasm beasm/beasm.c
	@echo "beasm: build/beasm ready"

# MBJS -> BeASM -> Berry OS native binary (app bytecode + runtime bridge,
# assembled by beasm, verified on the host against libmochiberry).
berry: libmochiberry beasm
	sh scripts/build-berry.sh examples/app_mochiberry.js dist/berry/app_mochiberry
	sh scripts/build-berry.sh examples/aot_demo.js dist/berry/aot_demo
	sh scripts/build-berry.sh examples/bench_fib.js dist/berry/bench_fib

# 16-bit (x86_16) freestanding check: mbjs-mem.c must compile in real-mode
# 16-bit mode (gcc -m16, 386+ real mode) with no libc references, like the
# baremetal-check above but for the 16-bit target. Gracefully SKIPs when the
# host compiler has no -m16 (needs 32-bit multilib: apt-get install gcc-multilib).
x86_16-check:
	@mkdir -p $(BUILD_DIR)
	@if printf '#include <string.h>\nint f(void){return 0;}\n' | $(CC) -m16 -march=i386 -x c -c - -o $(BUILD_DIR)/m16probe.o 2>/dev/null; then \
		echo "16-bit toolchain OK (-m16)"; \
		$(CC) -m16 -march=i386 -DMBJS_MEM_NO_LIBC -ffreestanding -nostdlib -I. -c mbjs-mem.c -o $(BUILD_DIR)/mbjs-mem-x16.o && \
		if nm $(BUILD_DIR)/mbjs-mem-x16.o | grep -E ' U (malloc|free|realloc|calloc|memcpy|memset|memmove|str)'; then echo "FAIL: libc refs in 16-bit mbjs-mem.o"; exit 1; fi; \
		echo "PASS: mbjs-mem.o (-m16, MBJS_MEM_NO_LIBC) has zero libc references"; \
		$(CC) -m16 -march=i386 -DMBJS_MEM_NO_LIBC -ffreestanding -nostdlib -I. -Iplatform/baremetal -c platform/baremetal/mbjs_baremetal.c -o $(BUILD_DIR)/mbjs-baremetal-x16.o && \
		echo "PASS: baremetal adapter compiles freestanding in -m16 real mode"; \
		echo "== x86_16-check OK =="; \
	else \
		echo "SKIP: host cc has no usable -m16 (install gcc-multilib or ia16-elf-gcc)"; \
	fi

# Verify the memory foundation and bare-metal adapter compile with no libc
# (region-backed arena, freestanding flags) and emit no libc symbols.
baremetal-check:
	@mkdir -p $(BUILD_DIR)
	$(CC) -DMBJS_MEM_NO_LIBC -ffreestanding -nostdlib -I. -c mbjs-mem.c -o $(BUILD_DIR)/mbjs-mem-bare.o
	@if nm $(BUILD_DIR)/mbjs-mem-bare.o | grep -E ' U (malloc|free|realloc|calloc|memcpy|memset|memmove|str)'; then \
		echo "FAIL: libc references found in mbjs-mem.o"; exit 1; \
	fi
	@echo "PASS: mbjs-mem.o (MBJS_MEM_NO_LIBC) has zero libc references"
	$(CC) -DMBJS_MEM_NO_LIBC -ffreestanding -nostdlib -I. -Iplatform/baremetal -c platform/baremetal/mbjs_baremetal.c -o $(BUILD_DIR)/mbjs-baremetal.o
	$(CC) -Wall -Wextra -I. -Iplatform/baremetal -fsyntax-only platform/baremetal/mbjs_baremetal.c
	@echo "PASS: baremetal adapter compiles freestanding + with -Wall -Wextra"
	@echo "== baremetal-check OK =="

ota: libmochiberry
	sh scripts/make-ota.sh

microbench: $(MBJS)
	$(MBJS) tests/microbench.js

# Three-way engine benchmark: interpreter vs JIT vs AOT native.
bench: libmochiberry
	sh scripts/bench.sh

unicode_gen: $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target unicode_gen

libunicode-table.h: unicode_gen
	$(BUILD_DIR)/unicode_gen unicode $@

.PHONY: all amalgam ctest cxxtest debug fuzz jscheck install clean codegen distclean stats test test262 test262-update test262-check purify purify-fix purity-test patchtest v8test memtest jittest libmochiberry ota native nativetest cross baremetal-check x86_16-check baremetal transpile basm beasm berry bench microbench unicode_gen takegawa takegawatest $(MBJS) $(MBJSC)
