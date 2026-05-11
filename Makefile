# =============================================================================
# gles_bridge — Makefile
#
# Produces:
#   out/aarch64/libgles_bridge_core.so  — shared bridge IPC state (aarch64)
#   out/aarch64/libGLESv2.so             — GLES stub, links bridge_core
#   out/aarch64/libEGL.so                — EGL stub,   links bridge_core
#   out/armv7a/gles_proxy               — real-GL dispatcher (armv7a)
#
# Why a separate libgles_bridge_core.so?
#   libGLESv2.so and libEGL.so must share one set of globals (g_ring, g_data,
#   the mutex, the proxy pid).  If bridge_core.c were statically linked into
#   both, the dynamic linker would create two copies, spawning two proxies and
#   using two different shm regions.  Having a shared .so avoids this: the
#   dynamic linker loads libgles_bridge_core.so once and both stubs call
#   into the same instance.
#
# Deployment (all three aarch64 .so files go in the same directory):
#   export LD_LIBRARY_PATH=/path/to/libdir:$LD_LIBRARY_PATH
# =============================================================================

# ── Toolchain ────────────────────────────────────────────────────────────────
CC_32_PATH ?= /opt/arm-webos-linux-gnueabi_sdk-buildroot/bin
CC_64_PATH ?= /opt/aarch64-webos-linux-gnu_sdk-buildroot/bin

CC_32  ?= $(CC_32_PATH)/arm-webos-linux-gnueabi-gcc
CC_64  ?= $(CC_64_PATH)/aarch64-webos-linux-gnu-gcc

SYSROOT_64 ?= /opt/aarch64-webos-linux-gnu_sdk-buildroot/aarch64-webos-linux-gnu/sysroot
SYSROOT_32 ?= /opt/arm-webos-linux-gnueabi_sdk-buildroot/arm-webos-linux-gnueabi/sysroot

GLES2_INCLUDE_32 := $(SYSROOT_32)/usr/include
GLES2_LIB_32     := $(SYSROOT_32)/usr/lib

GLES2_INCLUDE_64 := $(SYSROOT_64)/usr/include

WAYLAND_SCANNER_64 := $(CC_64_PATH)/wayland-scanner
WAYLAND_PROTOCOLS_64 := $(SYSROOT_64)/usr/share/wayland-webos

WAYLAND_SCANNER_32 := $(CC_32_PATH)/wayland-scanner
WAYLAND_PROTOCOLS_32 := $(SYSROOT_32)/usr/share/wayland-webos

# Path baked into the stub for locating the proxy binary at runtime.
# Override with env var GLES_PROXY_BIN at runtime if needed.
PROXY_INSTALL_PATH ?= /media/developer/temp/gles_proxy
64TO32_BRIDGE_DIR ?= /media/developer/apps/usr/palm/applications/org.webosbrew.bridge-64to32

PYTHON ?= python3
PATCHELF ?= patchelf

DEBUG ?= 0
GNU_BUILD_ID ?= 0
HAVE_OWN_WAYLAND_CLIENT ?= 1
HAVE_OWN_WAYLAND_EGL ?= 1
WAYLAND_STUB_NAME ?= libwayland-client.so.0
REAL_WAYLAND_LIB ?= libwayland-client-real.so.0

# Additional debugging, disabled by default as too spammy
DEBUG_TO_LOGFILE ?= 0
DEBUG_VERBOSE ?= 0
DEBUG_SHADERS ?= 0
DEBUG_SHADER_TEST ?= 0
DEBUG_DUMP_SHADERS ?= 0
DEBUG_BRIDGE_VERBOSE ?= 0
DEBUG_OPCODES ?= 0
DEBUG_GL_GETSTRING ?= 0
DEBUG_EGL_GETPROC ?= 0
DEBUG_WAYLAND ?= 0
DEBUG_WAYLAND_VERBOSE ?= 0
DEBUG_ABORT_ON_GL_ERROR ?= 0

ifeq ($(DEBUG),0)
  STRIP_64_CMD = $(CC_64_PATH)/aarch64-webos-linux-gnu-strip --strip-unneeded
  STRIP_32_CMD = $(CC_32_PATH)/arm-webos-linux-gnueabi-strip --strip-unneeded
else
  STRIP_64_CMD = :
  STRIP_32_CMD = :
endif

# performance enhancements
CACHE_UNIFORM_ATTRIB_LOCATION ?= 1

# ── Directories ──────────────────────────────────────────────────────────────
INCLUDE_DIR := include
BRIDGE_DIR  := bridge
STUB_DIR    := stub
PROXY_DIR   := proxy
TOOLS_DIR   := tools
OUT_64      := out/aarch64
OUT_32      := out/armv7a

# ── Flags — 64-bit ────────
CFLAGS_64_BASE := \
    --sysroot=$(SYSROOT_64) \
    -march=armv8-a \
    -Os \
    -fPIC \
    -std=c11 \
    -Wall -Wextra \
    -I$(INCLUDE_DIR) \
    -I$(GLES2_INCLUDE_64) \
    -DGLES_PROXY_DEFAULT_PATH=\"$(PROXY_INSTALL_PATH)\" \

LDFLAGS_64_BASE :=

# ── Flags — 32-bit proxy ─────────────────────────────────────────────────────
CFLAGS_32 := \
    --sysroot=$(SYSROOT_32) \
    -march=armv7-a \
    -mfpu=neon \
    -mfloat-abi=softfp \
    -std=c11 \
    -Wall -Wextra \
    -D_GNU_SOURCE \
    -I$(INCLUDE_DIR) \
    -I$(GLES2_INCLUDE_32)

LDFLAGS_32 := \
    --sysroot=$(SYSROOT_32) \
    -L$(GLES2_LIB_32) \
    -lGLESv2 \
    -lEGL \
    -lwayland-client \
    -lwayland-egl \
    -lrt \
    -lpthread \
    -ldl \
    -Wl,-rpath-link,$(GLES2_LIB_32)

ifeq ($(DEBUG),1)
  CFLAGS_64_BASE += -DDEBUG -g -O0 -fno-omit-frame-pointer
  CFLAGS_32 += -DDEBUG -g -O0 -fno-optimize-sibling-calls \
    -fno-inline -fno-strict-aliasing -fno-omit-frame-pointer -Wcast-function-type -Werror=cast-function-type
  LDFLAGS_32 += -g
  LDFLAGS_64_BASE += -g
else
  CFLAGS_64_BASE += -O3
  CFLAGS_32      += -O3
endif

ifeq ($(DEBUG_TO_LOGFILE),1)
  CFLAGS_64_BASE += -DDEBUG_TO_LOGFILE
  CFLAGS_32 += -DDEBUG_TO_LOGFILE
endif

ifeq ($(DEBUG_VERBOSE),1)
  CFLAGS_64_BASE += -DDEBUG_VERBOSE
  CFLAGS_32 += -DDEBUG_VERBOSE
endif

ifeq ($(DEBUG_SHADERS),1)
  CFLAGS_64_BASE += -DDEBUG_SHADERS
  CFLAGS_32 += -DDEBUG_SHADERS
endif

ifeq ($(DEBUG_SHADER_TEST),1)
  CFLAGS_64_BASE += -DDEBUG_SHADER_TEST
  CFLAGS_32 += -DDEBUG_SHADER_TEST
endif

ifeq ($(DEBUG_DUMP_SHADERS),1)
  CFLAGS_64_BASE += -DDEBUG_DUMP_SHADERS
  CFLAGS_32 += -DDEBUG_DUMP_SHADERS
endif

ifeq ($(DEBUG_BRIDGE_VERBOSE),1)
  CFLAGS_64_BASE += -DDEBUG_BRIDGE_VERBOSE
  CFLAGS_32 += -DDEBUG_BRIDGE_VERBOSE
endif

ifeq ($(DEBUG_OPCODES),1)
  CFLAGS_64_BASE += -DDEBUG_OPCODES
  CFLAGS_32 += -DDEBUG_OPCODES
endif

ifeq ($(DEBUG_GL_GETSTRING),1)
  CFLAGS_64_BASE += -DDEBUG_GL_GETSTRING
  CFLAGS_32 += -DDEBUG_GL_GETSTRING
endif

ifeq ($(DEBUG_EGL_GETPROC),1)
  CFLAGS_64_BASE += -DDEBUG_EGL_GETPROC
  CFLAGS_32 += -DDEBUG_EGL_GETPROC
endif

ifeq ($(DEBUG_WAYLAND),1)
  CFLAGS_64_BASE += -DDEBUG_WAYLAND
  CFLAGS_32 += -DDEBUG_WAYLAND
endif

ifeq ($(DEBUG_WAYLAND_VERBOSE),1)
  CFLAGS_64_BASE += -DDEBUG_WAYLAND_VERBOSE
  CFLAGS_32 += -DDEBUG_WAYLAND_VERBOSE
endif

ifeq ($(DEBUG_ABORT_ON_GL_ERROR),1)
  CFLAGS_64_BASE += -DDEBUG_ABORT_ON_GL_ERROR
  CFLAGS_32 += -DDEBUG_ABORT_ON_GL_ERROR
endif

ifeq ($(GNU_BUILD_ID),1)
  LDFLAGS_32 += -Wl,--build-id=sha1
  LDFLAGS_64_BASE += -Wl,--build-id=sha1
endif

ifeq ($(HAVE_OWN_WAYLAND_CLIENT),1)
  CFLAGS_64_BASE += -DHAVE_OWN_WAYLAND_CLIENT
  CFLAGS_32 += -DHAVE_OWN_WAYLAND_CLIENT
endif

ifeq ($(HAVE_OWN_WAYLAND_EGL),1)
  CFLAGS_64_BASE += -DHAVE_OWN_WAYLAND_EGL
  CFLAGS_32 += -DHAVE_OWN_WAYLAND_EGL
endif

ifeq ($(CACHE_UNIFORM_ATTRIB_LOCATION),1)
  CFLAGS_64_BASE += -DCACHE_UNIFORM_ATTRIB_LOCATION
  CFLAGS_32 += -DCACHE_UNIFORM_ATTRIB_LOCATION
endif

WAYLAND_XML_64 := $(WAYLAND_PROTOCOLS_64)/webos-shell.xml
WAYLAND_GEN_C_64 := $(OUT_64)/webos-shell.c
WAYLAND_GEN_H_64 := $(INCLUDE_DIR)/webos-shell.h

WAYLAND_XML_INPUT_64 := $(WAYLAND_PROTOCOLS_64)/webos-input-manager.xml
WAYLAND_GEN_C_INPUT_64 := $(OUT_64)/webos-input-manager.c
WAYLAND_GEN_H_INPUT_64 := $(INCLUDE_DIR)/webos-input-manager.h

WAYLAND_XML_32 := $(WAYLAND_PROTOCOLS_32)/webos-shell.xml
WAYLAND_GEN_C_32 := $(OUT_32)/webos-shell.c
WAYLAND_GEN_H_32 := $(INCLUDE_DIR)/webos-shell-32.h

WAYLAND_XML_INPUT_32 := $(WAYLAND_PROTOCOLS_32)/webos-input-manager.xml
WAYLAND_GEN_C_INPUT_32 := $(OUT_32)/webos-input-manager.c
WAYLAND_GEN_H_INPUT_32 := $(INCLUDE_DIR)/webos-input-manager-32.h

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all bridge_core stub egl wl proxy clean install dirs

all: dirs bridge_core stub egl wl proxy $(WAYLAND_GEN_C_64) $(WAYLAND_GEN_H_64) \
  $(WAYLAND_GEN_C_INPUT_64) $(WAYLAND_GEN_H_INPUT_64) \
  $(WAYLAND_GEN_C_32) $(WAYLAND_GEN_H_32) \
  $(WAYLAND_GEN_C_INPUT_32) $(WAYLAND_GEN_H_INPUT_32)

dirs:
	@mkdir -p $(OUT_64) $(OUT_32)

tools/gles2_generated.c tools/gles2_generated.h tools/gles2_invoke_dynamic.c: tools/gen_gles2_sigs.py
	cd tools && $(PYTHON) gen_gles2_sigs.py

$(WAYLAND_GEN_H_32): $(WAYLAND_XML_32)
	$(WAYLAND_SCANNER_32) client-header $(WAYLAND_XML_32) $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(WAYLAND_GEN_C_32): $(WAYLAND_XML_32)
	$(WAYLAND_SCANNER_32) private-code $(WAYLAND_XML_32) $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(WAYLAND_GEN_H_INPUT_32): $(WAYLAND_XML_INPUT_32)
	$(WAYLAND_SCANNER_32) client-header $(WAYLAND_XML_INPUT_32) $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(WAYLAND_GEN_C_INPUT_32): $(WAYLAND_XML_INPUT_32)
	$(WAYLAND_SCANNER_32) private-code $(WAYLAND_XML_INPUT_32) $@.tmp
	if ! cmp -s $@.tmp $@; then mv $@.tmp $@; else rm $@.tmp; fi

$(OUT_32)/webos-shell.o: $(WAYLAND_GEN_C_32)
	$(CC_32) $(CFLAGS_32) -c $< -o $@

$(OUT_32)/webos-input-manager.o: $(WAYLAND_GEN_C_INPUT_32)
	$(CC_32) $(CFLAGS_32) -c $< -o $@

$(WAYLAND_GEN_C_64): $(WAYLAND_XML_64)
	$(WAYLAND_SCANNER_64) private-code $(WAYLAND_XML_64) $@.new
	if ! cmp -s $@.new $@; then mv $@.new $@; else rm $@.new; fi

$(WAYLAND_GEN_H_64): $(WAYLAND_XML_64)
	$(WAYLAND_SCANNER_64) client-header $(WAYLAND_XML_64) $@.new
	if ! cmp -s $@.new $@; then mv $@.new $@; else rm $@.new; fi

$(WAYLAND_GEN_C_INPUT_64): $(WAYLAND_XML_INPUT_64)
	$(WAYLAND_SCANNER_64) private-code $(WAYLAND_XML_INPUT_64) $@.new
	if ! cmp -s $@.new $@; then mv $@.new $@; else rm $@.new; fi

$(WAYLAND_GEN_H_INPUT_64): $(WAYLAND_XML_INPUT_64)
	$(WAYLAND_SCANNER_64) client-header $(WAYLAND_XML_INPUT_64) $@.new
	if ! cmp -s $@.new $@; then mv $@.new $@; else rm $@.new; fi

$(OUT_64)/webos-shell.o: $(WAYLAND_GEN_C_64)
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/webos-input-manager.o: $(WAYLAND_GEN_C_INPUT_64)
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

# ─────────────────────────────────────────────────────────────────────────────
# 1. libgles_bridge_core.so  (aarch64)
#    Holds all bridge global state.  Both libGLESv2.so and libEGL.so link
#    against this so they share a single proxy connection.
# ─────────────────────────────────────────────────────────────────────────────
CORE_OBJ := \
  $(OUT_64)/bridge_core.o \
  $(OUT_64)/bridge_shm.o \
  $(OUT_64)/shared_util.o \
  $(OUT_64)/cJSON.o

LDFLAGS_CORE := \
  --sysroot=$(SYSROOT_64) \
  -shared \
  -Wl,-soname,libgles_bridge_core.so \
  -lrt -lpthread -ldl

$(OUT_64)/bridge_core.o: $(STUB_DIR)/bridge_core.c
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/bridge_shm.o: $(BRIDGE_DIR)/bridge_shm.c
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/shared_util.o: $(BRIDGE_DIR)/shared_util.c
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/cJSON.o: deps/cJSON.c deps/cJSON.h
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/libgles_bridge_core.so: $(CORE_OBJ)
	$(CC_64) $(LDFLAGS_CORE) $(LDFLAGS_64_BASE) -o $@ $^
	$(STRIP_64_CMD) $@
	@echo "  [64] Built $@"

bridge_core: $(OUT_64)/libgles_bridge_core.so

# ─────────────────────────────────────────────────────────────────────────────
# 2. libGLESv2.so  (aarch64)
# ─────────────────────────────────────────────────────────────────────────────
STUB_OBJ := $(OUT_64)/gles2_stub.o \
  $(OUT_64)/gles2_generated.o \
  $(OUT_64)/gles3_2_stub.o

LDFLAGS_GLES := \
  --sysroot=$(SYSROOT_64) \
  -shared \
  -Wl,-soname,libGLESv2.so.2 \
  -L$(OUT_64) \
  -lgles_bridge_core \
  -Wl,-rpath-link,$(OUT_64)

LDFLAGS_GLES += -Wl,--version-script=$(STUB_DIR)/libGLESv3_2.map

$(OUT_64)/gles2_stub.o: $(STUB_DIR)/gles2_stub.c
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/gles3_2_stub.o: $(STUB_DIR)/gles3_2_stub.c
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/gles2_generated.o: tools/gles2_generated.c | tools/gles2_generated.h tools/gles2_invoke_dynamic.c
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/libGLESv2.so.2: $(STUB_OBJ) $(OUT_64)/libgles_bridge_core.so
	$(CC_64) $(LDFLAGS_GLES) $(LDFLAGS_64_BASE) -o $@ $(STUB_OBJ)
	$(STRIP_64_CMD) $@
	@echo "  [64] Built $@"

stub: $(OUT_64)/libGLESv2.so.2

# ─────────────────────────────────────────────────────────────────────────────
# 3. libEGL.so  (aarch64)
# ─────────────────────────────────────────────────────────────────────────────
EGL_OBJ := $(OUT_64)/egl_stub.o \
  $(OUT_64)/egl_generated.o \
  $(OUT_64)/gles2_generated.o

LDFLAGS_EGL := \
  --sysroot=$(SYSROOT_64) \
  -shared \
  -Wl,-soname,libEGL.so.1 \
  -Wl,--version-script=$(STUB_DIR)/libEGL.map \
  -L$(OUT_64) \
  -lgles_bridge_core \
  -Wl,-rpath-link,$(OUT_64) \
  -ldl

EGL_GEN_FILES := \
    tools/egl_generated.c \
    tools/egl_generated.h \
    tools/egl_invoke_dynamic.c

tools/.egl_gen_stamp: tools/gen_egl_sigs.py
	cd tools && $(PYTHON) gen_egl_sigs.py
	@touch $@

$(EGL_GEN_FILES): tools/.egl_gen_stamp

$(OUT_64)/egl_generated.o: tools/egl_generated.c tools/.egl_gen_stamp

$(OUT_64)/egl_stub.o: $(STUB_DIR)/egl_stub.c $(EGL_GEN_FILES)
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/egl_generated.o: tools/egl_generated.c | tools/egl_generated.h tools/egl_invoke_dynamic.c
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/libEGL.so.1: $(EGL_OBJ) $(OUT_64)/libgles_bridge_core.so
	$(CC_64) $(LDFLAGS_EGL) $(LDFLAGS_64_BASE) -o $@ $(EGL_OBJ)
	$(STRIP_64_CMD) $@
	@echo "  [64] Built $@"

egl: $(OUT_64)/libEGL.so.1

# ─────────────────────────────────────────────────────────────────────────────
# 3. libwayland-client/stub.so  (aarch64)
# ─────────────────────────────────────────────────────────────────────────────
ifeq ($(HAVE_OWN_WAYLAND_CLIENT),1)
  WL_OBJ := \
    $(OUT_64)/wl_stub.o \
    $(OUT_64)/webos-shell.o \
    $(OUT_64)/webos-input-manager.o

  # libwayland-egl and xkbcommon needed for SDL2
  LDFLAGS_WL := \
    --sysroot=$(SYSROOT_64) \
    -shared \
    -Wl,-soname,$(WAYLAND_STUB_NAME) \
    -lwayland-client \
    -lwayland-cursor \
    -lwayland-egl \
    -lxkbcommon \
    -L$(OUT_64) \
    -lgles_bridge_core \
    -Wl,-rpath-link,$(OUT_64) \
    -ldl

$(OUT_64)/wl_stub.o: $(STUB_DIR)/wl_stub.c $(WAYLAND_GEN_H_64)
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/$(WAYLAND_STUB_NAME): $(WL_OBJ) $(OUT_64)/libgles_bridge_core.so
	$(CC_64) $(LDFLAGS_WL) $(LDFLAGS_64_BASE) -o $@ $(WL_OBJ)
	$(STRIP_64_CMD) $@
ifeq ($(WAYLAND_STUB_NAME), libwayland-client.so.0)
	$(PATCHELF) --replace-needed libwayland-client.so.0 $(REAL_WAYLAND_LIB) $(OUT_64)/$(WAYLAND_STUB_NAME)
endif
	@echo "  [64] Built $@"

wl: $(OUT_64)/$(WAYLAND_STUB_NAME)
endif

# ─────────────────────────────────────────────────────────────────────────────
# 4. libwayland-egl.so  (aarch64)
# ─────────────────────────────────────────────────────────────────────────────
ifeq ($(HAVE_OWN_WAYLAND_EGL),1)
  WL_EGL_OBJ := \
    $(OUT_64)/wl_egl_stub.o

  LDFLAGS_WL_EGL := \
    --sysroot=$(SYSROOT_64) \
    -shared \
    -Wl,-soname,libwayland-egl.so.1 \
    -L$(OUT_64) \
    -lgles_bridge_core \
    -Wl,-rpath-link,$(OUT_64) \
    -ldl

$(OUT_64)/wl_egl_stub.o: $(STUB_DIR)/wl_egl_stub.c $(WAYLAND_GEN_H_64)
	$(CC_64) $(CFLAGS_64_BASE) -c $< -o $@

$(OUT_64)/libwayland-egl.so.1: $(WL_EGL_OBJ) $(OUT_64)/libgles_bridge_core.so
	$(CC_64) $(LDFLAGS_WL_EGL) $(LDFLAGS_64_BASE) -o $@ $(WL_EGL_OBJ)
	$(STRIP_64_CMD) $@
	@echo "  [64] Built $@"

wl: $(OUT_64)/libwayland-egl.so.1
endif

# ─────────────────────────────────────────────────────────────────────────────
# 5. gles_proxy  (armv7a)
# ─────────────────────────────────────────────────────────────────────────────
PROXY_OBJ := \
    $(OUT_32)/proxy.o \
    $(OUT_32)/egl.o \
    $(OUT_32)/gles2.o \
    $(OUT_32)/shaders.o \
    $(OUT_32)/bridge_shm.o \
    $(OUT_32)/shared_util.o \
    $(OUT_32)/gles2_invoke_dynamic.o \
    $(OUT_32)/gles3_2.o \
    $(OUT_32)/egl_invoke_dynamic.o \
    $(OUT_32)/webos-shell.o

ifeq ($(HAVE_OWN_WAYLAND_CLIENT),1)
    PROXY_OBJ += \
      $(OUT_32)/wl.o
endif

ifeq ($(HAVE_OWN_WAYLAND_EGL),1)
    PROXY_OBJ += \
      $(OUT_32)/wl_egl.o
endif

PROTOCOL_SRC := proxy/protocol/egl.c \
                proxy/protocol/gles2.c \
                proxy/protocol/wl.c \
                proxy/protocol/wl_egl.c \
                proxy/protocol/gles3_2.c

PROTOCOL_OBJ := $(patsubst $(PROXY_DIR)/protocol/%.c,$(OUT_32)/%.o,$(PROTOCOL_SRC))

PROXY_OBJ += $(PROTOCOL_OBJ)

# Core proxy sources
$(OUT_32)/%.o: $(PROXY_DIR)/%.c $(WAYLAND_GEN_H_32)
	$(CC_32) $(CFLAGS_32) -c $< -o $@

# Protocol sources
$(OUT_32)/%.o: $(PROXY_DIR)/protocol/%.c
	$(CC_32) $(CFLAGS_32) -c $< -o $@

$(OUT_32)/bridge_shm.o: $(BRIDGE_DIR)/bridge_shm.c
	$(CC_32) $(CFLAGS_32) -c $< -o $@

$(OUT_32)/shared_util.o: $(BRIDGE_DIR)/shared_util.c
	$(CC_32) $(CFLAGS_32) -c $< -o $@

# Tests
$(OUT_32)/%.o: $(PROXY_DIR)/tests/%.c
	$(CC_32) $(CFLAGS_32) -c $< -o $@

$(OUT_32)/gles2_invoke_dynamic.o: tools/gles2_invoke_dynamic.c
	$(CC_32) $(CFLAGS_32) -I$(STUB_DIR) -c $< -o $@

$(OUT_32)/egl_invoke_dynamic.o: tools/egl_invoke_dynamic.c
	$(CC_32) $(CFLAGS_32) -I$(STUB_DIR) -c $< -o $@

$(OUT_32)/gles_proxy: $(PROXY_OBJ)
	$(CC_32) -o $@ $^ $(LDFLAGS_32)
	$(STRIP_32_CMD) $@
	@echo "  [32] Built $@"

proxy: $(OUT_32)/gles_proxy

# ─────────────────────────────────────────────────────────────────────────────
# 6. Standalone test: tests/external.c
# ─────────────────────────────────────────────────────────────────────────────
TESTS_DIR := tests

TEST_64 := $(OUT_64)/external_test_64
TEST_32 := $(OUT_32)/external_test

TEST_64_BUF := $(OUT_64)/external_test_buf_64
TEST_32_BUF := $(OUT_32)/external_test_buf

TEST_64_LDFLAGS := -Wl,-rpath=\$$ORIGIN/lib,--gc-sections
TEST_64_LDFLAGS += -Wl,-rpath=$(64TO32_BRIDGE_DIR)/lib

# 64‑bit test build
$(TEST_64): $(TESTS_DIR)/external.c $(OUT_64)/webos-shell.o
	$(CC_64) $(CFLAGS_64_BASE) -o $@ $^ \
    $(TEST_64_LDFLAGS) \
		-L$(SYSROOT_64)/usr/lib \
		-lwayland-client -lwayland-egl -lEGL -lGLESv2 -lpulse-simple -lpulse -pthread -lm
	$(STRIP_64_CMD) $@
	$(PATCHELF) --set-interpreter $(64TO32_BRIDGE_DIR)/lib/ld-linux-aarch64.so.1 $@

# 32‑bit test build
$(TEST_32): $(TESTS_DIR)/external.c $(OUT_32)/webos-shell.o
	$(CC_32) $(CFLAGS_32) -o $@ $^ \
		-L$(SYSROOT_32)/usr/lib \
		-lwayland-client -lwayland-egl -lEGL -lGLESv2 -lpulse-simple -lpulse -pthread -lm

# 64‑bit test build
$(TEST_64_BUF): $(TESTS_DIR)/external_buffer_test.c $(OUT_64)/webos-shell.o
	$(CC_64) $(CFLAGS_64_BASE) -o $@ $^ \
    $(TEST_64_LDFLAGS) \
		-L$(SYSROOT_64)/usr/lib \
		-lwayland-client -lwayland-egl -lEGL -lGLESv2 -lpulse-simple -lpulse -pthread -lm
	$(STRIP_64_CMD) $@
	$(PATCHELF) --set-interpreter $(64TO32_BRIDGE_DIR)/lib/ld-linux-aarch64.so.1 $@

# 32‑bit test build
$(TEST_32_BUF): $(TESTS_DIR)/external_buffer_test.c $(OUT_32)/webos-shell.o
	$(CC_32) $(CFLAGS_32) -o $@ $^ \
		-L$(SYSROOT_32)/usr/lib \
		-lwayland-client -lwayland-egl -lEGL -lGLESv2 -lpulse-simple -lpulse -pthread -lm

tests: dirs $(TEST_64) $(TEST_32) $(TEST_64_BUF) $(TEST_32_BUF)

# ─────────────────────────────────────────────────────────────────────────────
DESTDIR ?= staging

install: all
	install -d $(DESTDIR)/usr/lib
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(OUT_64)/libgles_bridge_core.so  $(DESTDIR)/usr/lib/
	install -m 755 $(OUT_64)/libGLESv2.so.2           $(DESTDIR)/usr/lib/
	install -m 755 $(OUT_64)/libEGL.so.1              $(DESTDIR)/usr/lib/
	install -m 755 $(OUT_64)/libwayland-client.so.0   $(DESTDIR)/usr/lib/
	install -m 755 $(OUT_32)/gles_proxy              $(DESTDIR)/usr/local/bin/
	@echo "Staged to $(DESTDIR)/"
	@echo ""
	@echo "NOTE: The original libwayland-client.so.0 must be renamed/relinked:"
	@echo "  patchelf --set-soname libwayland-client-real.so.0 <original libwayland-client.so.0>"
	@echo "  install it as libwayland-client-real.so.0 alongside the bridge libs"

clean:
	rm -rf out staging
