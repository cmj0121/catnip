# Native (host) build and tests for the Catnip runtime.
#
# The runtime is written to compile both on a host (for these tests) and on the
# ESP32-S3 (via PlatformIO, see platformio.ini). `make test` builds every
# test/native/*.c against the vendored Lua and the runtime, and runs them.

CC      ?= cc
CSTD    ?= -std=c99
WARN    ?= -Wall
OPT     ?= -O2 -g
LUADIR  := lib/lua
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

INC     := -I$(LUADIR) -I$(SRCDIR)
LUA_SRC := $(wildcard $(LUADIR)/*.c)
RT_SRC  := $(wildcard $(SRCDIR)/*.c)
TEST_SRC := $(wildcard test/native/*.c)
TESTS   := $(patsubst test/native/%.c,$(BUILD)/%,$(TEST_SRC))

.PHONY: test clean
test: $(TESTS)
	@echo "== running native tests =="
	@for t in $(TESTS); do echo "-- $$t"; "$$t" || exit 1; done
	@echo "== all native tests passed =="

$(BUILD)/%: test/native/%.c $(LUA_SRC) $(RT_SRC) | $(BUILD)
	$(CC) $(CSTD) $(WARN) $(OPT) $(PLAT_DEFS) $(INC) $^ -lm $(PLAT_LIBS) -o $@

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf .build
