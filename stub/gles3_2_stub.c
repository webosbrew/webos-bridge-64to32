#include <GLES3/gl32.h>
#include <alloca.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_PREFIX "[gles32_stub]"
#include "../bridge/shared_util.h"
#include "../include/gles_util_stub.h"
#include "bridge_core.h"
#include "gles_bridge_protocol.h"

StubMapEntry stub_maps[MAX_MAPS];

/* ── helpers ─────────────────────────────────────────────────────────────── */
static void setup_scalar(GLBridgeOpcode op)
{
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = (uint32_t)op;
  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;
}

/* ───────── VAOs ───────── */

GL_APICALL void GL_APIENTRY glGenVertexArrays(GLsizei n, GLuint *arrays)
{
#ifdef DEBUG_VERBOSE
  log_console("glGenVertexArrays: n:%d arrays:%p g_stub_current_ctx:%d vao:%d",
              n, arrays, g_stub_current_ctx,
              g_stub_ctx[g_stub_current_ctx].current_vao);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGenVertexArrays);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  memcpy(arrays, C->result_buf, n * sizeof(GLuint));

  GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];

  for (GLsizei i = 0; i < n; ++i)
  {
    GLuint id = arrays[i];
    memset(&ctx->vaos[id], 0, sizeof(VAOState));
  }
}

GL_APICALL void GL_APIENTRY glDeleteVertexArrays(GLsizei n,
                                                 const GLuint *arrays)
{
#ifdef DEBUG_VERBOSE
  log_console("glDeleteVertexArrays: n:%d arrays:%p", n, arrays);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDeleteVertexArrays);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_i32(&W, n);

  C->args_len = W.pos;

  C->data_offset = bridge_data_write(arrays, n * sizeof(GLuint));

  C->data_size = n * sizeof(GLuint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBindVertexArray(GLuint array)
{
#ifdef DEBUG_VERBOSE
  log_console("glBindVertexArray: array:%d g_stub_current_ctx:%d", array,
              g_stub_current_ctx);
#endif
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->vao == array)
    return;
  s->vao = array;
#endif
  g_stub_ctx[g_stub_current_ctx].current_vao = array;

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glBindVertexArray);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, array);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL GLboolean GL_APIENTRY glIsVertexArray(GLuint array)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glIsVertexArray);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, array);

  C->args_len = W.pos;

  return (GLboolean)BRIDGE_SEND_CALL();
}

/* ───────── Queries ───────── */

GL_APICALL void GL_APIENTRY glGenQueries(GLsizei n, GLuint *ids)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGenQueries);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_i32(&W, n);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  memcpy(ids, C->result_buf, n * sizeof(GLuint));
}

GL_APICALL void GL_APIENTRY glDeleteQueries(GLsizei n, const GLuint *ids)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDeleteQueries);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_i32(&W, n);

  C->args_len = W.pos;

  C->data_offset = bridge_data_write(ids, n * sizeof(GLuint));

  C->data_size = n * sizeof(GLuint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBeginQuery(GLenum target, GLuint id)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glBeginQuery);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);
  aw_u32(&W, id);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glEndQuery(GLenum target)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glEndQuery);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

/* ───────── Samplers ───────── */

GL_APICALL void GL_APIENTRY glBindSampler(GLuint unit, GLuint sampler)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (unit < MAX_TEXTURE_UNITS)
  {
    if (s->sampler[unit] == sampler)
      return;
    s->sampler[unit] = sampler;
  }
#endif
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glBindSampler);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, unit);
  aw_u32(&W, sampler);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

/* ───────── Instancing ───────── */

GL_APICALL void GL_APIENTRY glDrawArraysInstanced(GLenum mode, GLint first,
                                                  GLsizei count,
                                                  GLsizei instancecount)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDrawArraysInstanced);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, mode);
  aw_i32(&W, first);
  aw_i32(&W, count);
  aw_i32(&W, instancecount);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glVertexAttribDivisor(GLuint index, GLuint divisor)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glVertexAttribDivisor);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, index);
  aw_u32(&W, divisor);

  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_stub_state[index].divisor = divisor;
    GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index].divisor = divisor;
  }
  else
    log_error("glVertexAttribDivisor: index:%d > MAX_VERTEX_ATTRIBS!", index);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void *GL_APIENTRY glMapBufferRange(GLenum target, GLintptr offset,
                                              GLsizeiptr length,
                                              GLbitfield access)
{
  GLuint buf = get_bound_buffer(target);

#ifdef DEBUG_VERBOSE
  log_console("glMapBufferRange: target=0x%04x offset=%d length=%d "
              "access=0x%04x GL_MAP_READ_BIT set=%d buf=%d",
              target, offset, length, access,
              access & GL_MAP_READ_BIT ? true : false, buf);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glMapBufferRange);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i64(&W, (int64_t)offset);
  aw_i64(&W, (int64_t)length);
  aw_u32(&W, access);
  aw_u32(&W, buf);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  uint32_t map_id = (uint32_t)C->result;
  if (!map_id)
  {
    log_error("glMapBufferRange: map_id not found - C->result:%d target=0x%04x "
              "offset=%d length=%d "
              "access=0x%04x GL_MAP_READ_BIT set=%d buf=%d",
              (uint32_t)C->result, target, offset, length, access,
              access & GL_MAP_READ_BIT ? true : false, buf);
    return NULL;
  }
#ifdef DEBUG_VERBOSE
  else
  {
    log_console("glMapBufferRange: map_id:%d", map_id);
  }
#endif

  // Allocate a private buffer instead of pointing into shared data region
  void *local = malloc(length);
  if (!local)
  {
    log_error("glMapBufferRange: Unable to alloc buffer: %d", length);
    return NULL;
  }

  if (access & GL_MAP_READ_BIT)
  {
#ifdef DEBUG_VERBOSE
    log_console("glMapBufferRange: bridge_data_read - local:%p "
                "C->data_offset:%d offset:%d",
                local, C->data_offset, length);
#endif
    bridge_data_read(local, C->data_offset, length);
  }

  stub_maps[map_id].id = map_id;
  stub_maps[map_id].ptr = local;
  stub_maps[map_id].length = length;
  stub_maps[map_id].offset = offset;
  stub_maps[map_id].buffer = buf;
  stub_maps[map_id].target = target;
  stub_maps[map_id].access = access;

  return local;
}

GL_APICALL void GL_APIENTRY glTexStorage2D(GLenum target, GLsizei levels,
                                           GLenum internalformat, GLsizei width,
                                           GLsizei height)
{
#ifdef DEBUG_VERBOSE
  log_console("glTexStorage2D target=0x%04x levels=%d fmt=0x%04x %dx%d", target,
              levels, internalformat, width, height);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glTexStorage2D;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, levels);
  aw_u32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  C->args_len = W.pos;

#ifdef DEBUG_VERBOSE
  log_console("[stub][glTexStorage2D] sending opcode=%u", C->opcode);
#endif

  BRIDGE_SEND_VOID();
}

GL_APICALL GLsync GL_APIENTRY glFenceSync(GLenum condition, GLbitfield flags)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glFenceSync);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, condition);
  aw_u32(&W, flags);
  C->args_len = W.pos;

  uint32_t id = (uint32_t)BRIDGE_SEND_CALL();
  return (GLsync)(uintptr_t)id;
}

GL_APICALL GLenum GL_APIENTRY glClientWaitSync(GLsync sync, GLbitfield flags,
                                               GLuint64 timeout)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glClientWaitSync);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, (uint32_t)(uintptr_t)sync); // opaque ID
  aw_u32(&W, flags);
  aw_u64(&W, timeout);
  C->args_len = W.pos;

  return BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glDeleteSync(GLsync sync)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDeleteSync);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, (uint32_t)(uintptr_t)sync);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDebugMessageCallback(GLDEBUGPROC callback,
                                                   const void *userParam)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDebugMessageCallback);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDebugMessageControl(GLenum source, GLenum type,
                                                  GLenum severity,
                                                  GLsizei count,
                                                  const GLuint *ids,
                                                  GLboolean enabled)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDebugMessageControl);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, source);
  aw_u32(&W, type);
  aw_u32(&W, severity);
  aw_i32(&W, count);
  aw_u32(&W, enabled);

  C->args_len = W.pos;

  if (count)
  {
    C->data_offset = bridge_data_write(ids, count * sizeof(GLuint));

    C->data_size = count * sizeof(GLuint);
  }

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glFlushMappedBufferRange(GLenum target,
                                                     GLintptr offset,
                                                     GLsizeiptr length)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFlushMappedBufferRange);

  GLuint buf = get_bound_buffer(target);

  StubMapEntry *m = NULL;

  for (uint32_t i = 1; i < MAX_MAPS; i++)
  {
    if (!stub_maps[i].ptr)
      continue;
    if (stub_maps[i].buffer != buf)
      continue;

    GLintptr map_off = stub_maps[i].offset;
    GLsizeiptr map_len = stub_maps[i].length;

    if (offset < map_off)
      continue;
    if (offset + length > map_off + map_len)
      continue;

    m = &stub_maps[i];
    break;
  }

#ifdef DEBUG_VERBOSE
  log_console(
      "glFlushMappedBufferRange: target:%d offset:%lld length:%lld m->id:%d",
      target, (long long)offset, (long long)length, m ? m->id : 0);
#endif

  if (!m)
  {
    log_error("glFlushMappedBufferRange: no map found for target:%d offset:%d "
              "length:%d",
              target, offset, length);
#ifdef DEBUG_ABORT_ON_GL_ERROR
    abort();
#endif
    return;
  }

  GLintptr local_off = offset - m->offset;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, (int32_t)local_off);
  aw_i32(&W, (int32_t)length);
  aw_u32(&W, m->id);
  C->args_len = W.pos;

  C->data_offset =
      bridge_data_write((uint8_t *)m->ptr + local_off, (size_t)length);
  C->data_size = length;

  BRIDGE_SEND_VOID();
}

GL_APICALL GLboolean GL_APIENTRY glUnmapBuffer(GLenum target)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  GLuint buf = get_bound_buffer(target);

  StubMapEntry *m = NULL;
  for (uint32_t i = 1; i < MAX_MAPS; i++)
  {
    if (stub_maps[i].ptr && stub_maps[i].buffer == buf)
    {
#ifdef DEBUG_VERBOSE
      log_console("LIVE MAP id=%u buf=%u target=0x%x len=%zu", stub_maps[i].id,
                  stub_maps[i].buffer, stub_maps[i].target,
                  (size_t)stub_maps[i].length);
#endif
      m = &stub_maps[i];
      break;
    }
  }

#ifdef DEBUG_VERBOSE
  log_console("glUnmapBuffer: target=0x%x m=%p access=0x%x len=%zu buf=%d",
              target, m, m ? m->access : 0, m ? (size_t)m->length : 0, buf);
#endif

  setup_scalar(OP_glUnmapBuffer);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, m ? m->id : 0);
  C->args_len = W.pos;

  if (m)
  {
    /* If client used explicit flush, do NOT send the full buffer */
    if (m->access & GL_MAP_FLUSH_EXPLICIT_BIT)
    {
#ifdef DEBUG_VERBOSE
      log_console("glUnmapBuffer: skipping as GL_MAP_FLUSH_EXPLICIT_BIT set");
#endif
      C->data_size = 0;
      C->data_offset = 0;
    }
    else
    {
      size_t n = (size_t)m->length;
      if (n > GLES_BRIDGE_DATA_SIZE)
      {
        log_error("glUnmapBuffer: map too large (%zu > %u), clamping", n,
                  GLES_BRIDGE_DATA_SIZE);
        n = GLES_BRIDGE_DATA_SIZE;
      }
      /* Write the current contents of the mapped region into fresh
       * shared memory space. m->ptr is the CPU pointer the client wrote
       * into; we must re-copy it here because earlier bridge calls have
       * overwritten whatever was at m->ptr's old data SHM offset. */
      C->data_offset = bridge_data_write(m->ptr, n);
      C->data_size = n;
    }
  }
  else
  {
    log_error("glUnmapBuffer: target=0x%04x - stub map not found", target);
    C->data_size = 0;
#ifdef DEBUG_ABORT_ON_GL_ERROR
    abort();
#endif
  }

  GLboolean ret = BRIDGE_SEND_CALL();

  if (m)
  {
    if (m->ptr)
    {
#ifdef DEBUG_VERBOSE
      log_console("glUnmapBuffer: free m->ptr: %p", m->ptr);
#endif
      free(m->ptr);
    }
    m->ptr = NULL;
    m->length = 0;
    m->offset = 0;
    m->buffer = 0;
    m->target = 0;
    m->id = 0;
    m->access = 0;
  }

  return ret;
}

GL_APICALL void GL_APIENTRY glTexImage3D(GLenum target, GLint level,
                                         GLint internalformat, GLsizei width,
                                         GLsizei height, GLsizei depth,
                                         GLint border, GLenum format,
                                         GLenum type, const void *pixels)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glTexImage3D;

  size_t bytes = gl_unpack_image_size_3d(width, height, depth, format, type);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  uint32_t pixel_mode;
  if (!pixels)
    pixel_mode = 0; /* NULL */
  else if (stub_gl_state.pixel_unpack_buffer)
    pixel_mode = 1; /* PBO offset */
  else
    pixel_mode = 2; /* shared memory */

  aw_u32(&W, pixel_mode);
  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_i32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_i32(&W, depth);
  aw_i32(&W, border);
  aw_u32(&W, format);
  aw_u32(&W, type);

  C->args_len = W.pos;

  if (!pixels)
  {
    C->data_offset = 0;
    C->data_size = 0;
  }
  else if (stub_gl_state.pixel_unpack_buffer)
  {
    C->data_offset = (uint32_t)(uintptr_t)pixels;
    C->data_size = 0;
  }
  else
  {
    C->data_offset = bridge_data_write(pixels, bytes);
    C->data_size = bytes;
  }

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glTexSubImage3D(GLenum target, GLint level,
                                            GLint xoffset, GLint yoffset,
                                            GLint zoffset, GLsizei width,
                                            GLsizei height, GLsizei depth,
                                            GLenum format, GLenum type,
                                            const void *pixels)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glTexSubImage3D;

  size_t bytes = gl_unpack_image_size_3d(width, height, depth, format, type);

#ifdef DEBUG_VERBOSE
  log_console(
      "glTexSubImage3D: target=0x%04x level=%d xo=%d yo=%d zo=%d w=%d "
      "h=%d d=%d fmt=0x%04x type=0x%04x data_size=%zu pixel_unpack_buffer=%d",
      target, level, xoffset, yoffset, zoffset, width, height, depth, format,
      type, bytes, stub_gl_state.pixel_unpack_buffer);
#endif

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_i32(&W, xoffset);
  aw_i32(&W, yoffset);
  aw_i32(&W, zoffset);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_i32(&W, depth);
  aw_u32(&W, format);
  aw_u32(&W, type);

  C->args_len = W.pos;

  if (stub_gl_state.pixel_unpack_buffer)
  {
    C->data_offset = (uint32_t)(uintptr_t)pixels;
    C->data_size = 0; // signal PBO mode
  }
  else
  {
    C->data_offset = bridge_data_write(pixels, bytes);
    C->data_size = bytes;
  }

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY
glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                       GLsizei width, GLsizei height, GLsizei depth,
                       GLint border, GLsizei imageSize, const void *data)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glCompressedTexImage3D;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);

  aw_i32(&W, level);

  aw_u32(&W, internalformat);

  aw_i32(&W, width);

  aw_i32(&W, height);

  aw_i32(&W, depth);

  aw_i32(&W, border);

  aw_i32(&W, imageSize);

  C->args_len = W.pos;

  GLint pixel_mode;
  if (!data)
  {
    pixel_mode = 0;
    C->data_offset = 0;
    C->data_size = 0;
  }
  else if (stub_gl_state.pixel_unpack_buffer)
  {
    pixel_mode = 1;
    C->data_offset = (uint32_t)(uintptr_t)data;
    C->data_size = 0;
  }
  else
  {
    pixel_mode = 2;
    C->data_offset = bridge_data_write(data, imageSize);
    C->data_size = imageSize;
  }

  aw_i32(&W, pixel_mode);

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glCompressedTexSubImage3D(
    GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset,
    GLsizei width, GLsizei height, GLsizei depth, GLenum format,
    GLsizei imageSize, const void *data)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glCompressedTexSubImage3D;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);

  aw_i32(&W, level);

  aw_i32(&W, xoffset);

  aw_i32(&W, yoffset);

  aw_i32(&W, zoffset);

  aw_i32(&W, width);

  aw_i32(&W, height);

  aw_i32(&W, depth);

  aw_u32(&W, format);

  aw_i32(&W, imageSize);

  C->args_len = W.pos;

  GLint pixel_mode;
  if (!data)
  {
    pixel_mode = 0;
    C->data_offset = 0;
    C->data_size = 0;
  }
  else if (stub_gl_state.pixel_unpack_buffer)
  {
    pixel_mode = 1;
    C->data_offset = (uint32_t)(uintptr_t)data;
    C->data_size = 0;
  }
  else
  {
    pixel_mode = 2;
    C->data_offset = bridge_data_write(data, imageSize);
    C->data_size = imageSize;
  }

  aw_i32(&W, pixel_mode);

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glCopyTexSubImage3D(GLenum target, GLint level,
                                                GLint xoffset, GLint yoffset,
                                                GLint zoffset, GLint x, GLint y,
                                                GLsizei width, GLsizei height)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glCopyTexSubImage3D);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_i32(&W, xoffset);
  aw_i32(&W, yoffset);
  aw_i32(&W, zoffset);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, width);
  aw_i32(&W, height);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL const GLubyte *GL_APIENTRY glGetStringi(GLenum name, GLuint index)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetStringi;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, name);
  aw_u32(&W, index);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  /* Proxy writes null‑terminated string into result_buf */
  BridgeCtrl *C2 = BRIDGE_CTRL();
  return (const GLubyte *)C2->result_buf;
}

GL_APICALL void GL_APIENTRY glGetInteger64v(GLenum pname, GLint64 *data)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetInteger64v;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  if (C2->result_buf_len >= sizeof(GLint64))
    memcpy(data, C2->result_buf, sizeof(GLint64));
  else
    *data = 0;
}

GL_APICALL GLint GL_APIENTRY glGetFragDataLocation(GLuint program,
                                                   const GLchar *name)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetFragDataLocation;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);

  // Copy the string name into args buffer
  size_t len = strlen(name) + 1;
  if (len > BRIDGE_ARGS_SIZE - W.pos)
    len = BRIDGE_ARGS_SIZE - W.pos;

  memcpy(C->args + W.pos, name, len);
  C->args_len = W.pos + len;

  BRIDGE_SEND_CALL();

  // Result is a 32‑bit integer in C->result
  return (GLint)(int32_t)C->result;
}

/* ───────── Indexed boolean state ───────── */

GL_APICALL void GL_APIENTRY glGetBooleani_v(GLenum target, GLuint index,
                                            GLboolean *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGetBooleani_v);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, index);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
  BridgeCtrl *C2 = BRIDGE_CTRL();

  if (C2->result_buf_len >= sizeof(GLboolean))
    memcpy(data, C2->result_buf, sizeof(GLboolean));
}

GL_APICALL void GL_APIENTRY glVertexAttribI4i(GLuint index, GLint x, GLint y,
                                              GLint z, GLint w)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttribI4i);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, z);
  aw_i32(&W, w);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glVertexAttribI4ui(GLuint index, GLuint x, GLuint y,
                                               GLuint z, GLuint w)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttribI4ui);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_u32(&W, x);
  aw_u32(&W, y);
  aw_u32(&W, z);
  aw_u32(&W, w);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glVertexAttribI4iv(GLuint index, const GLint *v)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttribI4iv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);

  C->args_len = W.pos;

  C->data_offset = bridge_data_write(v, 4 * sizeof(GLint));
  C->data_size = 4 * sizeof(GLint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glVertexAttribI4uiv(GLuint index, const GLuint *v)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttribI4uiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);

  C->args_len = W.pos;

  C->data_offset = bridge_data_write(v, 4 * sizeof(GLuint));
  C->data_size = 4 * sizeof(GLuint);

  BRIDGE_SEND_VOID();
}

/* ───────── Transform feedback ───────── */

GL_APICALL void GL_APIENTRY glEndTransformFeedback(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glEndTransformFeedback);

  C->args_len = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetTransformFeedbackVarying(
    GLuint program, GLuint index, GLsizei bufSize, GLsizei *length,
    GLsizei *size, GLenum *type, GLchar *name)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGetTransformFeedbackVarying);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, index);
  aw_i32(&W, bufSize);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader R = ar_init(C2->result_buf, BRIDGE_RESULT_SIZE);

  GLsizei outLen = ar_i32(&R);
  GLsizei outSize = ar_i32(&R);
  GLenum outType = ar_u32(&R);

  if (length)
    *length = outLen;
  if (size)
    *size = outSize;
  if (type)
    *type = outType;

  if (name && bufSize > 0)
  {
    size_t copy = (size_t)bufSize;
    if (copy > (size_t)outLen + 1)
      copy = (size_t)outLen + 1;
    memcpy(name, C2->result_buf + R.pos, copy);
  }
}

/* ───────── Unsigned uniform getters/setters ───────── */

GL_APICALL void GL_APIENTRY glGetUniformuiv(GLuint program, GLint location,
                                            GLuint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGetUniformuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, location);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLuint));
}

GL_APICALL void GL_APIENTRY glUniform1ui(GLint location, GLuint v0)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform1ui);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_u32(&W, v0);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniform1uiv(GLint location, GLsizei count,
                                          const GLuint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform1uiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_i32(&W, count);

  C->args_len = W.pos;

  size_t bytes = (size_t)count * sizeof(GLuint);
  C->data_offset = bridge_data_write(value, bytes);
  C->data_size = bytes;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniform2ui(GLint location, GLuint v0, GLuint v1)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform2ui);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_u32(&W, v0);
  aw_u32(&W, v1);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniform2uiv(GLint location, GLsizei count,
                                          const GLuint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform2uiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_i32(&W, count);

  C->args_len = W.pos;

  size_t bytes = (size_t)count * 2 * sizeof(GLuint);
  C->data_offset = bridge_data_write(value, bytes);
  C->data_size = bytes;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniform3ui(GLint location, GLuint v0, GLuint v1,
                                         GLuint v2)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform3ui);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_u32(&W, v0);
  aw_u32(&W, v1);
  aw_u32(&W, v2);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniform3uiv(GLint location, GLsizei count,
                                          const GLuint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform3uiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_i32(&W, count);

  C->args_len = W.pos;

  size_t bytes = (size_t)count * 3 * sizeof(GLuint);
  C->data_offset = bridge_data_write(value, bytes);
  C->data_size = bytes;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniform4ui(GLint location, GLuint v0, GLuint v1,
                                         GLuint v2, GLuint v3)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform4ui);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_u32(&W, v0);
  aw_u32(&W, v1);
  aw_u32(&W, v2);
  aw_u32(&W, v3);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniform4uiv(GLint location, GLsizei count,
                                          const GLuint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glUniform4uiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, location);
  aw_i32(&W, count);

  C->args_len = W.pos;

  size_t bytes = (size_t)count * 4 * sizeof(GLuint);
  C->data_offset = bridge_data_write(value, bytes);
  C->data_size = bytes;

  BRIDGE_SEND_VOID();
}

/* ───────── Integer vertex attrib I pointer / getters ───────── */

GL_APICALL void GL_APIENTRY glVertexAttribIPointer(GLuint index, GLint size,
                                                   GLenum type, GLsizei stride,
                                                   const void *pointer)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glVertexAttribIPointer);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_i32(&W, size);
  aw_u32(&W, type);
  aw_i32(&W, stride);

  /* convert pointer to offset when VBO is bound */
  uint64_t ptr_val =
      (stub_gl_state.array_buffer != 0)
          ? (uint64_t)(uintptr_t)pointer  /* offset */
          : (uint64_t)(uintptr_t)pointer; /* raw pointer (client array) */

  aw_u64(&W, ptr_val);
  aw_u32(&W, stub_gl_state.array_buffer);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();

  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_stub_state[index].size = size;
    g_attrib_stub_state[index].type = type;
    g_attrib_stub_state[index].normalized = GL_FALSE;
    g_attrib_stub_state[index].stride = stride;
    g_attrib_stub_state[index].pointer = (uintptr_t)pointer;
    g_attrib_stub_state[index].vbo = stub_gl_state.array_buffer;
    g_attrib_stub_state[index].integer = GL_TRUE;

    GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index] = g_attrib_stub_state[index];
    vao->attribs[index].integer = GL_TRUE;
  }
  else
    log_error("glVertexAttribIPointer: index:%d > MAX_VERTEX_ATTRIBS!", index);
}

GL_APICALL void GL_APIENTRY glTexStorage3D(GLenum target, GLsizei levels,
                                           GLenum internalformat, GLsizei width,
                                           GLsizei height, GLsizei depth)
{
#ifdef DEBUG_VERBOSE
  log_console("glTexStorage3D: target:%d levels:%d internalFormat:%d width:%d "
              "height:%d depth:%d",
              target, levels, internalformat, width, height, depth);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glTexStorage3D);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, levels);
  aw_u32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_i32(&W, depth);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

/* ───────── Indexed state ───────── */

GL_APICALL void GL_APIENTRY glColorMaski(GLuint index, GLboolean r, GLboolean g,
                                         GLboolean b, GLboolean a)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glColorMaski);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_u32(&W, r);
  aw_u32(&W, g);
  aw_u32(&W, b);
  aw_u32(&W, a);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glEnablei(GLenum cap, GLuint index)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glEnablei);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, cap);
  aw_u32(&W, index);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDisablei(GLenum cap, GLuint index)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDisablei);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, cap);
  aw_u32(&W, index);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL GLboolean GL_APIENTRY glIsEnabledi(GLenum cap, GLuint index)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glIsEnabledi);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, cap);
  aw_u32(&W, index);

  C->args_len = W.pos;

  return (GLboolean)BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGetTexParameterIiv(GLenum target, GLenum pname,
                                                 GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGetTexParameterIiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLint));
}

GL_APICALL void GL_APIENTRY glGetTexParameterIuiv(GLenum target, GLenum pname,
                                                  GLuint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGetTexParameterIuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLuint));
}

GL_APICALL void GL_APIENTRY glTexParameterIiv(GLenum target, GLenum pname,
                                              const GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glTexParameterIiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  /* send up to 4 GLint values */
  C->data_offset = bridge_data_write(params, 4 * sizeof(GLint));
  C->data_size = 4 * sizeof(GLint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glTexParameterIuiv(GLenum target, GLenum pname,
                                               const GLuint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glTexParameterIuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  /* send up to 4 GLuint values */
  C->data_offset = bridge_data_write(params, 4 * sizeof(GLuint));
  C->data_size = 4 * sizeof(GLuint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetVertexAttribIiv(GLuint index, GLenum pname,
                                                 GLint *params)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetVertexAttribIiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLint));
}

GL_APICALL void GL_APIENTRY glGetVertexAttribIuiv(GLuint index, GLenum pname,
                                                  GLuint *params)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetVertexAttribIuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLuint));
}

GL_APICALL void GL_APIENTRY glGetQueryiv(GLenum target, GLenum pname,
                                         GLint *params)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetQueryiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLint));
}

GL_APICALL void GL_APIENTRY glGetQueryObjectuiv(GLuint id, GLenum pname,
                                                GLuint *params)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetQueryObjectuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, id);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLuint));
}

GL_APICALL GLboolean GL_APIENTRY glIsQuery(GLuint id)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glIsQuery);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, id);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  return (GLboolean)C2->result;
}

GL_APICALL void GL_APIENTRY glBindBufferBase(GLenum target, GLuint index,
                                             GLuint buffer)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindBufferBase);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, index);
  aw_u32(&W, buffer);

  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBindBufferRange(GLenum target, GLuint index,
                                              GLuint buffer, GLintptr offset,
                                              GLsizeiptr size)
{
#ifdef DEBUG_VERBOSE
  log_console("glBindBufferRange: target=0x%04x index=%u buffer=%u "
              "offset=0x%llx size=0x%llx g_stub_current_ctx=%d",
              target, index, buffer, (unsigned long long)offset,
              (unsigned long long)size, g_stub_current_ctx);
#endif

#ifdef CACHE_GL_STATE
  if (target == GL_UNIFORM_BUFFER && index < BUFRANGE_CACHE_SLOTS)
  {
    BufferRangeCacheEntry *ce = &g_bufrange_cache[g_stub_current_ctx][index];
    StubMapEntry *m = find_stub_map(buffer, GL_UNIFORM_BUFFER);
    bool unsynchronized_mapped = m && (m->access & GL_MAP_WRITE_BIT) &&
                                 !(m->access & GL_MAP_FLUSH_EXPLICIT_BIT);

    if (ce->buffer == buffer && ce->offset == offset && ce->size == size)
    {
      if (!unsynchronized_mapped)
        return; /* params unchanged, and nothing observable could have
                    changed the content without invalidating this entry */

      GLintptr local_off = offset - m->offset;
      if (local_off >= 0 &&
          (size_t)local_off + (size_t)size <= (size_t)m->length)
      {
        uint32_t h =
            fnv1a_hash((const uint8_t *)m->ptr + local_off, (size_t)size);
        if (ce->hash_valid && h == ce->content_hash)
          return; /* content unchanged since last send */
        ce->content_hash = h;
        ce->hash_valid = 1;
      }
      /* range doesn't fit the map — fall through and send normally */
    }
    else
    {
      ce->buffer = buffer;
      ce->offset = offset;
      ce->size = size;
      ce->hash_valid = 0;

      if (unsynchronized_mapped)
      {
        GLintptr local_off = offset - m->offset;
        if (local_off >= 0 &&
            (size_t)local_off + (size_t)size <= (size_t)m->length)
        {
          ce->content_hash =
              fnv1a_hash((const uint8_t *)m->ptr + local_off, (size_t)size);
          ce->hash_valid = 1;
        }
      }
    }
  }
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindBufferRange);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, index);
  aw_u32(&W, buffer);
  aw_i64(&W, (int64_t)offset);
  aw_i64(&W, (int64_t)size);

  C->args_len = W.pos;

  /* If this buffer has an active persistent write map (no
   * GL_MAP_FLUSH_EXPLICIT_BIT), the GPU mapped range is never updated via
   * unmap/flush.  Piggyback the sub-range the shader will actually read so the
   * proxy can copy it into the real GPU-mapped buffer before calling
   * glBindBufferRange. */
  C->data_offset = 0;
  C->data_size = 0;
  for (uint32_t i = 1; i < MAX_MAPS; i++)
  {
    StubMapEntry *m = &stub_maps[i];
    if (!m->ptr || m->buffer != buffer)
      continue;
    if (m->access & GL_MAP_FLUSH_EXPLICIT_BIT)
      break; /* explicit-flush maps handle their own updates */
    if (!(m->access & GL_MAP_WRITE_BIT))
      break;
    /* offset is buffer-relative; m->offset is the map start */
    GLintptr map_start = m->offset;
    GLintptr range_end = offset + size;
    GLintptr map_end = map_start + (GLintptr)m->length;
    if (offset >= map_start && range_end <= map_end)
    {
      /* Copy just [offset, offset+size) from the stub's malloc'd buffer */
      uint8_t *src = (uint8_t *)m->ptr + (offset - map_start);
      C->data_offset = bridge_data_write(src, (size_t)size);
      C->data_size = (uint32_t)size;
    }
    break;
  }

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetBufferPointerv(GLenum target, GLenum pname,
                                                void **params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetBufferPointerv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader R = ar_init(C2->result_buf, C2->result_buf_len);

  uintptr_t ptr = (uintptr_t)ar_u64(&R);
  *params = (void *)ptr;
}

GL_APICALL void GL_APIENTRY glGenSamplers(GLsizei count, GLuint *samplers)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGenSamplers);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, count);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(samplers, C2->result_buf, count * sizeof(GLuint));
}

GL_APICALL void GL_APIENTRY glDeleteSamplers(GLsizei count,
                                             const GLuint *samplers)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDeleteSamplers);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, count);
  C->args_len = W.pos;

  C->data_offset = bridge_data_write(samplers, count * sizeof(GLuint));
  C->data_size = count * sizeof(GLuint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glSamplerParameteri(GLuint sampler, GLenum pname,
                                                GLint param)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glSamplerParameteri);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  aw_i32(&W, param);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glSamplerParameteriv(GLuint sampler, GLenum pname,
                                                 const GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glSamplerParameteriv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  C->data_offset = bridge_data_write(params, 4 * sizeof(GLint));
  C->data_size = 4 * sizeof(GLint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glSamplerParameterf(GLuint sampler, GLenum pname,
                                                GLfloat param)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glSamplerParameterf);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  aw_f32(&W, param);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glSamplerParameterfv(GLuint sampler, GLenum pname,
                                                 const GLfloat *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glSamplerParameterfv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  C->data_offset = bridge_data_write(params, 4 * sizeof(GLfloat));
  C->data_size = 4 * sizeof(GLfloat);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetSamplerParameteriv(GLuint sampler,
                                                    GLenum pname, GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetSamplerParameteriv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, 4 * sizeof(GLint));
}

GL_APICALL void GL_APIENTRY glGetSamplerParameterfv(GLuint sampler,
                                                    GLenum pname,
                                                    GLfloat *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetSamplerParameterfv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, 4 * sizeof(GLfloat));
}

GL_APICALL GLboolean GL_APIENTRY glIsSampler(GLuint sampler)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glIsSampler);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  return (GLboolean)C2->result;
}

GL_APICALL GLboolean GL_APIENTRY glIsSync(GLsync sync)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glIsSync);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, (uint32_t)(uintptr_t)sync);
  C->args_len = W.pos;

  return (GLboolean)BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glWaitSync(GLsync sync, GLbitfield flags,
                                       GLuint64 timeout)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glWaitSync);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, (uint32_t)(uintptr_t)sync);
  aw_u32(&W, flags);
  aw_u64(&W, timeout);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetSynciv(GLsync sync, GLenum pname,
                                        GLsizei bufSize, GLsizei *length,
                                        GLint *values)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glGetSynciv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, (uint32_t)(uintptr_t)sync);
  aw_u32(&W, pname);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader R = ar_init(C2->result_buf, C2->result_buf_len);

  GLsizei outLen = ar_i32(&R);
  if (length)
    *length = outLen;

  if (values && outLen > 0)
    memcpy(values, C2->result_buf + sizeof(GLsizei), outLen * sizeof(GLint));
}

GL_APICALL void GL_APIENTRY glBeginTransformFeedback(GLenum primitiveMode)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBeginTransformFeedback);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, primitiveMode);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY
glTransformFeedbackVaryings(GLuint program, GLsizei count,
                            const GLchar *const *varyings, GLenum bufferMode)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glTransformFeedbackVaryings);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, count);
  aw_u32(&W, bufferMode);
  C->args_len = W.pos;

  /* pack all strings into one contiguous block in data shm */
  size_t total = 0;
  for (GLsizei i = 0; i < count; i++)
    total += strlen(varyings[i]) + 1; /* include NUL */

  char *buf = (char *)alloca(total);
  char *p = buf;
  for (GLsizei i = 0; i < count; i++)
  {
    size_t len = strlen(varyings[i]) + 1;
    memcpy(p, varyings[i], len);
    p += len;
  }

  uint32_t off = bridge_data_write(buf, total);
  C->data_offset = off;
  C->data_size = (uint32_t)total;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL GLuint GL_APIENTRY
glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetUniformBlockIndex);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  C->args_len = W.pos;

  uint32_t len = strlen(uniformBlockName) + 1;
  C->data_offset = bridge_data_write(uniformBlockName, len);
  C->data_size = len;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  return (GLuint)C2->result;
}

GL_APICALL void GL_APIENTRY glUniformBlockBinding(GLuint program,
                                                  GLuint uniformBlockIndex,
                                                  GLuint uniformBlockBinding)
{
#ifdef DEBUG_VERBOSE
  log_console("glUniformBlockBinding: program: %d uniformBlockIndex: %d "
              "uniformBlockBinding: %d",
              program, uniformBlockIndex, uniformBlockBinding);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUniformBlockBinding);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, uniformBlockIndex);
  aw_u32(&W, uniformBlockBinding);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glRenderbufferStorageMultisample(
    GLenum target, GLsizei samples, GLenum internalformat, GLsizei width,
    GLsizei height)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glRenderbufferStorageMultisample);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, samples);
  aw_u32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBlitFramebuffer(GLint srcX0, GLint srcY0,
                                              GLint srcX1, GLint srcY1,
                                              GLint dstX0, GLint dstY0,
                                              GLint dstX1, GLint dstY1,
                                              GLbitfield mask, GLenum filter)
{
#ifdef DEBUG_VERBOSE
  log_console("[glBlitFramebuffer] "
              "src=(%d,%d)->(%d,%d) dst=(%d,%d)->(%d,%d) "
              "mask=0x%08x filter=0x%04x",
              srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask,
              filter);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBlitFramebuffer);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, srcX0);
  aw_i32(&W, srcY0);
  aw_i32(&W, srcX1);
  aw_i32(&W, srcY1);
  aw_i32(&W, dstX0);
  aw_i32(&W, dstY0);
  aw_i32(&W, dstX1);
  aw_i32(&W, dstY1);
  aw_u32(&W, mask);
  aw_u32(&W, filter);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glFramebufferTextureLayer(GLenum target,
                                                      GLenum attachment,
                                                      GLuint texture,
                                                      GLint level, GLint layer)
{
#ifdef DEBUG_VERBOSE
  log_console(
      "glFramebufferTextureLayer: target=0x%x att=0x%x tex=%u lv=%d layer=%d",
      target, attachment, texture, level, layer);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFramebufferTextureLayer);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, attachment);
  aw_u32(&W, texture);
  aw_i32(&W, level);
  aw_i32(&W, layer);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGenTransformFeedbacks(GLsizei n, GLuint *ids)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGenTransformFeedbacks);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(ids, C2->result_buf, n * sizeof(GLuint));
}

GL_APICALL void GL_APIENTRY glDeleteTransformFeedbacks(GLsizei n,
                                                       const GLuint *ids)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDeleteTransformFeedbacks);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;

  C->data_offset = bridge_data_write(ids, n * sizeof(GLuint));
  C->data_size = n * sizeof(GLuint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBindTransformFeedback(GLenum target, GLuint id)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindTransformFeedback);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, id);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glPauseTransformFeedback(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glPauseTransformFeedback);
  C->args_len = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glResumeTransformFeedback(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glResumeTransformFeedback);
  C->args_len = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL GLboolean GL_APIENTRY glIsTransformFeedback(GLuint id)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glIsTransformFeedback);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, id);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  return (GLboolean)C2->result;
}

GL_APICALL void GL_APIENTRY glCopyBufferSubData(GLenum readTarget,
                                                GLenum writeTarget,
                                                GLintptr readOffset,
                                                GLintptr writeOffset,
                                                GLsizeiptr size)
{
#ifdef CACHE_GL_STATE
  bufrange_cache_invalidate_buffer(get_bound_buffer(writeTarget));
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCopyBufferSubData);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, readTarget);
  aw_u32(&W, writeTarget);
  aw_u64(&W, (uint64_t)readOffset);
  aw_u64(&W, (uint64_t)writeOffset);
  aw_u64(&W, (uint64_t)size);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDrawElementsInstanced(GLenum mode, GLsizei count,
                                                    GLenum type,
                                                    const void *indices,
                                                    GLsizei instanceCount)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawElementsInstanced);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mode);
  aw_i32(&W, count);
  aw_u32(&W, type);
  aw_u32(&W, instanceCount);
  C->args_len = W.pos;

  if (is_client_pointer(indices))
  {
    size_t elem = (type == GL_UNSIGNED_SHORT ? 2
                   : type == GL_UNSIGNED_INT ? 4
                                             : 1);
    size_t sz = count * elem;
    C->data_offset = bridge_data_write(indices, sz);
    C->data_size = sz;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY
glGetUniformIndices(GLuint program, GLsizei uniformCount,
                    const GLchar *const *uniformNames, GLuint *uniformIndices)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetUniformIndices);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, uniformCount);
  C->args_len = W.pos;

  // pack names
  size_t total = 0;
  for (int i = 0; i < uniformCount; i++)
    total += strlen(uniformNames[i]) + 1;

  uint8_t *buf = alloca(total);
  uint8_t *p = buf;
  for (int i = 0; i < uniformCount; i++)
  {
    size_t len = strlen(uniformNames[i]) + 1;
    memcpy(p, uniformNames[i], len);
    p += len;
  }

  C->data_offset = bridge_data_write(buf, total);
  C->data_size = total;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(uniformIndices, C2->result_buf, uniformCount * sizeof(GLuint));
}

GL_APICALL void GL_APIENTRY glGetActiveUniformsiv(GLuint program,
                                                  GLsizei uniformCount,
                                                  const GLuint *uniformIndices,
                                                  GLenum pname, GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetActiveUniformsiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, uniformCount);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  C->data_offset =
      bridge_data_write(uniformIndices, uniformCount * sizeof(GLuint));
  C->data_size = uniformCount * sizeof(GLuint);

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, uniformCount * sizeof(GLint));
}

GL_APICALL void GL_APIENTRY glGetActiveUniformBlockiv(GLuint program,
                                                      GLuint blockIndex,
                                                      GLenum pname,
                                                      GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetActiveUniformBlockiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, blockIndex);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, sizeof(GLint));
}

GL_APICALL void GL_APIENTRY glGetActiveUniformBlockName(GLuint program,
                                                        GLuint blockIndex,
                                                        GLsizei bufSize,
                                                        GLsizei *length,
                                                        GLchar *blockName)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetActiveUniformBlockName);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, blockIndex);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  GLsizei len = ar_i32(&r);
  if (length)
    *length = len;

  memcpy(blockName, C2->result_buf + sizeof(GLsizei), len);
}

GL_APICALL void GL_APIENTRY glBindImageTexture(GLuint unit, GLuint texture,
                                               GLint level, GLboolean layered,
                                               GLint layer, GLenum access,
                                               GLenum format)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindImageTexture);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, unit);
  aw_u32(&W, texture);
  aw_i32(&W, level);
  aw_u32(&W, layered);
  aw_i32(&W, layer);
  aw_u32(&W, access);
  aw_u32(&W, format);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDrawRangeElements(GLenum mode, GLuint start,
                                                GLuint end, GLsizei count,
                                                GLenum type,
                                                const void *indices)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawRangeElements);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mode);
  aw_u32(&W, start);
  aw_u32(&W, end);
  aw_i32(&W, count);
  aw_u32(&W, type);
  aw_u64(&W, (uint64_t)(uintptr_t)indices);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glSamplerParameterIiv(GLuint sampler, GLenum pname,
                                                  const GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glSamplerParameterIiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, sampler);
  aw_u32(&W, pname);

  /* Pack 4 GLint values (GLES spec: 1–4 depending on pname) */
  aw_i32(&W, params[0]);
  aw_i32(&W, params[1]);
  aw_i32(&W, params[2]);
  aw_i32(&W, params[3]);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGetPointerv(GLenum pname, void **params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetPointerv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  uintptr_t ptr = (uintptr_t)ar_u64(&r);
  *params = (void *)ptr;
}

GL_APICALL void GL_APIENTRY glPopDebugGroup(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glPopDebugGroup);

  C->args_len = 0;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGetProgramBinary(GLuint program, GLsizei bufSize,
                                               GLsizei *length,
                                               GLenum *binaryFormat,
                                               void *binary)
{
#ifdef DEBUG_SHADERS
  log_console("glGetProgramBinary: program:%d bufSize:%d length=%d "
              "binaryFormat:%d binary:%p",
              program, bufSize, length, binaryFormat, binary);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetProgramBinary);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  /* Allocate output space in the shared data region — no src to copy */
  uint32_t out = bridge_data_write(NULL, (size_t)bufSize);
  C->data_offset = out;
  C->data_size = (uint32_t)bufSize;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  /* result_buf now carries only the two small scalars (8 bytes) */
  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);
  GLsizei len = ar_i32(&r);
  GLenum fmt = ar_u32(&r);

  if (length)
    *length = len;
  if (binaryFormat)
    *binaryFormat = fmt;

  /* Copy actual binary out of the shared region */
  if (binary && len > 0)
    bridge_data_read(binary, out, (size_t)len);

#ifdef DEBUG_SHADERS
  log_console("glGetProgramBinary: program:%d bufSize:%d len=%d fmt:%d",
              program, bufSize, len, fmt);
#endif
}

GL_APICALL void GL_APIENTRY glReadBuffer(GLenum src)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glReadBuffer);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, src);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glDrawBuffers(GLsizei n, const GLenum *bufs)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawBuffers);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);

  for (int i = 0; i < n; i++)
    aw_u32(&W, bufs[i]);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glClearBufferfi(GLenum buffer, GLint drawbuffer,
                                            GLfloat depth, GLint stencil)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClearBufferfi);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, buffer);
  aw_i32(&W, drawbuffer);
  aw_f32(&W, depth);
  aw_i32(&W, stencil);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glClearBufferfv(GLenum buffer, GLint drawbuffer,
                                            const GLfloat *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClearBufferfv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, buffer);
  aw_i32(&W, drawbuffer);

  aw_f32(&W, value[0]);
  aw_f32(&W, value[1]);
  aw_f32(&W, value[2]);
  aw_f32(&W, value[3]);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glClearBufferiv(GLenum buffer, GLint drawbuffer,
                                            const GLint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClearBufferiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, buffer);
  aw_i32(&W, drawbuffer);

  aw_i32(&W, value[0]);
  aw_i32(&W, value[1]);
  aw_i32(&W, value[2]);
  aw_i32(&W, value[3]);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glClearBufferuiv(GLenum buffer, GLint drawbuffer,
                                             const GLuint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClearBufferuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, buffer);
  aw_i32(&W, drawbuffer);

  aw_u32(&W, value[0]);
  aw_u32(&W, value[1]);
  aw_u32(&W, value[2]);
  aw_u32(&W, value[3]);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glTexBuffer(GLenum target, GLenum internalformat,
                                        GLuint buffer)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glTexBuffer);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, internalformat);
  aw_u32(&W, buffer);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetBufferParameteri64v(GLenum target,
                                                     GLenum pname,
                                                     GLint64 *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetBufferParameteri64v);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  *params = (GLint64)ar_u64(&r);
}

GL_APICALL void GL_APIENTRY glGetInteger64i_v(GLenum target, GLuint index,
                                              GLint64 *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetInteger64i_v);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, index);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  *data = (GLint64)ar_u64(&r);
}

GL_APICALL void GL_APIENTRY glFramebufferTexture(GLenum target,
                                                 GLenum attachment,
                                                 GLuint texture, GLint level)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFramebufferTexture);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, attachment);
  aw_u32(&W, texture);
  aw_i32(&W, level);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGetInternalformativ(GLenum target,
                                                  GLenum internalformat,
                                                  GLenum pname, GLsizei bufSize,
                                                  GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetInternalformativ);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, internalformat);
  aw_u32(&W, pname);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  for (int i = 0; i < bufSize; i++)
    params[i] = ar_i32(&r);
}

GL_APICALL void GL_APIENTRY glGetIntegeri_v(GLenum target, GLuint index,
                                            GLint *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetIntegeri_v);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, index);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  *data = ar_i32(&r);
}

GL_APICALL void GL_APIENTRY glGetSamplerParameterIiv(GLuint sampler,
                                                     GLenum pname,
                                                     GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetSamplerParameterIiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  params[0] = ar_i32(&r);
  params[1] = ar_i32(&r);
  params[2] = ar_i32(&r);
  params[3] = ar_i32(&r);
}

GL_APICALL void GL_APIENTRY glGetSamplerParameterIuiv(GLuint sampler,
                                                      GLenum pname,
                                                      GLuint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetSamplerParameterIuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sampler);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  params[0] = ar_u32(&r);
  params[1] = ar_u32(&r);
  params[2] = ar_u32(&r);
  params[3] = ar_u32(&r);
}

GL_APICALL void GL_APIENTRY glSamplerParameterIuiv(GLuint sampler, GLenum pname,
                                                   const GLuint *param)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glSamplerParameterIuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, sampler);
  aw_u32(&W, pname);

  aw_u32(&W, param[0]);
  aw_u32(&W, param[1]);
  aw_u32(&W, param[2]);
  aw_u32(&W, param[3]);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glProgramBinary(GLuint program, GLenum binaryFormat,
                                            const void *binary, GLsizei length)
{
#ifdef DEBUG_VERBOSE
  log_console("glProgramBinary program:%d binaryFormat:%d binary:%p length:%d",
              program, binaryFormat, binary, length);

  uint32_t crc = 0;
  for (int i = 0; i < length; i++)
    crc += ((const uint8_t *)binary)[i];
  log_console("glProgramBinary stub: len=%d crc=0x%08x", length, crc);
#endif

#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
  loc_cache_invalidate_program(program);
#endif

  if ((size_t)length > GLES_BRIDGE_DATA_SIZE)
  {
    log_error("glProgramBinary: binary too large for bridge: %d > %zu", length,
              GLES_BRIDGE_DATA_SIZE);
    // return;
  }

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glProgramBinary);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, binaryFormat);
  aw_i32(&W, length);
  C->args_len = W.pos;

  /* put binary into shared data region */
  if (binary && length > 0)
  {
    C->data_offset = bridge_data_write(binary, (size_t)length);
    C->data_size = (uint32_t)length;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glProgramParameteri(GLuint program, GLenum pname,
                                                GLint value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glProgramParameteri);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, pname);
  aw_i32(&W, value);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glTexStorage3DMultisample(
    GLenum target, GLsizei samples, GLenum internalformat, GLsizei width,
    GLsizei height, GLsizei depth, GLboolean fixedsamplelocations)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glTexStorage3DMultisample);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);
  aw_i32(&W, samples);
  aw_u32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_i32(&W, depth);
  aw_u32(&W, fixedsamplelocations);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glDrawElementsBaseVertex(GLenum mode, GLsizei count,
                                                     GLenum type,
                                                     const void *indices,
                                                     GLint basevertex)
{
  GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
  VAOState *vao = &ctx->vaos[ctx->current_vao];

#ifdef DEBUG_VERBOSE
  log_console("glDrawElementsBaseVertex: mode=0x%04x count=%d type=0x%04x "
              "indices=0x%lx basevertex=%d index_mode=%d g_stub_current_ctx=%d",
              mode, count, type, (unsigned long)(uintptr_t)indices, basevertex,
              vao->ebo != 0 ? IDX_MODE_OFFSET : IDX_MODE_POINTER,
              g_stub_current_ctx);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawElementsBaseVertex);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, mode);
  aw_i32(&W, count);
  aw_u32(&W, type);

  uint32_t index_mode = 0;
  uint32_t ebo_piggyback_buffer = 0;
  uint32_t ebo_piggyback_offset = 0;
  uint32_t ebo_piggyback_length = 0;
  const uint8_t *index_bytes = NULL;

  if (vao->ebo != 0)
  {
#ifdef DEBUG_VERBOSE
    log_console("glDrawElementsBaseVertex ebo mode");
#endif
    index_mode = IDX_MODE_OFFSET;
    aw_u64(&W, (uint64_t)(uintptr_t)indices);
    C->data_size = 0;

    size_t index_size = index_type_size(type);
    size_t index_bytes_len = (size_t)count * index_size;
    StubMapEntry *ebo_map = find_stub_map(vao->ebo, GL_ELEMENT_ARRAY_BUFFER);

    if (index_size && ebo_map &&
        !(ebo_map->access & GL_MAP_FLUSH_EXPLICIT_BIT) &&
        (ebo_map->access & GL_MAP_WRITE_BIT))
    {
      GLintptr index_offset = (GLintptr)(uintptr_t)indices;
      if (index_offset >= ebo_map->offset &&
          index_offset + (GLintptr)index_bytes_len <=
              ebo_map->offset + ebo_map->length)
      {
        GLintptr local_off = index_offset - ebo_map->offset;
        index_bytes = (const uint8_t *)ebo_map->ptr + local_off;

        ebo_piggyback_buffer = vao->ebo;
        ebo_piggyback_offset = (uint32_t)index_offset;
        ebo_piggyback_length = (uint32_t)index_bytes_len;
        C->data_offset = bridge_data_write(index_bytes, index_bytes_len);
        C->data_size = (uint32_t)index_bytes_len;
      }
    }
  }
  else
  {
#ifdef DEBUG_VERBOSE
    log_console("glDrawElementsBaseVertex alt mode");
#endif
    index_mode = IDX_MODE_POINTER;

    size_t index_size = index_type_size(type);
    size_t bytes = count * index_size;

    uint32_t off = bridge_data_write(indices, bytes);
    C->data_size = bytes;
    index_bytes = (const uint8_t *)indices;

    aw_u64(&W, off);
  }

  aw_i32(&W, basevertex);
  aw_u32(&W, index_mode);
  aw_u32(&W, ebo_piggyback_buffer);
  aw_u32(&W, ebo_piggyback_offset);
  aw_u32(&W, ebo_piggyback_length);

  /* Piggyback any persistently-mapped GL_ARRAY_BUFFER used by an enabled
   * VBO-backed attrib in the current VAO. */
  uint32_t piggyback_buffer = 0;
  uint32_t piggyback_offset = 0; /* buffer-relative start of copied range */
  uint32_t piggyback_length = 0; /* bytes copied                        */
  uint32_t piggyback_data_off = 0;
  GLintptr min_vertex = 0;
  GLintptr max_vertex = 0;
  bool have_index_bounds = mapped_index_bounds(
      index_bytes, count, type, basevertex, &min_vertex, &max_vertex);

  for (int ai = 0; ai < MAX_VERTEX_ATTRIBS && !piggyback_buffer; ai++)
  {
    AttribState *a = &vao->attribs[ai];
    if (!a->enabled || a->vbo == 0)
      continue;

    for (uint32_t mi = 1; mi < MAX_MAPS; mi++)
    {
      StubMapEntry *m = &stub_maps[mi];
      if (!m->ptr || m->buffer != (GLuint)a->vbo)
        continue;
      if (m->access & GL_MAP_FLUSH_EXPLICIT_BIT)
        break; /* explicit-flush maps handle their own updates */
      if (!(m->access & GL_MAP_WRITE_BIT))
        break;

      /* Found a persistently-mapped VBO backing this attrib */
      size_t stride = a->stride ? (size_t)a->stride : 1;
      GLintptr first_vertex = have_index_bounds
                                  ? min_vertex
                                  : (GLintptr)(basevertex > 0 ? basevertex : 0);
      GLintptr last_vertex =
          have_index_bounds ? max_vertex : first_vertex + (GLintptr)count;

      GLintptr byte_start =
          (GLintptr)a->pointer + first_vertex * (GLintptr)stride;
      GLintptr byte_end =
          (GLintptr)a->pointer + last_vertex * (GLintptr)stride +
          (GLintptr)((size_t)a->size * attrib_type_size(a->type));

      for (int aj = 0; aj < MAX_VERTEX_ATTRIBS; aj++)
      {
        AttribState *b = &vao->attribs[aj];
        if (!b->enabled || b->vbo != a->vbo)
          continue;

        size_t b_stride = b->stride ? (size_t)b->stride : 1;
        GLintptr b_start =
            (GLintptr)b->pointer + first_vertex * (GLintptr)b_stride;
        GLintptr b_end =
            (GLintptr)b->pointer + last_vertex * (GLintptr)b_stride +
            (GLintptr)((size_t)b->size * attrib_type_size(b->type));

        if (b_start < byte_start)
          byte_start = b_start;
        if (b_end > byte_end)
          byte_end = b_end;
      }

      if (byte_start < 0)
        byte_start = 0;
      if (byte_end <= byte_start)
        break;

      if (byte_start < m->offset || byte_end > m->offset + (GLintptr)m->length)
        break;

      GLintptr local_start = byte_start - m->offset;
      size_t copy_len = (size_t)(byte_end - byte_start);
      if ((size_t)local_start >= (size_t)m->length)
        break; /* draw range entirely outside this map — not the right buffer */
      if ((size_t)local_start + copy_len > (size_t)m->length)
        copy_len = (size_t)m->length - (size_t)local_start;

      piggyback_buffer = a->vbo;
      piggyback_offset = (uint32_t)byte_start;
      piggyback_length = (uint32_t)copy_len;
      piggyback_data_off =
          bridge_data_write((uint8_t *)m->ptr + local_start, copy_len);
      break;
    }
  }

  aw_u32(&W, piggyback_buffer);
  aw_u32(&W, piggyback_offset);
  aw_u32(&W, piggyback_length);

  C->args_len = W.pos;

  if (piggyback_buffer)
  {
    C->data2_offset = piggyback_data_off;
    C->data2_size = piggyback_length;
  }
  else
  {
    C->data2_offset = 0;
    C->data2_size = 0;
  }

#ifdef DEBUG_VERBOSE
  if (piggyback_buffer)
    log_console("glDrawElementsBaseVertex: piggyback VBO=%u off=%u len=%u",
                piggyback_buffer, piggyback_offset, piggyback_length);
#endif

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDrawElementsInstancedBaseVertex(
    GLenum mode, GLsizei count, GLenum type, const void *indices,
    GLsizei instancecount, GLint basevertex)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawElementsInstancedBaseVertex);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, mode);
  aw_i32(&W, count);
  aw_u32(&W, type);
  aw_u64(&W, (uint64_t)(uintptr_t)indices);
  aw_i32(&W, instancecount);
  aw_i32(&W, basevertex);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glDrawRangeElementsBaseVertex(
    GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
    const void *indices, GLint basevertex)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawRangeElementsBaseVertex);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, mode);
  aw_u32(&W, start);
  aw_u32(&W, end);
  aw_i32(&W, count);
  aw_u32(&W, type);
  aw_u64(&W, (uint64_t)(uintptr_t)indices);
  aw_i32(&W, basevertex);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glMinSampleShading(GLfloat value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glMinSampleShading);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, value);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glDebugMessageInsert(GLenum source, GLenum type,
                                                 GLuint id, GLenum severity,
                                                 GLsizei length,
                                                 const char *message)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDebugMessageInsert);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, source);
  aw_u32(&W, type);
  aw_u32(&W, id);
  aw_u32(&W, severity);
  aw_i32(&W, length);
  C->args_len = W.pos;

  if (message)
  {
    if (length < 0)
      length = (GLsizei)strlen(message);

    C->data_offset = bridge_data_write(message, (size_t)length);
    C->data_size = (uint32_t)length;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();
}

GL_APICALL GLuint GL_APIENTRY glGetDebugMessageLog(
    GLuint count, GLsizei bufSize, GLenum *sources, GLenum *types, GLuint *ids,
    GLenum *severities, GLsizei *lengths, GLchar *messageLog)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetDebugMessageLog);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, count);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  GLuint num = ar_u32(&r);

  for (GLuint i = 0; i < num; i++)
  {
    GLenum src = ar_u32(&r);
    GLenum type = ar_u32(&r);
    GLuint id = ar_u32(&r);
    GLenum sev = ar_u32(&r);
    GLsizei len = ar_i32(&r);

    if (sources)
      sources[i] = src;
    if (types)
      types[i] = type;
    if (ids)
      ids[i] = id;
    if (severities)
      severities[i] = sev;
    if (lengths)
      lengths[i] = len;

    if (messageLog && len > 0)
    {
      memcpy(messageLog, C2->result_buf + r.pos, len);
      messageLog += len;
    }

    r.pos += len; /* always advance over the message bytes */
  }

  return num;
}

GL_APICALL void GL_APIENTRY glGetObjectLabel(GLenum identifier, GLuint name,
                                             GLsizei bufSize, GLsizei *length,
                                             GLchar *label)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetObjectLabel);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, identifier);
  aw_u32(&W, name);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  GLsizei len = ar_i32(&r);
  if (length)
    *length = len;

  if (label && len > 0)
    memcpy(label, C2->result_buf + sizeof(GLsizei), len);
}

GL_APICALL void GL_APIENTRY glGetObjectPtrLabel(const void *ptr,
                                                GLsizei bufSize,
                                                GLsizei *length, GLchar *label)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetObjectPtrLabel);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u64(&W, (uint64_t)(uintptr_t)ptr);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  GLsizei len = ar_i32(&r);
  if (length)
    *length = len;

  if (label && len > 0)
    memcpy(label, C2->result_buf + sizeof(GLsizei), len);
}

GL_APICALL void GL_APIENTRY glObjectLabel(GLenum identifier, GLuint name,
                                          GLsizei length, const GLchar *label)
{
#ifdef DEBUG_VERBOSE
  log_console("glObjectLabel identifier:%d name: %d length:%d label: %s",
              identifier, name, length, label);
#endif

#ifndef DEBUG
  (void)identifier;
  (void)name;
  (void)length;
  (void)label;
  return;
#else
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glObjectLabel);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, identifier);
  aw_u32(&W, name);
  aw_i32(&W, length);
  C->args_len = W.pos;

  if (label)
  {
    if (length < 0)
      length = (GLsizei)strlen(label);

    C->data_offset = bridge_data_write(label, (size_t)length);
    C->data_size = (uint32_t)length;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();
#endif
}

GL_APICALL void GL_APIENTRY glPushDebugGroup(GLenum source, GLuint id,
                                             GLsizei length,
                                             const GLchar *message)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glPushDebugGroup);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, source);
  aw_u32(&W, id);
  aw_i32(&W, length);
  C->args_len = W.pos;

  if (message)
  {
    if (length < 0)
      length = (GLsizei)strlen(message);

    C->data_offset = bridge_data_write(message, (size_t)length);
    C->data_size = (uint32_t)length;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY
glCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel, GLint srcX,
                   GLint srcY, GLint srcZ, GLuint dstName, GLenum dstTarget,
                   GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ,
                   GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth)
{
#ifdef DEBUG_VERBOSE
  log_console("glCopyImageSubData: srcName=%u srcTarget=%u srcLevel=%d srcX=%d "
              "srcY=%d srcZ=%d dstName=%u dstTarget=%u dstLevel=%d dstX=%d "
              "dstY=%d dstZ=%d srcWidth=%d srcHeight=%d srcDepth=%d",
              srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName,
              dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight,
              srcDepth);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCopyImageSubData);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, srcName);
  aw_u32(&W, srcTarget);
  aw_i32(&W, srcLevel);
  aw_i32(&W, srcX);
  aw_i32(&W, srcY);
  aw_i32(&W, srcZ);

  aw_u32(&W, dstName);
  aw_u32(&W, dstTarget);
  aw_i32(&W, dstLevel);
  aw_i32(&W, dstX);
  aw_i32(&W, dstY);
  aw_i32(&W, dstZ);

  aw_i32(&W, srcWidth);
  aw_i32(&W, srcHeight);
  aw_i32(&W, srcDepth);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glMemoryBarrier(GLbitfield barriers)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glMemoryBarrier);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, barriers);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDispatchCompute(GLuint num_groups_x,
                                              GLuint num_groups_y,
                                              GLuint num_groups_z)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDispatchCompute);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, num_groups_x);
  aw_u32(&W, num_groups_y);
  aw_u32(&W, num_groups_z);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glDispatchComputeIndirect(GLintptr indirect)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDispatchComputeIndirect);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u64(&W, (uint64_t)indirect);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glTexStorage2DMultisample(
    GLenum target, GLsizei samples, GLenum internalformat, GLsizei width,
    GLsizei height, GLboolean fixedsamplelocations)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glTexStorage2DMultisample);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, target);
  aw_i32(&W, samples);
  aw_u32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_u32(&W, fixedsamplelocations);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glObjectPtrLabel(const void *ptr, GLsizei length,
                                             const GLchar *label)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glObjectPtrLabel);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u64(&W, (uint64_t)(uintptr_t)ptr);
  aw_i32(&W, length);
  C->args_len = W.pos;

  if (label)
  {
    if (length < 0)
      length = (GLsizei)strlen(label);

    C->data_offset = bridge_data_write(label, (size_t)length);
    C->data_size = (uint32_t)length;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glPrimitiveBoundingBox(GLfloat minX, GLfloat minY,
                                                   GLfloat minZ, GLfloat minW,
                                                   GLfloat maxX, GLfloat maxY,
                                                   GLfloat maxZ, GLfloat maxW)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glPrimitiveBoundingBox);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_f32(&W, minX);
  aw_f32(&W, minY);
  aw_f32(&W, minZ);
  aw_f32(&W, minW);
  aw_f32(&W, maxX);
  aw_f32(&W, maxY);
  aw_f32(&W, maxZ);
  aw_f32(&W, maxW);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glUseProgramStages(GLuint pipeline,
                                               GLbitfield stages,
                                               GLuint program)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUseProgramStages);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, pipeline);
  aw_u32(&W, stages);
  aw_u32(&W, program);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glBindProgramPipeline(GLuint pipeline)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindProgramPipeline);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pipeline);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGenProgramPipelines(GLsizei n, GLuint *pipelines)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGenProgramPipelines);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  for (int i = 0; i < n; i++)
    pipelines[i] = ar_u32(&r);
}

GL_APICALL void GL_APIENTRY glDeleteProgramPipelines(GLsizei n,
                                                     const GLuint *pipelines)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDeleteProgramPipelines);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_i32(&W, n);
  for (int i = 0; i < n; i++)
    aw_u32(&W, pipelines[i]);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL GLboolean GL_APIENTRY glIsProgramPipeline(GLuint pipeline)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glIsProgramPipeline);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pipeline);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  return (GLboolean)ar_u32(&r);
}

GL_APICALL void GL_APIENTRY glDrawArraysIndirect(GLenum mode,
                                                 const void *indirect)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawArraysIndirect);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, mode);
  aw_u64(&W, (uint64_t)(uintptr_t)indirect);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glDrawElementsIndirect(GLenum mode, GLenum type,
                                                   const void *indirect)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDrawElementsIndirect);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, mode);
  aw_u32(&W, type);
  aw_u64(&W, (uint64_t)(uintptr_t)indirect);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

// -----------------------------
// UniformMatrix* (GLES 3.0)
// -----------------------------
GL_APICALL void GL_APIENTRY glUniformMatrix2x3fv(GLint loc, GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUniformMatrix2x3fv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, loc);
  aw_i32(&W, count);
  aw_u32(&W, transpose);
  C->args_len = W.pos;
  C->data_offset =
      bridge_data_write(value, (size_t)count * 6 * sizeof(GLfloat));
  C->data_size = (uint32_t)count * 6 * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniformMatrix3x2fv(GLint loc, GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUniformMatrix3x2fv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, loc);
  aw_i32(&W, count);
  aw_u32(&W, transpose);
  C->args_len = W.pos;
  C->data_offset =
      bridge_data_write(value, (size_t)count * 6 * sizeof(GLfloat));
  C->data_size = (uint32_t)count * 6 * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniformMatrix2x4fv(GLint loc, GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUniformMatrix2x4fv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, loc);
  aw_i32(&W, count);
  aw_u32(&W, transpose);
  C->args_len = W.pos;
  C->data_offset =
      bridge_data_write(value, (size_t)count * 8 * sizeof(GLfloat));
  C->data_size = (uint32_t)count * 8 * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniformMatrix4x2fv(GLint loc, GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUniformMatrix4x2fv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, loc);
  aw_i32(&W, count);
  aw_u32(&W, transpose);
  C->args_len = W.pos;
  C->data_offset =
      bridge_data_write(value, (size_t)count * 8 * sizeof(GLfloat));
  C->data_size = (uint32_t)count * 8 * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniformMatrix3x4fv(GLint loc, GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUniformMatrix3x4fv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, loc);
  aw_i32(&W, count);
  aw_u32(&W, transpose);
  C->args_len = W.pos;
  C->data_offset =
      bridge_data_write(value, (size_t)count * 12 * sizeof(GLfloat));
  C->data_size = (uint32_t)count * 12 * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUniformMatrix4x3fv(GLint loc, GLsizei count,
                                                 GLboolean transpose,
                                                 const GLfloat *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUniformMatrix4x3fv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, loc);
  aw_i32(&W, count);
  aw_u32(&W, transpose);
  C->args_len = W.pos;
  C->data_offset =
      bridge_data_write(value, (size_t)count * 12 * sizeof(GLfloat));
  C->data_size = (uint32_t)count * 12 * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

// -----------------------------
// Program Interface (GLES 3.1)
// -----------------------------
GL_APICALL void GL_APIENTRY glGetProgramInterfaceiv(GLuint program,
                                                    GLenum iface, GLenum pname,
                                                    GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetProgramInterfaceiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, iface);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);
  *params = ar_i32(&r);
}

GL_APICALL GLuint GL_APIENTRY glGetProgramResourceIndex(GLuint program,
                                                        GLenum iface,
                                                        const GLchar *name)
{
#ifdef DEBUG_SHADERS
  log_console("glGetProgramResourceIndex: program:%d iface:%d name:%s", program,
              iface, name);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetProgramResourceIndex);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, iface);
  C->args_len = W.pos;

  if (name)
  {
    uint32_t len = (uint32_t)strlen(name) + 1;
    C->data_offset = bridge_data_write(name, len);
    C->data_size = len;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);
  return ar_u32(&r);
}

GL_APICALL void GL_APIENTRY glGetProgramResourceName(GLuint program,
                                                     GLenum iface, GLuint index,
                                                     GLsizei bufSize,
                                                     GLsizei *length,
                                                     GLchar *name)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetProgramResourceName);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, iface);
  aw_u32(&W, index);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  *length = ar_i32(&r);
  memcpy(name, C2->result_buf + sizeof(GLsizei), *length);
}

GL_APICALL void GL_APIENTRY glGetProgramResourceiv(
    GLuint program, GLenum iface, GLuint index, GLsizei propCount,
    const GLenum *props, GLsizei bufSize, GLsizei *length, GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetProgramResourceiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, iface);
  aw_u32(&W, index);
  aw_i32(&W, propCount);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  /* Send props array via data shm */
  if (props && propCount > 0)
  {
    C->data_offset = bridge_data_write(props, propCount * sizeof(GLenum));
    C->data_size = propCount * sizeof(GLenum);
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  GLsizei outLen = ar_i32(&r);
  if (length)
    *length = outLen;
  for (int i = 0; i < outLen; i++)
    params[i] = ar_i32(&r);
}

GL_APICALL GLint GL_APIENTRY glGetProgramResourceLocation(GLuint program,
                                                          GLenum iface,
                                                          const GLchar *name)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetProgramResourceLocation);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, iface);
  C->args_len = W.pos;

  uint32_t len = (name ? (uint32_t)strlen(name) + 1 : 0);
  if (len > 0)
  {
    C->data_offset = bridge_data_write(name, len);
    C->data_size = len;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);
  return ar_i32(&r);
}

// -----------------------------
// Program Pipeline (GLES 3.1)
// -----------------------------
GL_APICALL void GL_APIENTRY glActiveShaderProgram(GLuint pipeline,
                                                  GLuint program)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glActiveShaderProgram);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pipeline);
  aw_u32(&W, program);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL GLuint GL_APIENTRY
glCreateShaderProgramv(GLenum type, GLsizei count, const GLchar *const *strings)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCreateShaderProgramv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, type);
  aw_i32(&W, count);
  C->args_len = W.pos;

  /* Pack all strings into one contiguous block */
  if (strings && count > 0)
  {
    size_t total = 0;
    for (int i = 0; i < count; i++)
      total += strlen(strings[i]) + 1;

    char *buf = alloca(total);
    char *p = buf;

    for (int i = 0; i < count; i++)
    {
      size_t len = strlen(strings[i]) + 1;
      memcpy(p, strings[i], len);
      p += len;
    }

    C->data_offset = bridge_data_write(buf, total);
    C->data_size = (uint32_t)total;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);
  return ar_u32(&r);
}

GL_APICALL void GL_APIENTRY glValidateProgramPipeline(GLuint pipeline)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glValidateProgramPipeline);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pipeline);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGetProgramPipelineInfoLog(GLuint pipeline,
                                                        GLsizei bufSize,
                                                        GLsizei *length,
                                                        GLchar *infoLog)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetProgramPipelineInfoLog);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pipeline);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);

  *length = ar_i32(&r);
  memcpy(infoLog, C2->result_buf + sizeof(GLsizei), *length);
}

// -----------------------------
// MemoryBarrierByRegion (GLES 3.1)
// -----------------------------
GL_APICALL void GL_APIENTRY glMemoryBarrierByRegion(GLbitfield barriers)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glMemoryBarrierByRegion);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, barriers);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

// -----------------------------
// BlendBarrier (KHR extension)
// -----------------------------
GL_APICALL void GL_APIENTRY glBlendBarrier(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBlendBarrier);

  C->args_len = 0;
  BRIDGE_SEND_CALL();
}

// -----------------------------
// Robustness (GLES 3.2)
// -----------------------------
GL_APICALL GLenum GL_APIENTRY glGetGraphicsResetStatus(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetGraphicsResetStatus);

  C->args_len = 0;
  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  ArgReader r = ar_init(C2->result_buf, C2->result_buf_len);
  return ar_u32(&r);
}

GL_APICALL void GL_APIENTRY glReadnPixels(GLint x, GLint y, GLsizei w,
                                          GLsizei h, GLenum format, GLenum type,
                                          GLsizei bufSize, void *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glReadnPixels);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, w);
  aw_i32(&W, h);
  aw_u32(&W, format);
  aw_u32(&W, type);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(data, C2->result_buf, bufSize);
}

GL_APICALL void GL_APIENTRY glGetnUniformfv(GLuint program, GLint loc,
                                            GLsizei bufSize, GLfloat *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetnUniformfv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, loc);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, bufSize);
}

GL_APICALL void GL_APIENTRY glGetnUniformiv(GLuint program, GLint loc,
                                            GLsizei bufSize, GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetnUniformiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, loc);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, bufSize);
}

GL_APICALL void GL_APIENTRY glGetnUniformuiv(GLuint program, GLint loc,
                                             GLsizei bufSize, GLuint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetnUniformuiv);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, loc);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  memcpy(params, C2->result_buf, bufSize);
}

// -----------------------------
// PatchParameteri (GLES 3.2)
// -----------------------------
GL_APICALL void GL_APIENTRY glPatchParameteri(GLenum pname, GLint value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glPatchParameteri);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pname);
  aw_i32(&W, value);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

// -----------------------------
// TexBufferRange (GLES 3.2)
// -----------------------------
GL_APICALL void GL_APIENTRY glTexBufferRange(GLenum target,
                                             GLenum internalformat,
                                             GLuint buffer, GLintptr offset,
                                             GLsizeiptr size)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glTexBufferRange);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, internalformat);
  aw_u32(&W, buffer);
  aw_u64(&W, (uint64_t)offset);
  aw_u64(&W, (uint64_t)size);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}
