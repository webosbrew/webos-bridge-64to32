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
extern BridgeRing *ring;

/* ── Helper: data pointer ────────────────────────────────────────────────── */
static inline void *dp(uint32_t off)
{
  return data + off;
}

static inline void *dp_wl(uint32_t off)
{
  return wl_data + off;
}

/* GL_EXT_buffer_storage */
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
#ifndef GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT
#define GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT 0x00004000
#endif

#define GL_RESULT(C) ((void)(C), ring->result_buf)

/* Copy the texels the stub piggybacked (data2) for a persistently mapped
 * GL_PIXEL_UNPACK_BUFFER into the real mapping at the upload's buffer offset,
 * before the upload reads it. */
static inline void apply_unpack_piggyback(const BridgeCtrl *C, uintptr_t offset)
{
  if (C->data2_size == 0)
    return;

  proxy_resolve_deferred_waits();

  GLint pbo = 0;
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &pbo);

  for (uint32_t i = 1; i < MAX_MAPS; i++)
  {
    MapEntry *m = &maps[i];
    if (!m->real_ptr || m->buffer != (GLuint)pbo)
      continue;
    if ((GLintptr)offset < m->offset ||
        (GLintptr)offset + (GLintptr)C->data2_size > m->offset + m->length)
      break;

    memcpy((uint8_t *)m->real_ptr + ((GLintptr)offset - m->offset),
           dp(C->data2_offset), C->data2_size);

    /* Non-coherent persistent writes need a barrier before GL reads them */
    if (!(m->access & GL_MAP_COHERENT_BIT))
      glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
    break;
  }
}
