# Host-side checks for Project Kaos-Byte4.1.0v (The Static Rift). The plugin itself is built with the Xbox 360 XDK.
CXX ?= g++
CXXFLAGS ?= -std=c++98 -Wall -Wextra -Werror -pedantic -O1 -g
CLANGXX ?= clang++

all: run

test_portal: test_portal.cpp ../src/core/portal_core.cpp ../src/core/portal_core.h ../src/core/sp_lock.h
	$(CXX) $(CXXFLAGS) -o $@ test_portal.cpp ../src/core/portal_core.cpp -lpthread

# The phone remote's HTTP helpers (request parsing, which files it hands out, JSON).
test_remote: test_remote.cpp ../src/core/remote_http.cpp ../src/core/remote_http.h
	$(CXX) $(CXXFLAGS) -o $@ test_remote.cpp ../src/core/remote_http.cpp

# Nicknames and lists (nicknames.ini, lists.ini; what's typed for them).
test_collections: test_collections.cpp ../src/core/collections.cpp ../src/core/collections.h
	$(CXX) $(CXXFLAGS) -o $@ test_collections.cpp ../src/core/collections.cpp

# config.cpp built natively against the XDK stubs (with host shims in test_config.cpp).
test_config: test_config.cpp ../src/xbox/config.cpp ../src/xbox/config.h
	$(CXX) -std=c++98 -Wall -Wno-unused-parameter -D_XBOX -D__cdecl= -Ixdk_stubs -I../src/xbox -o $@ test_config.cpp ../src/xbox/config.cpp

# The figure library (order by type, sections, My Picks) against a small pack it writes; host
# shims for the file APIs are in test_figures.cpp.  ./test_figures <The Static Rift> reports on
# a real pack too.
test_figures: test_figures.cpp ../src/xbox/figures.cpp ../src/xbox/figures.h ../src/xbox/elements.cpp
	$(CXX) -std=c++98 -Wall -Wno-unused-parameter -D_XBOX -D__cdecl= -Ixdk_stubs -I../src/xbox -o $@ \
	  test_figures.cpp ../src/xbox/figures.cpp ../src/xbox/elements.cpp

# Import patcher + execution-ID reader (util.cpp) against a fake title import table.
# --gc-sections drops the util.cpp functions that need real XDK file APIs.
test_patch: test_patch.cpp ../src/xbox/util.cpp ../src/xbox/util.h
	$(CXX) -std=c++98 -Wall -Wno-unused-parameter -D_XBOX -D__cdecl= -Ixdk_stubs -I../src/xbox \
	  -ffunction-sections -fdata-sections -Wl,--gc-sections -o $@ test_patch.cpp ../src/xbox/util.cpp

# The open toolchain's freestanding libc/printf (xrt/), built for the host with every symbol
# renamed xrt_* so the test can compare it against the host's C library in the same program.
test_xrt: test_xrt.cpp ../xrt/xrt_libc.cpp ../xrt/xrt_printf.cpp
	$(CXX) -std=c++11 -O1 -fno-builtin -ffreestanding -I../xrt/include -c ../xrt/xrt_libc.cpp -o xrt_libc.host.o
	$(CXX) -std=c++11 -O1 -fno-builtin -ffreestanding -I../xrt/include -c ../xrt/xrt_printf.cpp -o xrt_printf.host.o
	objcopy --prefix-symbols=xrt_ xrt_libc.host.o
	objcopy --prefix-symbols=xrt_ --redefine-sym xrt__ZdlPv=_ZdlPv xrt_printf.host.o
	$(CXX) -std=c++11 -O1 -fno-builtin -Wall -o $@ test_xrt.cpp xrt_libc.host.o xrt_printf.host.o

# Every plugin source, compiled (no link) for 32-bit big-endian PowerPC like the Xbox 360,
# against stub XDK/xkelib headers. Catches C++ errors without the XDK; it is not a real build.
# Checked twice: the Dashlaunch plugin and the Peer Pressure sideload DLL (-DSKYPORTAL_SIDELOAD).
syntax:
	@for flavour in "" "-DSKYPORTAL_SIDELOAD" "-DSKYPORTAL_SIDELOAD -DSKYPORTAL_MENU_POP" \
	    "-DSKYPORTAL_SIDELOAD -DSKYPORTAL_MENU_DRAWN" "-DSKYPORTAL_MENU_DRAWN"; do \
	  for f in ../src/xbox/*.cpp ../src/core/portal_core.cpp ../src/core/remote_http.cpp ../src/core/collections.cpp; do \
	    echo "  syntax $$f $$flavour"; \
	    $(CLANGXX) --target=powerpc-unknown-none -std=c++98 -nostdlibinc -fsyntax-only -Wall -Wextra \
	      -Wno-unused-parameter -D_XBOX -D__cdecl= $$flavour -Ixdk_stubs/libc -Ixdk_stubs -I../src/xbox $$f || exit 1; \
	  done; \
	done

run: test_portal test_remote test_collections test_config test_figures test_patch test_xrt syntax
	./test_portal
	./test_remote
	./test_collections
	./test_config
	./test_figures
	./test_patch
	./test_xrt

# Optional: runs the built open-toolchain DLL end to end on an emulated PowerPC 970 (Unicorn).
# Needs Unicorn's Python binding: UNICORN_PY=<unicorn>/bindings/python UNICORN_LIB=<dir of libunicorn.so.2>
emu:
	$(MAKE) -C ../xrt all-variants
	PYTHONPATH=$(UNICORN_PY) LIBUNICORN_PATH=$(UNICORN_LIB) python3 emu/run_dll.py \
	  ../xrt/build/classic/KaosByte-classic.xex ../xrt/build/classic/KaosByte-classic.elf classic
	PYTHONPATH=$(UNICORN_PY) LIBUNICORN_PATH=$(UNICORN_LIB) python3 emu/run_dll.py \
	  ../xrt/build/pop/KaosByte-pop.xex ../xrt/build/pop/KaosByte-pop.elf pop
	PYTHONPATH=$(UNICORN_PY) LIBUNICORN_PATH=$(UNICORN_LIB) python3 emu/run_dll.py \
	  ../xrt/build/drawn/KaosByte.xex ../xrt/build/drawn/KaosByte.elf drawn
	PYTHONPATH=$(UNICORN_PY) LIBUNICORN_PATH=$(UNICORN_LIB) python3 emu/run_dll.py \
	  ../xrt/build/rgh/KaosByteDSL.xex ../xrt/build/rgh/KaosByteDSL.elf rgh

# The phone remote end to end: the Peer Pressure build on the emulated CPU with XAM's sockets on
# real ones, and a phone-sized headless Chromium (node + playwright) using its page.
# FOLDER = a full The Static Rift folder (figures, UI, pictures).
FOLDER ?= ../build/package/The Static Rift
emu-remote:
	$(MAKE) -C ../xrt VARIANT=drawn
	PYTHONPATH=$(UNICORN_PY) LIBUNICORN_PATH=$(UNICORN_LIB) python3 emu/run_remote.py \
	  ../xrt/build/drawn/KaosByte.xex ../xrt/build/drawn/KaosByte.elf "$(FOLDER)"

# The same on the Dashlaunch build (RGH / JTAG / BadUpdate): booted on the dashboard, the server
# starting with the game, then the game exiting and starting again.
emu-remote-rgh:
	$(MAKE) -C ../xrt VARIANT=rgh
	PYTHONPATH=$(UNICORN_PY) LIBUNICORN_PATH=$(UNICORN_LIB) python3 emu/run_remote.py \
	  ../xrt/build/rgh/KaosByteDSL.xex ../xrt/build/rgh/KaosByteDSL.elf "$(FOLDER)"

# The skydrawn menu rendered on the PC (same code as the console) into PPM previews.
# ASSETS = the SkyPortal folder tools/mkassets.py wrote (UI\ and Images\).
ASSETS ?= ../build/package/SkyPortal
preview: overlay_preview.cpp ../src/xbox/overlay_render.cpp ../src/xbox/elements.cpp ../src/xbox/qoi.cpp
	$(CXX) -std=c++11 -O2 -I../src/xbox -o overlay_preview overlay_preview.cpp \
	  ../src/xbox/overlay_render.cpp ../src/xbox/elements.cpp ../src/xbox/qoi.cpp
	./overlay_preview $(ASSETS) preview

clean:
	rm -f test_figures test_portal test_remote test_collections test_config test_patch test_xrt overlay_preview preview_*.ppm *.host.o test_config.tmp.ini

.PHONY: all run syntax emu emu-remote emu-remote-rgh clean
