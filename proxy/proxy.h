#pragma once

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "protocol/egl.h"
#include "protocol/gles2.h"
#include "protocol/gles3_2.h"
#include "protocol/wl.h"
#include "protocol/wl_egl.h"
#include "tests/shaders.h"

#include "gles_bridge_protocol.h"
#include "gles_util_proxy.h"

#define DEFAULT_EGL_IDX 1
#define DEFAULT_WL_IDX 1
#define DEFAULT_WL_EGL_IDX 1

/* Convenience macros */
#define AR(r) ArgReader r = ar_init(C->args, C->args_len)

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if (__has_attribute(visibility) || defined(__GNUC__) && __GNUC__ >= 4)
#define WL_PRIVATE __attribute__((visibility("hidden")))
#else
#define WL_PRIVATE
#endif

extern uint8_t *data;
extern uint8_t *wl_data;

/* ── Helper: data pointer ────────────────────────────────────────────────── */
static inline void *dp(uint32_t off)
{
  return data + off;
}

static inline void *dp_wl(uint32_t off)
{
  return wl_data + off;
}
