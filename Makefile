ARCH ?= 32
ifeq ($(ARCH),32)
BIN := C:/winlibs/mingw32/bin
else ifeq ($(ARCH),64)
BIN := C:/winlibs/mingw64/bin
else
$(error ARCH must be 32 or 64)
endif

CC     := $(BIN)/gcc.exe
OUT    := build/$(ARCH)
DLL    := $(OUT)/klatt$(ARCH).dll
IMPLIB := $(OUT)/libklatt$(ARCH).a
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Iklatt -ffp-contract=off
ifeq ($(ARCH),32)
# a host that sets 24-bit x87 precision (e.g. Direct3D) would otherwise change 32-bit output by a few LSB;
# force SSE2 math so it does not depend on the x87 control word.
CFLAGS += -msse2 -mfpmath=sse -mstackrealign
endif

KLATT_SRC := $(wildcard klatt/*.c)
KLATT_HDR := $(wildcard klatt/*.h)
TEST_SRC  := $(filter-out tests/calibrate.c,$(wildcard tests/*.c))
GUI_SRC   := $(addprefix kpe/,main.c strips.c table.c dialogs.c audio.c)
CORE_SRC  := $(filter-out $(GUI_SRC),$(wildcard kpe/*.c))
TESTS     := $(OUT)/run_tests.exe

.PHONY: build check all test calibrate
build: $(DLL) $(TESTS)

$(OUT):
	mkdir -p $@

ifeq ($(ARCH),32)
# 32-bit __stdcall: the DLL exports plain names (--kill-at), while C callers reference decorated ones
# (KlattCreate@8). dlltool -k builds an import library that maps the decorated names to the plain exports.
$(DLL): $(KLATT_SRC) $(KLATT_HDR) | $(OUT)
	$(CC) $(CFLAGS) -DKLATT_BUILD -shared -o $@ $(KLATT_SRC) -static-libgcc -Wl,--kill-at,--output-def,$(OUT)/klatt.def
	sed -E 's/^ +[A-Za-z0-9_]+ = ([A-Za-z0-9_]+@[0-9]+).*/    \1/' $(OUT)/klatt.def > $(OUT)/klatt.imp.def
	$(BIN)/dlltool.exe -k -d $(OUT)/klatt.imp.def -D klatt32.dll -l $(IMPLIB)
else
$(DLL): $(KLATT_SRC) $(KLATT_HDR) | $(OUT)
	$(CC) $(CFLAGS) -DKLATT_BUILD -shared -o $@ $(KLATT_SRC) -static-libgcc -Wl,--out-implib,$(IMPLIB)
endif

$(TESTS): $(TEST_SRC) $(CORE_SRC) $(wildcard tests/*.h kpe/*.h) $(DLL) | $(OUT)
	$(CC) $(CFLAGS) -Itests -Ikpe -o $@ $(TEST_SRC) $(CORE_SRC) $(IMPLIB) -static

check: build
	cd $(OUT) && ./run_tests.exe

calibrate: | $(OUT)
	$(CC) $(CFLAGS) -o $(OUT)/calibrate.exe tests/calibrate.c -static
	$(OUT)/calibrate.exe

all:
	$(MAKE) ARCH=32 build
	$(MAKE) ARCH=64 build

test:
	$(MAKE) ARCH=32 check
	$(MAKE) ARCH=64 check
	$(BIN)/gcc.exe -O2 -Wall -o build/xarch_cmp.exe tools/xarch_cmp.c -static
	build/xarch_cmp.exe build/32/xarch.raw build/64/xarch.raw
