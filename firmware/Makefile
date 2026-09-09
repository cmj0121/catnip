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
# lib/bmi270 is the IMU's 8192-byte configuration image, which exists to be
# pushed over I2C into a chip that is not present on a host. Nothing built here
# reads it, and the wildcard would otherwise link 8 KB of unreachable data into
# every test binary. The device build picks it up on its own: PlatformIO's
# library finder sees imu.cpp include bmi270_config.h and compiles lib/bmi270
# into the image, which is why no -I for it appears in platformio.ini.
LIB_SRC := $(filter-out lib/bmi270/%,$(wildcard lib/*/*.c))
RT_SRC  := $(wildcard $(SRCDIR)/*.c)
# Most of src/device is Arduino C++ and cannot be built here. The few pieces
# that are deliberately hardware-free - a driver's logic, lifted out of the
# driver so a host test can drive it - are listed one by one rather than
# wildcarded, so that adding a device source cannot quietly break this build.
RT_SRC  += $(SRCDIR)/device/input_debounce.c
RT_SRC  += $(SRCDIR)/device/input_names.c
RT_SRC  += $(SRCDIR)/device/battery_gauge.c
RT_SRC  += $(SRCDIR)/device/touch_map.c
RT_SRC  += $(SRCDIR)/device/diag_layout.c
RT_SRC  += $(SRCDIR)/device/imu_map.c
RT_SRC  += $(SRCDIR)/device/ui_input_map.c
RT_SRC  += $(SRCDIR)/device/press_gesture.c
RT_SRC  += $(SRCDIR)/device/swipe.c
RT_SRC  += $(SRCDIR)/device/rtc_time.c
RT_SRC  += $(SRCDIR)/device/key_repeat.c
RT_SRC  += $(SRCDIR)/device/ui_input_core.c
TEST_SRC := $(wildcard test/native/*.c)
TESTS   := $(patsubst test/native/%.c,$(BUILD)/%,$(TEST_SRC))

# ---- the layout tests, which need LVGL --------------------------------------
#
# A correct tree with wrong boxes is the one class of bug the tests above cannot
# reach: the launch screen that came out 80% mascot, the setter that collapsed
# into a corner, the clock's date under the control hint. Those are the layout
# engine's answers, and the layout engine runs perfectly well with no panel -
# LVGL renders into memory and hands the result to a flush callback, which here
# throws it away. What is asserted is where things ended up.
#
# LVGL is PlatformIO's copy, because pinning a second one here is how the host
# and the device would come to be laid out by different versions. That makes
# these tests conditional: a clone that has never run `pio` has no LVGL, and
# `make test` still runs everything else and says which one it skipped.
LVGL_DIR := .pio/libdeps/$(PIO_ENV)/lvgl
LVGL_SRC := $(shell find $(LVGL_DIR)/src -name '*.c' 2>/dev/null)
LVGL_LIB := $(BUILD)/liblvgl.a
# The device's own backend and frame, compiled as themselves: a test against a
# reimplementation would be a test of the reimplementation.
GUI_SRC  := $(SRCDIR)/device/lvgl_backend.cpp $(SRCDIR)/device/frame.cpp
GUI_CSRC := $(SRCDIR)/device/catnip_icon_img.c $(SRCDIR)/device/catnip_mascot_img.c \
            $(SRCDIR)/device/catnip_font_display.c test/host/host_lvgl.c
LAYOUT_SRC := $(wildcard test/layout/*.cpp)
LAYOUT_BIN := $(patsubst test/layout/%.cpp,$(BUILD)/%,$(LAYOUT_SRC))
ifneq ($(LVGL_SRC),)
  LAYOUT := $(LAYOUT_BIN)
else
  LAYOUT :=
endif

CXX     ?= c++
CXXSTD  ?= -std=gnu++17
LV_DEFS := -DLV_CONF_INCLUDE_SIMPLE
LV_INC  := -I$(LVGL_DIR)/src -I$(LVGL_DIR)

.PHONY: all build test device clean check-icons
all: build

build: $(TESTS) $(LAYOUT)  # compile the runtime + host tests (no run)
	@echo "== firmware host build ok =="

check-icons:           # SVG raster must match catnip_icon_bits.h
	@echo "== checking icon sources =="
	@python3 tools/gen_icons.py --check

test: build check-icons  # compile, check icon drift, then run the host tests
	@echo "== running native tests =="
	@for t in $(TESTS); do echo "-- $$t"; "$$t" || exit 1; done
ifneq ($(LVGL_SRC),)
	@echo "== running layout tests =="
	@for t in $(LAYOUT); do echo "-- $$t"; "$$t" || exit 1; done
else
	@echo "!! layout tests skipped: no LVGL under $(LVGL_DIR)"
	@echo "   run 'make device' (or 'pio pkg install') once to fetch it"
endif
	@echo "== all native tests passed =="

device:                # build the on-device firmware image (needs PlatformIO)
	pio run -e $(PIO_ENV)

$(BUILD)/%: test/native/%.c $(LIB_SRC) $(RT_SRC) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) $(PLAT_DEFS) $(INC) $^ -lm $(PLAT_LIBS) -o $@

# LVGL once, into an archive: 311 files that never change between test binaries.
$(LVGL_LIB): | $(BUILD)
	@echo "== building LVGL for the host (once) =="
	@mkdir -p $(BUILD)/lvgl
	@cd $(BUILD)/lvgl && $(CC) -std=gnu99 -w $(OPT) $(LV_DEFS) \
		-I$(CURDIR)/test/host -I$(CURDIR)/$(SRCDIR) \
		$(addprefix -I$(CURDIR)/,$(LVGL_DIR)/src $(LVGL_DIR)) \
		-c $(addprefix $(CURDIR)/,$(LVGL_SRC))
	@ar rcs $@ $(BUILD)/lvgl/*.o

# The C stays C and the C++ stays C++: the runtime is written to C99 rules, and
# handing it to a C++ compiler would be testing a different translation of it.
LAYOUT_CC_SRC := $(LIB_SRC) $(RT_SRC) $(GUI_CSRC)
LAYOUT_OBJ    := $(patsubst %,$(BUILD)/obj/%.o,$(LAYOUT_CC_SRC) $(GUI_SRC))
LAYOUT_INC    := -Itest/host $(INC) $(LV_INC)

# -MMD -MP so an edited header rebuilds what included it. Without it these
# objects went stale silently, and a layout test that answers from a stale
# object is worse than no layout test - it reports on a build nobody has.
$(BUILD)/obj/%.c.o: %.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CSTD) $(WARN) $(OPT) -MMD -MP $(PLAT_DEFS) $(LV_DEFS) $(LAYOUT_INC) \
		-c $< -o $@

$(BUILD)/obj/%.cpp.o: %.cpp | $(BUILD)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) -MMD -MP $(PLAT_DEFS) $(LV_DEFS) $(LAYOUT_INC) \
		-c $< -o $@

-include $(LAYOUT_OBJ:.o=.d)

$(BUILD)/%: test/layout/%.cpp $(LAYOUT_OBJ) $(LVGL_LIB) | $(BUILD)
	$(CXX) $(CXXSTD) $(WARN) $(OPT) $(PLAT_DEFS) $(LV_DEFS) $(LAYOUT_INC) \
		$^ -lm $(PLAT_LIBS) -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf .build
