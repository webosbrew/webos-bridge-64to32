#pragma once

#include <GLES3/gl32.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "gles_util_shared.h"

#define MAX_MAPS 64

typedef struct
{
  void *real_ptr;      // pointer returned by glMapBufferRange offset)
  uint32_t shm_offset; // offset into shared memory
  GLsizeiptr length;   // length of mapped range
  GLenum target;       // GL buffer target
  GLintptr offset;     // buffer-relative offset passed to glMapBufferRange
  GLuint buffer;
  GLbitfield access;
} MapEntry;

typedef struct
{
  GLsync real;
} SyncEntry;

extern AttribState g_attrib_proxy_state[MAX_VERTEX_ATTRIBS];
extern GLContextState g_proxy_ctx[MAX_CONTEXTS];
extern MapEntry maps[MAX_MAPS];

static inline GLenum dump_fbo_state(const char *tag)
{
  GLint draw = 0;
  GLint read = 0;
  GLint fbo = 0;

#ifdef GL_DRAW_FRAMEBUFFER_BINDING
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
#else
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  draw = fbo;
  read = fbo;
#endif

#ifdef GL_DRAW_FRAMEBUFFER
  GLenum draw_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

  GLenum read_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);

  log_console("[%s] FBO dump: draw=%d read=%d bind=%d "
              "draw_status=0x%x read_status=0x%x",
              tag, draw, read, fbo, draw_status, read_status);

  return draw_status;
#else
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

  log_console("[%s] FBO dump: draw=%d read=%d bind=%d status=0x%x", tag, draw,
              read, fbo, status);

  return status;
#endif
}

static inline void performRenderingChecklist(const char *location)
{
  dump_fbo_state(location);

  GLint prog = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  log_console("%s current program=%d", location, prog);

  GLint vao = 0;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
  log_console("%s VAO=%d", location, vao);

  GLint enabled = 0;
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
  log_console("%s attrib0 enabled=%d", location, enabled);

  GLint ebo = 0;
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);

  GLint bufSize = 0;
  glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufSize);

  GL_LOG_IF_ERR("%s -> err=0x%04x", location, after_err);
}
