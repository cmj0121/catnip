# Native (host) build and tests for the Catnip runtime.
#
# The runtime is written to compile both on a host (for these tests) and on the
# ESP32-S3 (via PlatformIO, see platformio.ini). `make test` builds every
# test/native/*.c against the vendored Lua and the runtime, and runs them.

CC      ?= cc
CSTD    ?= -std=c99
WARN    ?= -Wall
OPT     ?= -O2 -g
PIO_ENV ?= meowkit
# lib sources are discovered under lib/*
SRCDIR  := src
BUILD   := .build/native

UNAME   := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  PLAT_DEFS := -DLUA_USE_MACOSX
  PLAT_LIBS :=
else
  PLAT_DEFS := -DLUA_USE_LINUX
  PLAT_LIBS := -ldl
endif

LIBDIRS := $(wildcard lib/*)
INC     := $(addprefix -I,$(LIBDIRS)) -I$(SRCDIR)
LIB_SRC := $(wildcard lib/*/*.c)
RT_SRC  := $(wildcard $(SRCDIR)/*.c)
# Most of src/device is Arduino C++ and cannot be built here. The few pieces
# that are deliberately hardware-free - a driver's logic, lifted out of the
# driver so a host test can drive it - are listed one by one rather than
# wildcarded, so that adding a device source cannot quietly break this build.
RT_SRC  += $(SRCDIR)/device/input_debounce.c
RT_SRC  += $(SRCDIR)/device/touch_map.c
RT_SRC  += $(SRCDIR)/device/diag_layout.c
RT_SRC  += $(SRCDIR)/device/imu_map.c
TEST_SRC := $(wildcard test/native/*.c)
TESTS   := $(patsubst test/native/%.c,$(BUILD)/%,$(TEST_SRC))

.PHONY: all build test device clean
all: build

build: $(TESTS)        # compile the runtime + host tests (no run)
	@echo "== firmware host build ok =="

test: build            # compile, then run the host tests
	@echo "== running native tests =="
	@for t in $(TESTS); do echo "-- $$t"; "$$t" || exit 1; done
	@echo "== all native tests passed =="

device:                # build the on-device firmware image (needs PlatformIO)
	pio run -e $(PIO_ENV)

$(BUILD)/%: test/native/%.c $(LIB_SRC) $(RT_SRC) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) $(PLAT_DEFS) $(INC) $^ -lm $(PLAT_LIBS) -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf .build
