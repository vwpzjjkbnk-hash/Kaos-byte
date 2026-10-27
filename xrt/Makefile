# Project Kaos-Byte2.0v (The Static Rift) open toolchain: builds the Peer Pressure title DLL with clang + ld.lld + Python,
# no Xbox 360 XDK. Needs clang/lld with the PowerPC target (LLVM 14+) and python3.
#
#   make -C xrt                  -> xrt/build/classic/Kaos-Byte2.0v-classic.xex (menu: notifications)
#   make -C xrt VARIANT=pop      -> xrt/build/pop/Kaos-Byte2.0v-pop.xex (console pop-up menu)
#   make -C xrt VARIANT=drawn    -> xrt/build/drawn/Kaos-Byte2.0v.xex (menu drawn on screen)
#   make -C xrt VARIANT=rgh      -> xrt/build/rgh/Kaos-Byte2.0v-RGH.xex (Dashlaunch plugin for
#                                   RGH / JTAG / BadUpdate + XeUnshackle; drawn menu)
#   make -C xrt all-variants
#   make -C xrt BASE=0x8C000000   (if the default base clashes with a game)

CLANG   ?= clang
LLD     ?= ld.lld
PYTHON  ?= python3
OBJDUMP ?= llvm-objdump
VARIANT ?= classic
# The Peer Pressure title DLL loads next to the game (title images start at 0x82000000); the
# Dashlaunch system DLL in the system plugins' range (another plugin at the same address makes
# Dashlaunch skip one of them: then build with another 1 MB-aligned BASE).
ifeq ($(VARIANT),rgh)
  BASE ?= 0x91C80000
else
  BASE ?= 0x8A000000
endif
# Peer Pressure build: a title DLL, devkit-encrypted and signed like every module Peer Pressure
# loads. Dashlaunch build: a system DLL, plain (RGH / XeUnshackle kernels load unsigned code).
LOAD_DEFS := -DSKYPORTAL_SIDELOAD
XEX_FLAGS := --dll --encrypt-devkit --sign-devkit
SIGN_CHECK = $(PYTHON) xexsign.py $@

ifeq ($(VARIANT),rgh)
  MENU_DEFS := -DSKYPORTAL_MENU_DRAWN
  NAME := Kaos-Byte2.0v-RGH
  LOAD_DEFS :=
  XEX_FLAGS := --dll --sysdll --page-4k
  SIGN_CHECK = @true
else ifeq ($(VARIANT),pop)
  MENU_DEFS := -DSKYPORTAL_MENU_POP
  NAME := Kaos-Byte2.0v-pop
else ifeq ($(VARIANT),drawn)
  MENU_DEFS := -DSKYPORTAL_MENU_DRAWN
  NAME := Kaos-Byte2.0v
else
  MENU_DEFS :=
  NAME := Kaos-Byte2.0v-classic
endif

BUILD := build/$(VARIANT)
SRC   := ../src

# 32-bit big-endian PowerPC, SysV. -msoft-float/-mno-altivec keep the code off the FPRs and
# vector registers entirely (the console's code keeps values in f14-f31/v14-v31; the plugin never
# touches them, so nothing needs saving). -fshort-wchar: the console's wchar_t is 16 bits.
TARGET_FLAGS := --target=powerpc-unknown-linux-gnu -mcpu=ppc -msoft-float -mno-altivec
CXXFLAGS := $(TARGET_FLAGS) -std=c++11 -O2 -ffreestanding -fno-builtin -nostdlibinc \
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector -fno-pic \
  -fshort-wchar -fno-optimize-sibling-calls -ffunction-sections -fdata-sections \
  -Wall -Wno-unused-parameter -Wno-unused-function -Wno-unused-private-field \
  -D_XBOX -DNDEBUG $(LOAD_DEFS) -DSKYPORTAL_XRT $(MENU_DEFS) \
  -Iinclude -I$(SRC)/core -I$(SRC)/xbox
ASFLAGS := $(TARGET_FLAGS)

PLUGIN_SRCS := $(SRC)/core/portal_core.cpp $(SRC)/xbox/config.cpp $(SRC)/xbox/figures.cpp \
  $(SRC)/xbox/hooks.cpp $(SRC)/xbox/main.cpp $(SRC)/xbox/util.cpp $(SRC)/xbox/elements.cpp \
  $(SRC)/xbox/menu_pop.cpp $(SRC)/xbox/overlay.cpp $(SRC)/xbox/overlay_render.cpp \
  $(SRC)/xbox/qoi.cpp
XRT_SRCS := xrt.cpp xrt_entry.cpp xrt_libc.cpp xrt_printf.cpp

OBJS := $(patsubst $(SRC)/%.cpp,$(BUILD)/plugin/%.o,$(PLUGIN_SRCS)) \
  $(patsubst %.cpp,$(BUILD)/xrt/%.o,$(XRT_SRCS)) \
  $(BUILD)/xrt/entry.o $(BUILD)/xrt/imports.o

all: $(BUILD)/$(NAME).xex

all-variants:
	$(MAKE) VARIANT=classic
	$(MAKE) VARIANT=pop
	$(MAKE) VARIANT=drawn
	$(MAKE) VARIANT=rgh

# Headers are listed wholesale: a header change rebuilds everything (the build takes seconds).
HEADERS := $(wildcard $(SRC)/xbox/*.h $(SRC)/core/*.h include/*.h)

$(BUILD)/plugin/%.o: $(SRC)/%.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CLANG) $(CXXFLAGS) -c $< -o $@

$(BUILD)/xrt/%.o: %.cpp $(HEADERS)
	@mkdir -p $(dir $@)
	$(CLANG) $(CXXFLAGS) -c $< -o $@

$(BUILD)/xrt/entry.o: entry.S
	@mkdir -p $(dir $@)
	$(CLANG) $(ASFLAGS) -c $< -o $@

$(BUILD)/imports.S: imports.txt gen_imports.py
	@mkdir -p $(BUILD)
	$(PYTHON) gen_imports.py imports.txt $@

$(BUILD)/xrt/imports.o: $(BUILD)/imports.S
	@mkdir -p $(dir $@)
	$(CLANG) $(ASFLAGS) -c $< -o $@

$(BUILD)/dll.ld: dll.ld
	@mkdir -p $(BUILD)
	sed 's/0x8A000000/$(BASE)/' $< > $@

$(BUILD)/$(NAME).elf: $(OBJS) $(BUILD)/dll.ld
	$(LLD) -m elf32ppc -static --gc-sections --no-undefined -T $(BUILD)/dll.ld -Map $(BUILD)/$(NAME).map \
	  -o $@ $(OBJS)

$(BUILD)/$(NAME).xex: $(BUILD)/$(NAME).elf elf2xex.py xexsign.py
	$(PYTHON) elf2xex.py $(XEX_FLAGS) --base $(BASE) --pe-name $(NAME).exe $< $@
	$(SIGN_CHECK)

disasm: $(BUILD)/$(NAME).elf
	$(OBJDUMP) -d --no-show-raw-insn $< > $(BUILD)/$(NAME).dis

clean:
	rm -rf build

.PHONY: all all-variants disasm clean
