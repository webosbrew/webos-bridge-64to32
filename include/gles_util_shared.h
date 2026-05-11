#pragma once

#include <GLES3/gl32.h>

#include "../bridge/shared_util.h"

#define GL_LOG_IF_ERR(...)                                                     \
  do                                                                           \
  {                                                                            \
    GLint after_err = glGetError();                                            \
    if (after_err != GL_NO_ERROR)                                              \
    {                                                                          \
      log_console(__VA_ARGS__);                                                \
    }                                                                          \
  } while (0)

#ifdef DEBUG_ABORT_ON_GL_ERROR
#undef GL_LOG_IF_ERR
#define GL_LOG_IF_ERR(...)                                                     \
  do                                                                           \
  {                                                                            \
    GLint after_err = glGetError();                                            \
    if (after_err != GL_NO_ERROR)                                              \
    {                                                                          \
      log_console(__VA_ARGS__);                                                \
      log_error("aborting due to stale GL/EGL error");                         \
      abort();                                                                 \
    }                                                                          \
  } while (0)
#endif

#define MAX_CONTEXTS 4
#define MAX_VAOS 1024
#define MAX_VERTEX_ATTRIBS 16

typedef struct
{
  GLint size;
  GLenum type;
  GLboolean normalized;
  GLsizei stride;
  GLboolean enabled;

  uintptr_t pointer;
  GLuint divisor;    // for glVertexAttribDivisor
  GLboolean integer; // for glVertexAttribIPointer

  GLint vbo; // buffer

} AttribState;

typedef struct
{
  AttribState attribs[MAX_VERTEX_ATTRIBS];
  GLuint ebo;
  uint8_t initialised;
} VAOState;

typedef struct
{
  VAOState vaos[MAX_VAOS];
  GLuint current_vao;
} GLContextState;

typedef struct
{
  uint32_t off;
  uint32_t len;
} ShaderChunk;
