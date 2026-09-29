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
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Iklatt

KLATT_SRC := $(wildcard klatt/*.c)
KLATT_HDR := $(wildcard klatt/*.h)
TEST_SRC  := $(filter-out tests/calibrate.c,$(wildcard tests/*.c))
TESTS     := $(OUT)/run_tests.exe

.PHONY: build check all test
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

$(TESTS): $(TEST_SRC) $(wildcard tests/*.h) $(DLL) | $(OUT)
	$(CC) $(CFLAGS) -Itests -o $@ $(TEST_SRC) $(IMPLIB) -static

check: build
	cd $(OUT) && ./run_tests.exe

all:
	$(MAKE) ARCH=32 build
	$(MAKE) ARCH=64 build

test:
	$(MAKE) ARCH=32 check
	$(MAKE) ARCH=64 check
