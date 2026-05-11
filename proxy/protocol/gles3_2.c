#include <GLES3/gl32.h>

#define LOG_PREFIX "[proxy/gles32]"
#include "../include/gles_util_proxy.h"
#include "../proxy.h"

#ifdef DEBUG_VERBOSE
#include <sys/syscall.h>
#include <unistd.h>
#endif

#define MAX_SYNCS 64
MapEntry maps[MAX_MAPS];
static SyncEntry syncs[MAX_SYNCS];

static uint32_t alloc_sync(GLsync s)
{
  for (uint32_t i = 1; i < MAX_SYNCS; i++)
  {
    if (!syncs[i].real)
    {
      syncs[i].real = s;
      return i;
    }
  }
  return 0;
}

static GLsync lookup_sync(uint32_t id)
{
  if (id == 0 || id >= MAX_SYNCS)
    return NULL;
  return syncs[id].real;
}

static void free_sync(uint32_t id)
{
  if (id == 0 || id >= MAX_SYNCS)
    return;
  syncs[id].real = NULL;
}

static uint32_t alloc_map(void *real, uint32_t shm_offset, GLsizeiptr length,
                          GLenum target, GLuint buffer, GLbitfield access)
{
  for (uint32_t i = 1; i < MAX_MAPS; i++)
  {
    if (!maps[i].real_ptr)
    {
      maps[i].real_ptr = real;
      maps[i].shm_offset = shm_offset;
      maps[i].length = length;
      maps[i].target = target;
      maps[i].buffer = buffer;
      maps[i].access = access;
      return i;
    }
  }

  return 0;
}

static void *real_gles = NULL;

static void ensure_gles_loaded(void)
{
  if (!real_gles)
    real_gles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
}

#define LOAD_GLES_FUNC_DLSYM(type, name)                                       \
  static type p_##name = NULL;                                                 \
  if (!p_##name)                                                               \
  {                                                                            \
    ensure_gles_loaded();                                                      \
    p_##name = (type)dlsym(real_gles, #name);                                  \
    if (!p_##name)                                                             \
    {                                                                          \
      log_error("Missing GLES function: %s", #name);                           \
      return;                                                                  \
    }                                                                          \
  }

void h_glGenVertexArrays(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLsizei n = ar_i32(&r);

  // Step 1: generate real VAO IDs
  glGenVertexArrays(n, (GLuint *)C->result_buf);

#ifdef DEBUG_VERBOSE
  log_console("h_glGenVertexArrays (tid=%ld): n:%d g_current_ctx:%d vao:%d",
              (long)syscall(SYS_gettid), n, g_current_ctx,
              g_proxy_ctx[g_current_ctx].current_vao);
#endif

  // Step 2: tell stub how many bytes we wrote
  C->result_buf_len = n * sizeof(GLuint);

  // Step 3: zero-init VAO state for each generated ID
  GLuint *ids = (GLuint *)C->result_buf;

  GLContextState *ctx = &g_proxy_ctx[g_current_ctx];

  for (GLsizei i = 0; i < n; ++i)
  {
    GLuint id = ids[i];
    memset(&ctx->vaos[id], 0, sizeof(VAOState));
  }

  (void)D;
}

void h_glDeleteVertexArrays(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLsizei n = ar_i32(&r);

  glDeleteVertexArrays(n, (GLuint *)dp(C->data_offset));

  (void)D;
}

void h_glBindVertexArray(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint array = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glBindVertexArray array:%u g_current_ctx:%d", array,
              g_current_ctx);
#endif

  if (array >= MAX_VAOS)
    log_error("h_glBindVertexArray: VAO OOB in EBO restore: %u", array);

  GLContextState *ctx = &g_proxy_ctx[g_current_ctx];
  ctx->current_vao = array;

  glBindVertexArray(array);

  VAOState *vao = &ctx->vaos[array];

  if (!vao->initialised)
  {
    memset(vao, 0, sizeof(VAOState));
    vao->initialised = 1;
  }

#ifdef DEBUG_VERBOSE
  log_console("h_glBindVertexArray restore vao->ebo:%d", vao->ebo);
#endif

  // Restore EBO
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vao->ebo);

  GLint prev_array_buffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

  bool restoreArrayBuffer = false;

  // Restore attributes
  for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
  {
    AttribState *a = &vao->attribs[i];

    // Skip completely unused slots
    if (!a->enabled && a->size == 0 && a->vbo == 0)
    {
#ifdef DEBUG_VERBOSE
      log_console("h_glBindVertexArray skipping i:%i", i);
#endif
      continue;
    }

    // Size must be 1..4 for glVertexAttribPointer/IPointer
    if (a->size < 1 || a->size > 4)
    {
#ifdef DEBUG_VERBOSE
      log_console("h_glBindVertexArray skipping i:%i a->size:%d", a->size);
#endif
      continue;
    }

#ifdef DEBUG_VERBOSE
    log_console("h_glBindVertexArray i:%d a->size:%d a->type:%d "
                "a->normalized:%d a->stride:%d a->pointer:%p a->divisor:%d",
                i, a->size, a->type, a->normalized, a->stride, a->pointer,
                a->divisor);
#endif

    restoreArrayBuffer = true;

    glBindBuffer(GL_ARRAY_BUFFER, a->vbo);

    if (a->integer)
      glVertexAttribIPointer(i, a->size, a->type, a->stride,
                             (void *)a->pointer);
    else
      glVertexAttribPointer(i, a->size, a->type, a->normalized, a->stride,
                            (void *)a->pointer);

    if (a->enabled)
      glEnableVertexAttribArray(i);
    else
      glDisableVertexAttribArray(i);

    glVertexAttribDivisor(i, a->divisor);
  }

  if (restoreArrayBuffer)
    glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);

  (void)D;
}

void h_glIsVertexArray(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  C->result = glIsVertexArray(ar_u32(&r));

  (void)D;
}

void h_glGenQueries(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLsizei n = ar_i32(&r);

  glGenQueries(n, (GLuint *)C->result_buf);

  C->result_buf_len = n * sizeof(GLuint);

  (void)D;
}

void h_glDeleteQueries(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLsizei n = ar_i32(&r);

  glDeleteQueries(n, (GLuint *)dp(C->data_offset));

  (void)D;
}

void h_glBeginQuery(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  glBeginQuery(ar_u32(&r), ar_u32(&r));

  (void)D;
}

void h_glEndQuery(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  glEndQuery(ar_u32(&r));

  (void)D;
}

void h_glBindSampler(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLuint unit = ar_u32(&r);
  GLuint sampler = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glBindSampler (tid=%ld): unit=%u sampler=%u",
              syscall(SYS_gettid), unit, sampler);
#endif

  glBindSampler(unit, sampler);

  (void)D;
}

void h_glSamplerParameteri(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  glSamplerParameteri(ar_u32(&r), ar_u32(&r), ar_i32(&r));

  (void)D;
}

void h_glDrawArraysInstanced(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  glDrawArraysInstanced(ar_u32(&r), ar_i32(&r), ar_i32(&r), ar_i32(&r));

  (void)D;
}

void h_glVertexAttribDivisor(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint index = ar_u32(&r);
  GLuint divisor = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glVertexAttribDivisor: index:%d divisor:%d", index, divisor);
#endif

  glVertexAttribDivisor(index, divisor);

  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_proxy_state[index].divisor = divisor;
    GLContextState *ctx = &g_proxy_ctx[g_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index].divisor = divisor;
  }
  else
    log_error("h_glVertexAttribDivisor: index:%d >= MAX_VERTEX_ATTRIBS", index);

  (void)D;
}

static GLenum get_binding_enum(GLenum target)
{
  switch (target)
  {
  case GL_ARRAY_BUFFER:
    return GL_ARRAY_BUFFER_BINDING;

  case GL_ELEMENT_ARRAY_BUFFER:
    return GL_ELEMENT_ARRAY_BUFFER_BINDING;

#ifdef GL_COPY_READ_BUFFER
  case GL_COPY_READ_BUFFER:
    return GL_COPY_READ_BUFFER_BINDING;
#endif

#ifdef GL_COPY_WRITE_BUFFER
  case GL_COPY_WRITE_BUFFER:
    return GL_COPY_WRITE_BUFFER_BINDING;
#endif

#ifdef GL_PIXEL_PACK_BUFFER
  case GL_PIXEL_PACK_BUFFER:
    return GL_PIXEL_PACK_BUFFER_BINDING;
#endif

#ifdef GL_PIXEL_UNPACK_BUFFER
  case GL_PIXEL_UNPACK_BUFFER:
    return GL_PIXEL_UNPACK_BUFFER_BINDING;
#endif

#ifdef GL_TRANSFORM_FEEDBACK_BUFFER
  case GL_TRANSFORM_FEEDBACK_BUFFER:
    return GL_TRANSFORM_FEEDBACK_BUFFER_BINDING;
#endif

#ifdef GL_UNIFORM_BUFFER
  case GL_UNIFORM_BUFFER:
    return GL_UNIFORM_BUFFER_BINDING;
#endif

#ifdef GL_TEXTURE_BUFFER
  case GL_TEXTURE_BUFFER:
    return GL_TEXTURE_BUFFER_BINDING;
#endif

#ifdef GL_ATOMIC_COUNTER_BUFFER
  case GL_ATOMIC_COUNTER_BUFFER:
    return GL_ATOMIC_COUNTER_BUFFER_BINDING;
#endif

#ifdef GL_SHADER_STORAGE_BUFFER
  case GL_SHADER_STORAGE_BUFFER:
    return GL_SHADER_STORAGE_BUFFER_BINDING;
#endif

#ifdef GL_DISPATCH_INDIRECT_BUFFER
  case GL_DISPATCH_INDIRECT_BUFFER:
    return GL_DISPATCH_INDIRECT_BUFFER_BINDING;
#endif

#ifdef GL_DRAW_INDIRECT_BUFFER
  case GL_DRAW_INDIRECT_BUFFER:
    return GL_DRAW_INDIRECT_BUFFER_BINDING;
#endif

  default:
    return 0;
  }
}

void h_glMapBufferRange(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);
  GLintptr offset = (GLintptr)ar_i64(&r);
  GLsizeiptr length = (GLsizeiptr)ar_i64(&r);
  GLbitfield access = ar_u32(&r);
  GLuint buffer = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console(
      "h_glMapBufferRange (tid=%ld): target=0x%04x offset=%lld length=%lld "
      "access=0x%04x GL_MAP_READ_BIT set=%d buffer=%d g_current_ctx=%d",
      (long)syscall(SYS_gettid), target, (long long)offset, (long long)length,
      access, access & GL_MAP_READ_BIT ? true : false, buffer, g_current_ctx);
#endif

  /* Bind the correct buffer before mapping — the stub sends the buffer id
   * explicitly because the proxy's generic bind point may differ.
   * Save and restore the previous binding to avoid corrupting proxy state. */
  GLint old_binding = 0;
  GLenum binding_enum = get_binding_enum(target);
  if (binding_enum)
    glGetIntegerv(binding_enum, &old_binding);

  if ((GLuint)old_binding != buffer)
    glBindBuffer(target, buffer);

  void *real = glMapBufferRange(target, offset, length, access);

  if (!real)
  {
    log_error("h_glMapBufferRange: target=0x%04x buffer=%u offset=%lld "
              "length=%lld glMapBufferRange returned NULL (gl_err=0x%x)",
              target, buffer, (long long)offset, (long long)length,
              glGetError());
    if ((GLuint)old_binding != buffer)
      glBindBuffer(target, old_binding);
    C->result = 0;
    return;
  }

  if ((GLuint)old_binding != buffer)
    glBindBuffer(target, old_binding);

  // real points to the mapped range (already includes 'offset')
  // Copy mapped contents into shared memory at offset 0 for this call
  if (access & GL_MAP_READ_BIT)
  {
#ifdef DEBUG_VERBOSE
    log_console("h_glMapBufferRange memcpy: real=%p length=%lld ", real,
                length);
#endif
    memcpy(D, real, (size_t)length);
  }

  C->data_offset = 0;
  C->data_size = length;

  uint32_t idx = alloc_map(real, 0, length, target, buffer, access);
  if (idx && idx < MAX_MAPS)
    maps[idx].offset = offset;

  C->result = idx;
}

void h_glUnmapBuffer(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);
  uint32_t id = ar_u32(&r);
  size_t n = C->data_size;

  if (id && id < MAX_MAPS && maps[id].real_ptr && n && maps[id].buffer != 0 &&
      n <= (size_t)maps[id].length)
  {
#ifdef DEBUG_VERBOSE
    log_console(
        "h_glUnmapBuffer: id=%u maps[id].real_ptr=%p C->data_size/n=%zu "
        "map_len=%lld buffer=%u target=%u offset=%lld maps[id].access=%d",
        id, maps[id].real_ptr, n, (long long)maps[id].length, maps[id].buffer,
        maps[id].target, (long long)maps[id].offset, maps[id].access);
#endif
    if (!(maps[id].access & GL_MAP_FLUSH_EXPLICIT_BIT))
    {
      const uint8_t *src = (const uint8_t *)dp(C->data_offset);
      uint8_t *dst = (uint8_t *)maps[id].real_ptr;

      /* Copy in 1MB chunks to avoid crashing */
      const size_t CHUNK = 1u * 1024u * 1024u;
      size_t remaining = n;
      size_t pos = 0;
      while (remaining > 0)
      {
        size_t chunk = remaining < CHUNK ? remaining : CHUNK;
#ifdef DEBUG_VERBOSE
        log_console("h_glUnmapBuffer: remaining: %d chunk:%d", remaining,
                    chunk);
#endif
        if (maps[id].access & GL_MAP_WRITE_BIT)
        {
#ifdef DEBUG_VERBOSE
          log_console("h_glUnmapBuffer: using memcpy dest:%p src:%p chunk:%d",
                      dst + pos, src + pos, chunk);
#endif
          memcpy(dst + pos, src + pos, chunk);
        }
        pos += chunk;
        remaining -= chunk;
      }
    }
#ifdef DEBUG_VERBOSE
    else
    {
      log_console("h_glUnmapBuffer: skipping as GL_MAP_FLUSH_EXPLICIT_BIT set");
    }
#endif
  }
  else if (id && id < MAX_MAPS && maps[id].real_ptr &&
           !(maps[id].access & GL_MAP_FLUSH_EXPLICIT_BIT))
  {
    log_error("h_glUnmapBuffer: id=%u size mismatch data_size=%zu map_len=%lld",
              id, n, (long long)maps[id].length);
  }

  GLint old_binding = 0;

  GLenum binding_enum = get_binding_enum(target);
  if (binding_enum)
    glGetIntegerv(binding_enum, &old_binding);

  glBindBuffer(target, maps[id].buffer);
  C->result = glUnmapBuffer(target);

  glBindBuffer(target, old_binding);

#ifdef DEBUG_VERBOSE
  log_console("h_glUnmapBuffer: calling glUnmapBuffer - result:%d", C->result);
#endif
  if (id && id < MAX_MAPS)
  {
    maps[id].real_ptr = NULL;
    maps[id].length = 0;
    maps[id].target = 0;
    maps[id].offset = 0;
    maps[id].buffer = 0;
    maps[id].access = 0;
  }

  C->result_buf_len = 0;
}

void h_glTexStorage2D(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLsizei levels = ar_i32(&r);
  GLenum internalformat = ar_u32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  log_console("[proxy][h_glTexStorage2D] BEGIN");
  log_console("    target=0x%04x", target);
  log_console("    levels=%d", levels);
  log_console("    internalformat=0x%04x", internalformat);
  log_console("    width=%d height=%d", width, height);

  GLenum prev_err = glGetError();
  log_console("    prev_gl_err=0x%04x", prev_err);

  GLenum prev_egl_err = eglGetError();
  log_console("    prev_egl_err=0x%04x", prev_egl_err);
#endif

  glTexStorage2D(target, levels, internalformat, width, height);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("    glTexStorage2D -> gl_err=0x%04x", after_err);

  GLenum new_egl_err = eglGetError();
  if (new_egl_err != EGL_SUCCESS)
    log_error("    glTexStorage2D -> egl_err=0x%04x", new_egl_err);

#ifdef DEBUG_ABORT_ON_GL_ERROR
  if (new_egl_err != EGL_SUCCESS)
    abort();
#endif

  log_console("[proxy][h_glTexStorage2D] END");
#endif

  C->result = 0;
}

void h_glFenceSync(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum condition = ar_u32(&r);
  GLbitfield flags = ar_u32(&r);

  GLsync s = glFenceSync(condition, flags);
  uint32_t id = alloc_sync(s);

  C->result = id;
}

void h_glClientWaitSync(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t id = ar_u32(&r);
  GLbitfield flags = ar_u32(&r);
  GLuint64 timeout = ar_u64(&r);

  GLsync s = lookup_sync(id);
  C->result = glClientWaitSync(s, flags, timeout);
}

void h_glDeleteSync(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t id = ar_u32(&r);
  GLsync s = lookup_sync(id);

  if (s)
    glDeleteSync(s);

  free_sync(id);
}

static void proxy_debug_callback(GLenum source, GLenum type, GLuint id,
                                 GLenum severity, GLsizei length,
                                 const GLchar *message, const void *userParam)
{
  (void)source;
  (void)type;
  (void)id;
  (void)severity;
  (void)length;
  (void)userParam;
#ifdef DEBUG_VERBOSE
  log_console("[GLDBG] %s", message);
#else
  (void)message;
#endif
}

void h_glDebugMessageCallback(BridgeCtrl *C, uint8_t *D)
{
  glDebugMessageCallback(proxy_debug_callback, NULL);

  (void)C;
  (void)D;
}

void h_glDebugMessageControl(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum source = ar_u32(&r);

  GLenum type = ar_u32(&r);

  GLenum severity = ar_u32(&r);

  GLsizei count = ar_i32(&r);

  GLboolean enabled = ar_u32(&r);

  glDebugMessageControl(source, type, severity, count,
                        count ? (GLuint *)dp(C->data_offset) : NULL, enabled);

  (void)D;
}

void h_glFlushMappedBufferRange(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);
  GLintptr offset = ar_i32(&r); // local offset within mapped range
  GLsizeiptr length = ar_i32(&r);
  GLuint id = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console(
      "h_glFlushMappedBufferRange: target=0x%x id=%u buffer=%u offset=%lld "
      "length=%lld first4=%02x %02x %02x %02x",
      target, id, maps[id].buffer, (long long)offset, (long long)length,
      maps[id].real_ptr ? ((uint8_t *)maps[id].real_ptr)[offset] : 0,
      maps[id].real_ptr ? ((uint8_t *)maps[id].real_ptr)[offset + 1] : 0,
      maps[id].real_ptr ? ((uint8_t *)maps[id].real_ptr)[offset + 2] : 0,
      maps[id].real_ptr ? ((uint8_t *)maps[id].real_ptr)[offset + 3] : 0);
#endif

  if (id == 0 || id >= MAX_MAPS || !maps[id].real_ptr)
  {
    log_error("h_glFlushMappedBufferRange: invalid map id %u", id);
    return;
  }

  if (maps[id].target != target)
  {
    log_error("h_glFlushMappedBufferRange: target mismatch "
              "(id=%u map_target=0x%04x req_target=0x%04x)",
              id, maps[id].target, target);
    return;
  }

  if ((GLsizeiptr)offset < 0 || offset + length > maps[id].length)
  {
    log_error("h_glFlushMappedBufferRange: range overflow "
              "(id=%u offset=%lld length=%lld map_length=%lld)",
              id, (long long)offset, (long long)length,
              (long long)maps[id].length);
    return;
  }

  // Copy from shared memory into real mapped range
  memcpy((uint8_t *)maps[id].real_ptr + offset, dp(C->data_offset),
         (size_t)length);

#ifdef DEBUG_VERBOSE
  uint8_t *p = (uint8_t *)maps[id].real_ptr + offset;
  log_console("h_glFlushMappedBufferRange: flush wrote: %02x %02x %02x %02x",
              p[0], p[1], p[2], p[3]);
#endif

  // GL expects buffer-relative offset, not local offset
  GLintptr buf_off = maps[id].offset + offset;

  glFlushMappedBufferRange(target, buf_off, length);

#ifdef DEBUG_VERBOSE
  GLenum err = glGetError();
  if (err)
    log_error("h_glFlushMappedBufferRange error: 0x%x", err);
#endif
}

void h_glTexImage3D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t pixel_mode = ar_u32(&r);

#ifdef DEBUG_VERBOSE

  GLenum target = ar_u32(&r);
  GLint level = ar_i32(&r);
  GLint internalformat = ar_i32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);
  GLsizei depth = ar_i32(&r);
  GLint border = ar_i32(&r);
  GLenum format = ar_u32(&r);
  GLenum type = ar_u32(&r);

  const void *pixels;

  switch (pixel_mode)
  {
  case 0: /* NULL */
    pixels = NULL;
    break;

  case 1: /* PBO offset */
    pixels = (void *)(uintptr_t)C->data_offset;
    break;

  default: /* shared memory */
    pixels = D + C->data_offset;
    break;
  }

  log_console("h_glTexImage3D: target=0x%04x level=%d ifmt=0x%04x "
              "width=%d height=%d depth=%d border=%d format=0x%04x "
              "type=0x%04x data_size=%zu pixels=%p",
              target, level, internalformat, width, height, depth, border,
              format, type, C->data_size, pixels);

  glTexImage3D(target, level, internalformat, width, height, depth, border,
               format, type, pixels);

#else
  glTexImage3D(ar_u32(&r), /* target */
               ar_i32(&r), /* level */
               ar_i32(&r), /* internalformat */
               ar_i32(&r), /* width */
               ar_i32(&r), /* height */
               ar_i32(&r), /* depth */
               ar_i32(&r), /* border */
               ar_u32(&r), /* format */
               ar_u32(&r), /* type */
               pixel_mode == 0   ? NULL
               : pixel_mode == 1 ? (void *)(uintptr_t)C->data_offset
                                 : D + C->data_offset);
#endif
}

void h_glTexSubImage3D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

#ifdef DEBUG_VERBOSE
  GLenum target = ar_u32(&r);
  GLint level = ar_i32(&r);
  GLint xo = ar_i32(&r);
  GLint yo = ar_i32(&r);
  GLint zo = ar_i32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);
  GLsizei depth = ar_i32(&r);
  GLenum format = ar_u32(&r);
  GLenum type = ar_u32(&r);

  GLint unpack_pbo = 0;
  glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_pbo);

  GLint bound = 0;
  if (target == GL_TEXTURE_2D_ARRAY)
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &bound);

  log_console("h_glTexSubImage3D: target=%d level=%d xo=%d yo=%d zo=%d "
              "width=%d height=%d depth=%d format=0x%04x type=%d unpack_pbo=%d "
              "data_size=%zu data_offset=%zu bound=%d",
              target, level, xo, yo, zo, width, height, depth, format, type,
              unpack_pbo, C->data_size, C->data_offset, bound);

  const void *pixels = (C->data_size == 0)
                           ? (const void *)(uintptr_t)C->data_offset
                           : (const void *)(D + C->data_offset);

  GLenum prev_err = glGetError();

  glTexSubImage3D(target, level, xo, yo, zo, width, height, depth, format, type,
                  pixels);

  GL_LOG_IF_ERR("h_glTexSubImage3D: err_before=0x%x "
                "after_err=0x%x",
                prev_err, after_err);

#else
  glTexSubImage3D(ar_u32(&r), ar_i32(&r), ar_i32(&r), ar_i32(&r), ar_i32(&r),
                  ar_i32(&r), ar_i32(&r), ar_i32(&r), ar_u32(&r), ar_u32(&r),
                  C->data_size != 0 ? D + C->data_offset
                                    : (void *)(uintptr_t)C->data_offset);

#endif
}

void h_glCompressedTexImage3D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);

  GLint level = ar_i32(&r);

  GLenum internalformat = ar_u32(&r);

  GLsizei width = ar_i32(&r);

  GLsizei height = ar_i32(&r);

  GLsizei depth = ar_i32(&r);

  GLint border = ar_i32(&r);

  GLsizei imageSize = ar_i32(&r);

  uint32_t pixel_mode = ar_u32(&r);

  const void *data = NULL;

  switch (pixel_mode)
  {
  case 0:
    data = NULL;
    break;

  case 1:
    data = (void *)(uintptr_t)C->data_offset;
    break;

  default:
    data = dp(C->data_offset);
    break;
  }

#ifdef DEBUG_VERBOSE
  log_console("h_glCompressedTexImage3D:"
              " target=%d"
              " level=%d"
              " ifmt=%d"
              " w=%d"
              " h=%d"
              " d=%d"
              " size=%d"
              " has_data=%d",
              target, level, internalformat, width, height, depth, imageSize,
              data != NULL);
#endif

  glCompressedTexImage3D(target, level, internalformat, width, height, depth,
                         border, imageSize, data);
}

void h_glCompressedTexSubImage3D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);

  GLint level = ar_i32(&r);

  GLint xoffset = ar_i32(&r);

  GLint yoffset = ar_i32(&r);

  GLint zoffset = ar_i32(&r);

  GLsizei width = ar_i32(&r);

  GLsizei height = ar_i32(&r);

  GLsizei depth = ar_i32(&r);

  GLenum format = ar_u32(&r);

  GLsizei imageSize = ar_i32(&r);

  uint32_t pixel_mode = ar_u32(&r);

  const void *data = NULL;

  switch (pixel_mode)
  {
  case 0:
    data = NULL;
    break;

  case 1:
    data = (void *)(uintptr_t)C->data_offset;
    break;

  default:
    data = dp(C->data_offset);
    break;
  }

#ifdef DEBUG_VERBOSE
  log_console("h_glCompressedTexSubImage3D:"
              " target=%d"
              " level=%d"
              " xo=%d"
              " yo=%d"
              " zo=%d"
              " w=%d"
              " h=%d"
              " d=%d"
              " fmt=%d"
              " size=%d"
              " has_data=%d",
              target, level, xoffset, yoffset, zoffset, width, height, depth,
              format, imageSize, data != NULL);
#endif

  glCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset, width,
                            height, depth, format, imageSize, data);
}

void h_glCopyTexSubImage3D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  glCopyTexSubImage3D(ar_u32(&r), ar_i32(&r), ar_i32(&r), ar_i32(&r),
                      ar_i32(&r), ar_i32(&r), ar_i32(&r), ar_i32(&r),
                      ar_i32(&r));

  (void)D;
}

void h_glTexStorage3D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);
  GLsizei levels = ar_i32(&r);
  GLenum internalformat = ar_u32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);
  GLsizei depth = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  GLint bound = 0;
  if (target == GL_TEXTURE_2D_ARRAY)
    glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &bound);

  log_console("h_glTexStorage3D: target=0x%x bound_tex=%d "
              "levels=%d ifmt=0x%x %dx%dx%d",
              target, bound, levels, internalformat, width, height, depth);
  GLenum prev_err = glGetError();
#endif

  glTexStorage3D(target, levels, internalformat, width, height, depth);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("h_glTexStorage3D: err_before=0x%x "
                "after_err=0x%x",
                prev_err, after_err);
#endif
}

void h_glGetStringi(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum name = ar_u32(&r);
  GLuint index = ar_u32(&r);

  const GLubyte *s = glGetStringi(name, index);

#ifdef DEBUG_GL_GETSTRING
  log_console("h_glGetStringi: name=0x%04x index=%d -> %s", name, index,
              s ? (const char *)s : "(null)");
#endif

  if (s)
  {
    strncpy((char *)C->result_buf, (const char *)s, BRIDGE_RESULT_SIZE - 1);
    C->result_buf[BRIDGE_RESULT_SIZE - 1] = '\0';
  }
  else
  {
    C->result_buf[0] = '\0';
  }

  C->result = 0;
}

void h_glGetInteger64v(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum pname = ar_u32(&r);

  GLint64 value = 0;
  glGetInteger64v(pname, &value);

  memcpy(C->result_buf, &value, sizeof(GLint64));
  C->result_buf_len = sizeof(GLint64);
  C->result = 0;
}

void h_glGetFragDataLocation(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint program = ar_u32(&r);

  // The rest of args is the null‑terminated string
  const char *name = (const char *)(C->args + r.pos);

  GLint loc = glGetFragDataLocation(program, name);

  C->result = (uint64_t)(int64_t)loc;
}

typedef void(GL_APIENTRYP PFNGLGETBOOLEANI_VPROC)(GLenum target, GLuint index,
                                                  GLboolean *data);

void h_glGetBooleani_v(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);
  GLuint index = ar_u32(&r);

  static PFNGLGETBOOLEANI_VPROC pfn = NULL;

  if (!pfn)
  {
    pfn = (PFNGLGETBOOLEANI_VPROC)eglGetProcAddress("glGetBooleani_v");

    if (!pfn)
    {
#ifdef DEBUG_VERBOSE
      log_error("glGetBooleani_v unavailable");
      abort();
#endif
      C->result_buf_len = 0;
      return;
    }
  }

  GLboolean value = 0;

  pfn(target, index, &value);

  memcpy(C->result_buf, &value, sizeof(value));

  C->result_buf_len = sizeof(value);

  (void)D;
}

/* ───────── I4i / I4iv / I4ui / I4uiv ───────── */

void h_glVertexAttribI4i(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  GLint x = ar_i32(&r);
  GLint y = ar_i32(&r);
  GLint z = ar_i32(&r);
  GLint w = ar_i32(&r);
  glVertexAttribI4i(index, x, y, z, w);
}

void h_glVertexAttribI4iv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  const GLint *v = (const GLint *)dp(C->data_offset);
  glVertexAttribI4iv(index, v);
}

void h_glVertexAttribI4ui(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  GLuint x = ar_u32(&r);
  GLuint y = ar_u32(&r);
  GLuint z = ar_u32(&r);
  GLuint w = ar_u32(&r);
  glVertexAttribI4ui(index, x, y, z, w);
}

void h_glVertexAttribI4uiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  const GLuint *v = (const GLuint *)dp(C->data_offset);
  glVertexAttribI4uiv(index, v);
}

/* ───────── IPointer ───────── */

void h_glVertexAttribIPointer(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  GLint size = ar_i32(&r);
  GLenum type = ar_u32(&r);
  GLsizei stride = ar_i32(&r);
  const void *ptr = (const void *)(uintptr_t)ar_u64(&r);
  GLuint vbo = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console(
      "h_glVertexAttribIPointer: index:%d size:%d type:%d stride:%d ptr:%p",
      index, size, type, stride, ptr);
#endif

  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_proxy_state[index].size = size;
    g_attrib_proxy_state[index].type = type;
    g_attrib_proxy_state[index].normalized = GL_FALSE;
    g_attrib_proxy_state[index].stride = stride;
    g_attrib_proxy_state[index].pointer = (uintptr_t)ptr;
    g_attrib_proxy_state[index].vbo = vbo;
    g_attrib_proxy_state[index].integer = GL_TRUE;

    GLContextState *ctx = &g_proxy_ctx[g_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index] = g_attrib_proxy_state[index];
  }
  else
    log_error("h_glVertexAttribIPointer: index:%d >= MAX_VERTEX_ATTRIBS",
              index);

  GLint prev_array_buffer = 0;

  /* bind VBO if non-zero so ptr is treated as offset */
  if (vbo != 0)
  {
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
  }

  glVertexAttribIPointer(index, size, type, stride, (const void *)ptr);

  if (vbo != 0)
    glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
}

void h_glBeginTransformFeedback(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum mode = ar_u32(&r);
  glBeginTransformFeedback(mode);
}

void h_glEndTransformFeedback(BridgeCtrl *C, uint8_t *D)
{
  (void)C;
  (void)D;
  glEndTransformFeedback();
}

void h_glTransformFeedbackVaryings(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum bufferMode = ar_u32(&r);

  uint8_t *p = (uint8_t *)dp(C->data_offset);
  const char **names = (const char **)alloca(count * sizeof(char *));

  for (GLsizei i = 0; i < count; i++)
  {
    names[i] = (const char *)p;
    size_t len = strlen(names[i]) + 1;
    p += len;
  }

  glTransformFeedbackVaryings(program, count, names, bufferMode);
}

void h_glGetTransformFeedbackVarying(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLuint index = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLsizei length = 0, size = 0;
  GLenum type = 0;
  char *tmp = (char *)alloca(bufSize > 0 ? bufSize : 1);

  glGetTransformFeedbackVarying(program, index, bufSize, &length, &size, &type,
                                tmp);

  ArgWriter W = aw_init(C->result_buf, BRIDGE_RESULT_SIZE);
  aw_i32(&W, length);
  aw_i32(&W, size);
  aw_u32(&W, type);
  memcpy(C->result_buf + W.pos, tmp, (size_t)length + 1);
}

/* Uniform*ui* */

void h_glUniform1ui(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLuint v0 = ar_u32(&r);
  glUniform1ui(location, v0);
}

void h_glGetUniformuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLint location = ar_i32(&r);
  GLuint v = 0;
  glGetUniformuiv(program, location, &v);
  memcpy(C->result_buf, &v, sizeof(GLuint));
  C->result_buf_len = sizeof(GLuint);
}

void h_glUniform1uiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLsizei count = ar_i32(&r);

  const GLuint *value = (const GLuint *)dp(C->data_offset);
  glUniform1uiv(location, count, value);
}

void h_glUniform2ui(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLuint v0 = ar_u32(&r);
  GLuint v1 = ar_u32(&r);

  glUniform2ui(location, v0, v1);
}

void h_glUniform2uiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLsizei count = ar_i32(&r);

  const GLuint *value = (const GLuint *)dp(C->data_offset);
  glUniform2uiv(location, count, value);
}

void h_glUniform3ui(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLuint v0 = ar_u32(&r);
  GLuint v1 = ar_u32(&r);
  GLuint v2 = ar_u32(&r);

  glUniform3ui(location, v0, v1, v2);
}

void h_glUniform3uiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLsizei count = ar_i32(&r);

  const GLuint *value = (const GLuint *)dp(C->data_offset);
  glUniform3uiv(location, count, value);
}

void h_glUniform4ui(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLuint v0 = ar_u32(&r);
  GLuint v1 = ar_u32(&r);
  GLuint v2 = ar_u32(&r);
  GLuint v3 = ar_u32(&r);

  glUniform4ui(location, v0, v1, v2, v3);
}

void h_glUniform4uiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint location = ar_i32(&r);
  GLsizei count = ar_i32(&r);

  const GLuint *value = (const GLuint *)dp(C->data_offset);
  glUniform4uiv(location, count, value);
}

/* Indexed state */

void h_glColorMaski(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  GLboolean r_ = (GLboolean)ar_u32(&r);
  GLboolean g_ = (GLboolean)ar_u32(&r);
  GLboolean b_ = (GLboolean)ar_u32(&r);
  GLboolean a_ = (GLboolean)ar_u32(&r);
  glColorMaski(index, r_, g_, b_, a_);
}

void h_glEnablei(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum cap = ar_u32(&r);
  GLuint index = ar_u32(&r);
  glEnablei(cap, index);
}

void h_glDisablei(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum cap = ar_u32(&r);
  GLuint index = ar_u32(&r);
  glDisablei(cap, index);
}

void h_glIsEnabledi(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum cap = ar_u32(&r);
  GLuint index = ar_u32(&r);
  GLboolean v = glIsEnabledi(cap, index);
  C->result = (uint64_t)v;
}

void h_glGetTexParameterIiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLint v = 0;
  glGetTexParameterIiv(target, pname, &v);
  memcpy(C->result_buf, &v, sizeof(GLint));
}

void h_glGetTexParameterIuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLuint v = 0;
  glGetTexParameterIuiv(target, pname, &v);
  memcpy(C->result_buf, &v, sizeof(GLuint));
}

void h_glTexParameterIiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  const GLint *params = (const GLint *)dp(C->data_offset);
  glTexParameterIiv(target, pname, params);
}

void h_glTexParameterIuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  const GLuint *params = (const GLuint *)dp(C->data_offset);
  glTexParameterIuiv(target, pname, params);
}

void h_glGetVertexAttribIiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint v = 0;
  glGetVertexAttribIiv(index, pname, &v);

  memcpy(C->result_buf, &v, sizeof(GLint));
}

void h_glGetVertexAttribIuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint index = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLuint v = 0;
  glGetVertexAttribIuiv(index, pname, &v);

  memcpy(C->result_buf, &v, sizeof(GLuint));
}

void h_glGetQueryiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint v = 0;
  glGetQueryiv(target, pname, &v);

  memcpy(C->result_buf, &v, sizeof(GLint));
}

void h_glGetQueryObjectuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint id = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLuint v = 0;
  glGetQueryObjectuiv(id, pname, &v);

  memcpy(C->result_buf, &v, sizeof(GLuint));
}

void h_glIsQuery(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint id = ar_u32(&r);

  GLboolean v = glIsQuery(id);
  C->result = v;
}

void h_glBindBufferBase(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLuint index = ar_u32(&r);
  GLuint buffer = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glBindBufferBase: target:%d index:%d buffer:%d", target, index,
              buffer);
#endif

  glBindBufferBase(target, index, buffer);
}

void h_glBindBufferRange(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLuint index = ar_u32(&r);
  GLuint buffer = ar_u32(&r);
  GLintptr offset = (GLintptr)ar_i64(&r);
  GLsizeiptr size = (GLsizeiptr)ar_i64(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glBindBufferRange (tid=%ld): target=0x%04x index=%u buffer=%u "
              "offset=0x%llx size=0x%llx g_current_ctx=%d",
              syscall(SYS_gettid), target, index, buffer,
              (unsigned long long)offset, (unsigned long long)size,
              g_current_ctx);

  const uint8_t *ubo_ptr = (const uint8_t *)dp(offset);
  log_console("h_glBindBufferRange: UBO[0..3] = %02x %02x %02x %02x",
              ubo_ptr[0], ubo_ptr[1], ubo_ptr[2], ubo_ptr[3]);
#endif

  /* If the stub piggybacked sub-range data for a persistently-mapped buffer,
   * copy it into the real GPU-mapped range now, before binding. */
  if (C->data_size > 0)
  {
    for (uint32_t i = 1; i < MAX_MAPS; i++)
    {
      if (!maps[i].real_ptr || maps[i].buffer != buffer)
        continue;
      if ((GLintptr)offset >= maps[i].offset &&
          (GLintptr)(offset + (GLintptr)C->data_size) <=
              maps[i].offset + (GLintptr)maps[i].length)
      {
        /* Write directly into the real GPU-mapped range.
         * GL_MAP_UNSYNCHRONIZED_BIT only suppresses driver stalls before
         * returning the pointer — it does not make the memory incoherent.
         * CPU writes here are visible to subsequent GPU draws. */
        uint8_t *dst = (uint8_t *)maps[i].real_ptr + (offset - maps[i].offset);
        memcpy(dst, dp(C->data_offset), C->data_size);

#ifdef DEBUG_VERBOSE
        log_console("h_glBindBufferRange: wrote %u bytes of UBO data at map "
                    "offset 0x%llx",
                    C->data_size,
                    (unsigned long long)(offset - maps[i].offset));
#endif
      }
      break;
    }
  }

  glBindBufferRange(target, index, buffer, offset, size);

#ifdef DEBUG_VERBOSE
  /*GLint ubo = 0;
  glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, index, &ubo);
  GLint uboSize = 0;

  GLint rangeStart = 0;
  GLint rangeSize = 0;

  glGetIntegeri_v(GL_UNIFORM_BUFFER_START, index, &rangeStart);
  glGetIntegeri_v(GL_UNIFORM_BUFFER_SIZE, index, &rangeSize);

  glGetBufferParameteriv(GL_UNIFORM_BUFFER, GL_BUFFER_SIZE, &uboSize);
  log_console("h_glBindBufferRange UBO index=%u ubo buffer=%d start=0x%x "
              "size=0x%x uboSize=%d",
              index, ubo, rangeStart, rangeSize, uboSize);*/
#endif
}

void h_glGetBufferPointerv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  void *ptr = NULL;

  glGetBufferPointerv(target, pname, &ptr);

  uint64_t v = (uint64_t)(uintptr_t)ptr;
  memcpy(C->result_buf, &v, sizeof(uint64_t));
  C->result_buf_len = sizeof(uint64_t);
  C->result = 0;
}

void h_glGenSamplers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLsizei count = ar_i32(&r);

  GLuint *tmp = alloca(count * sizeof(GLuint));

  glGenSamplers(count, tmp);

  memcpy(C->result_buf, tmp, count * sizeof(GLuint));
}

void h_glDeleteSamplers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLsizei count = ar_i32(&r);

  const GLuint *samplers = (const GLuint *)dp(C->data_offset);

  glDeleteSamplers(count, samplers);
}

void h_glSamplerParameteriv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  const GLint *params = (const GLint *)dp(C->data_offset);

  glSamplerParameteriv(sampler, pname, params);
}

void h_glSamplerParameterf(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLfloat param = ar_f32(&r);

  glSamplerParameterf(sampler, pname, param);
}

void h_glSamplerParameterfv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  const GLfloat *params = (const GLfloat *)dp(C->data_offset);

  glSamplerParameterfv(sampler, pname, params);
}

void h_glGetSamplerParameteriv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint vals[4] = {0, 0, 0, 0};

  glGetSamplerParameteriv(sampler, pname, vals);

  memcpy(C->result_buf, vals, 4 * sizeof(GLint));
}

void h_glGetSamplerParameterfv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLfloat vals[4] = {0, 0, 0, 0};

  glGetSamplerParameterfv(sampler, pname, vals);

  memcpy(C->result_buf, vals, 4 * sizeof(GLfloat));
}

void h_glIsSampler(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint sampler = ar_u32(&r);

  GLboolean v = glIsSampler(sampler);

  C->result = v;
}

void h_glIsSync(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t id = ar_u32(&r);
  GLsync s = lookup_sync(id);

  C->result = (uint64_t)glIsSync(s);
}

void h_glWaitSync(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t id = ar_u32(&r);
  GLbitfield flags = ar_u32(&r);
  GLuint64 timeout = ar_u64(&r);

  GLsync s = lookup_sync(id);
  glWaitSync(s, flags, timeout);
}

void h_glGetSynciv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t id = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLsync s = lookup_sync(id);

  GLint *vals = alloca(bufSize * sizeof(GLint));
  GLsizei len = 0;

  glGetSynciv(s, pname, bufSize, &len, vals);

  memcpy(C->result_buf, &len, sizeof(GLsizei));
  memcpy(C->result_buf + sizeof(GLsizei), vals, len * sizeof(GLint));
  C->result_buf_len = sizeof(GLsizei) + len * sizeof(GLint);
}

void h_glGetUniformBlockIndex(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  const GLchar *name = (const GLchar *)dp(C->data_offset);

  GLuint idx = glGetUniformBlockIndex(program, name);
  C->result = idx;
}

void h_glUniformBlockBinding(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLuint index = ar_u32(&r);
  GLuint binding = ar_u32(&r);

  glUniformBlockBinding(program, index, binding);
}

void h_glRenderbufferStorageMultisample(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLsizei samples = ar_i32(&r);
  GLenum internalformat = ar_u32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);

  glRenderbufferStorageMultisample(target, samples, internalformat, width,
                                   height);
}

void h_glBlitFramebuffer(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLint srcX0 = ar_i32(&r);
  GLint srcY0 = ar_i32(&r);
  GLint srcX1 = ar_i32(&r);
  GLint srcY1 = ar_i32(&r);
  GLint dstX0 = ar_i32(&r);
  GLint dstY0 = ar_i32(&r);
  GLint dstX1 = ar_i32(&r);
  GLint dstY1 = ar_i32(&r);
  GLbitfield mask = ar_u32(&r);
  GLenum filter = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  GLenum prev_gl_err = glGetError();
  EGLint prev_egl_err = eglGetError();

  log_console("[h_glBlitFramebuffer] BEGIN");
  log_console("    src=(%d,%d)->(%d,%d)", srcX0, srcY0, srcX1, srcY1);
  log_console("    dst=(%d,%d)->(%d,%d)", dstX0, dstY0, dstX1, dstY1);
  log_console("    mask=0x%08x filter=0x%04x", mask, filter);
  log_console("    prev_gl_err=0x%04x prev_egl_err=0x%04x", prev_gl_err,
              prev_egl_err);
#endif

  glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                    mask, filter);

#ifdef DEBUG_VERBOSE
  EGLint egl_err = eglGetError();

  GL_LOG_IF_ERR("    glBlitFramebuffer -> gl_err=0x%04x egl_err=0x%04x",
                after_err, egl_err);

#ifdef DEBUG_ABORT_ON_GL_ERROR
  if (egl_err != EGL_SUCCESS)
    abort();
#endif

  log_console("[h_glBlitFramebuffer] END");
#endif
}

void h_glFramebufferTextureLayer(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum attachment = ar_u32(&r);
  GLuint texture = ar_u32(&r);
  GLint level = ar_i32(&r);
  GLint layer = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  GLenum err_before = glGetError();

  log_console(
      "h_glFramebufferTextureLayer: target=0x%x att=0x%x tex=%u lv=%d layer=%d",
      target, attachment, texture, level, layer);
#endif

  glFramebufferTextureLayer(target, attachment, texture, level, layer);

#ifdef DEBUG_VERBOSE
  GLint att_name = 0;
  glGetFramebufferAttachmentParameteriv(
      target, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &att_name);

  log_console("h_glFramebufferTextureLayer: att_name=%d", att_name);

  GL_LOG_IF_ERR("h_glFramebufferTextureLayer: err_before=0x%x "
                "after_err=0x%x",
                err_before, after_err);

#endif
}

void h_glGenTransformFeedbacks(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLsizei n = ar_i32(&r);

  GLuint *tmp = alloca(n * sizeof(GLuint));

  glGenTransformFeedbacks(n, tmp);

  memcpy(C->result_buf, tmp, n * sizeof(GLuint));
}

void h_glDeleteTransformFeedbacks(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLsizei n = ar_i32(&r);
  const GLuint *ids = (const GLuint *)dp(C->data_offset);

  glDeleteTransformFeedbacks(n, ids);
}

void h_glBindTransformFeedback(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLuint id = ar_u32(&r);

  glBindTransformFeedback(target, id);
}

void h_glPauseTransformFeedback(BridgeCtrl *C, uint8_t *D)
{
  (void)C;
  (void)D;
  glPauseTransformFeedback();
}

void h_glResumeTransformFeedback(BridgeCtrl *C, uint8_t *D)
{
  (void)C;
  (void)D;
  glResumeTransformFeedback();
}

void h_glIsTransformFeedback(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint id = ar_u32(&r);

  GLboolean v = glIsTransformFeedback(id);

  C->result = v;
}

void h_glCopyBufferSubData(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum readTarget = ar_u32(&r);
  GLenum writeTarget = ar_u32(&r);
  GLintptr readOffset = (GLintptr)ar_u64(&r);
  GLintptr writeOffset = (GLintptr)ar_u64(&r);
  GLsizeiptr size = (GLsizeiptr)ar_u64(&r);

  glCopyBufferSubData(readTarget, writeTarget, readOffset, writeOffset, size);
}

void h_glDrawElementsInstanced(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum mode = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum type = ar_u32(&r);
  GLsizei inst = ar_u32(&r);

  const void *indices = NULL;
  if (C->data_size)
    indices = dp(C->data_offset);

  glDrawElementsInstanced(mode, count, type, indices, inst);
}

void h_glGetUniformIndices(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLsizei count = ar_i32(&r);

  uint8_t *p = dp(C->data_offset);
  const char **names = alloca(count * sizeof(char *));
  for (int i = 0; i < count; i++)
  {
    names[i] = (char *)p;
    size_t len = strlen(names[i]) + 1;
    p += len;
  }

  GLuint *out = alloca(count * sizeof(GLuint));

  glGetUniformIndices(program, count, names, out);

  memcpy(C->result_buf, out, count * sizeof(GLuint));
}

void h_glGetActiveUniformsiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum pname = ar_u32(&r);

  const GLuint *indices = (const GLuint *)dp(C->data_offset);
  GLint *vals = alloca(count * sizeof(GLint));

  glGetActiveUniformsiv(program, count, indices, pname, vals);

  memcpy(C->result_buf, vals, count * sizeof(GLint));
}

void h_glGetActiveUniformBlockiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLuint blockIndex = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint v = 0;

  glGetActiveUniformBlockiv(program, blockIndex, pname, &v);

  memcpy(C->result_buf, &v, sizeof(GLint));
}

void h_glGetActiveUniformBlockName(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLuint blockIndex = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLchar *tmp = alloca(bufSize);
  GLsizei len = 0;

  glGetActiveUniformBlockName(program, blockIndex, bufSize, &len, tmp);

  memcpy(C->result_buf, &len, sizeof(GLsizei));
  memcpy(C->result_buf + sizeof(GLsizei), tmp, len);
  C->result_buf_len = sizeof(GLsizei) + len;
}

void h_glBindImageTexture(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint unit = ar_u32(&r);
  GLuint texture = ar_u32(&r);
  GLint level = ar_i32(&r);
  GLboolean layered = (GLboolean)ar_u32(&r);
  GLint layer = ar_i32(&r);
  GLenum access = ar_u32(&r);
  GLenum format = ar_u32(&r);

  glBindImageTexture(unit, texture, level, layered, layer, access, format);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDrawRangeElements(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum mode = ar_u32(&r);
  GLuint start = ar_u32(&r);
  GLuint end = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum type = ar_u32(&r);
  const void *indices = (const void *)(uintptr_t)ar_u64(&r);

  glDrawRangeElements(mode, start, end, count, type, indices);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glSamplerParameterIiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint param[4];
  param[0] = ar_i32(&r);
  param[1] = ar_i32(&r);
  param[2] = ar_i32(&r);
  param[3] = ar_i32(&r);

  glSamplerParameterIiv(sampler, pname, param);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glGetPointerv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum pname = ar_u32(&r);

  void *ptr = NULL;

  glGetPointerv(pname, &ptr);

  /* return pointer as 64‑bit */
  uint64_t v = (uint64_t)(uintptr_t)ptr;
  memcpy(C->result_buf, &v, sizeof(uint64_t));
  C->result_buf_len = sizeof(uint64_t);
  C->result = 0;
}

void h_glPopDebugGroup(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  /* no args */
  glPopDebugGroup();

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glGetProgramBinary(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint program = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

#ifdef DEBUG_SHADERS
  log_console("h_glGetProgramBinary: program:%d bufSize:%d", program, bufSize);
#endif

  GLsizei length = 0;
  GLenum binaryFormat = 0;

  /* Write binary directly into the shared data region — no alloca, no overflow
   */
  glGetProgramBinary(program, bufSize, &length, &binaryFormat,
                     dp(C->data_offset));

  /* Return only the two scalars through result_buf */
  uint8_t *out = C->result_buf;
  memcpy(out, &length, sizeof(GLsizei));
  memcpy(out + sizeof(GLsizei), &binaryFormat, sizeof(GLenum));
  C->result_buf_len = sizeof(GLsizei) + sizeof(GLenum);
  C->result = 0;
}

void h_glReadBuffer(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum src = ar_u32(&r);

  glReadBuffer(src);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDrawBuffers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLsizei n = ar_i32(&r);
  GLenum *bufs = alloca(sizeof(GLenum) * n);

  for (int i = 0; i < n; i++)
    bufs[i] = ar_u32(&r);

  glDrawBuffers(n, bufs);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glClearBufferfi(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum buffer = ar_u32(&r);
  GLint drawbuffer = ar_i32(&r);
  GLfloat depth = ar_f32(&r);
  GLint stencil = ar_i32(&r);

  glClearBufferfi(buffer, drawbuffer, depth, stencil);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glClearBufferfv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum buffer = ar_u32(&r);
  GLint drawbuffer = ar_i32(&r);

  GLfloat value[4];
  value[0] = ar_f32(&r);
  value[1] = ar_f32(&r);
  value[2] = ar_f32(&r);
  value[3] = ar_f32(&r);

  glClearBufferfv(buffer, drawbuffer, value);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glClearBufferiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum buffer = ar_u32(&r);
  GLint drawbuffer = ar_i32(&r);

  GLint value[4];
  value[0] = ar_i32(&r);
  value[1] = ar_i32(&r);
  value[2] = ar_i32(&r);
  value[3] = ar_i32(&r);

  glClearBufferiv(buffer, drawbuffer, value);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glClearBufferuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum buffer = ar_u32(&r);
  GLint drawbuffer = ar_i32(&r);

  GLuint value[4];
  value[0] = ar_u32(&r);
  value[1] = ar_u32(&r);
  value[2] = ar_u32(&r);
  value[3] = ar_u32(&r);

  glClearBufferuiv(buffer, drawbuffer, value);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glTexBuffer(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLenum internalformat = ar_u32(&r);
  GLuint buffer = ar_u32(&r);

  glTexBuffer(target, internalformat, buffer);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glGetBufferParameteri64v(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint64 value = 0;

  glGetBufferParameteri64v(target, pname, &value);

  memcpy(C->result_buf, &value, sizeof(GLint64));
  C->result_buf_len = sizeof(GLint64);
  C->result = 0;
}

void h_glGetInteger64i_v(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLuint index = ar_u32(&r);

  GLint64 value = 0;

  LOAD_GLES_FUNC_DLSYM(PFNGLGETINTEGER64I_VPROC, glGetInteger64i_v);
  if (p_glGetInteger64i_v)
    p_glGetInteger64i_v(target, index, &value);
  else
    log_error("p_glGetInteger64i_v not found!");

  memcpy(C->result_buf, &value, sizeof(GLint64));
  C->result_buf_len = sizeof(GLint64);
  C->result = 0;
}

void h_glFramebufferTexture(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLenum attachment = ar_u32(&r);
  GLuint texture = ar_u32(&r);
  GLint level = ar_i32(&r);

  glFramebufferTexture(target, attachment, texture, level);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glGetInternalformativ(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLenum internalformat = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLint *tmp = alloca(sizeof(GLint) * bufSize);

  glGetInternalformativ(target, internalformat, pname, bufSize, tmp);

  memcpy(C->result_buf, tmp, sizeof(GLint) * bufSize);
  C->result_buf_len = sizeof(GLint) * bufSize;
  C->result = 0;
}

void h_glGetIntegeri_v(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLuint index = ar_u32(&r);

  GLint value = 0;

  LOAD_GLES_FUNC_DLSYM(PFNGLGETINTEGERI_VPROC, glGetIntegeri_v);
  if (p_glGetIntegeri_v)
    p_glGetIntegeri_v(target, index, &value);
  else
    log_error("glGetIntegeri_v not found!");

  memcpy(C->result_buf, &value, sizeof(GLint));
  C->result_buf_len = sizeof(GLint);
  C->result = 0;
}

void h_glGetSamplerParameterIiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint tmp[4];

  glGetSamplerParameterIiv(sampler, pname, tmp);

  memcpy(C->result_buf, tmp, sizeof(tmp));
  C->result_buf_len = sizeof(tmp);
  C->result = 0;
}

void h_glGetSamplerParameterIuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLuint tmp[4];

  glGetSamplerParameterIuiv(sampler, pname, tmp);

  memcpy(C->result_buf, tmp, sizeof(tmp));
  C->result_buf_len = sizeof(tmp);
  C->result = 0;
}

void h_glSamplerParameterIuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint sampler = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLuint param[4];
  param[0] = ar_u32(&r);
  param[1] = ar_u32(&r);
  param[2] = ar_u32(&r);
  param[3] = ar_u32(&r);

  glSamplerParameterIuiv(sampler, pname, param);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glProgramBinary(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint program = ar_u32(&r);
  GLenum binaryFormat = ar_u32(&r);
  GLsizei length = ar_i32(&r);

  const void *binary = NULL;
  if (C->data_size && length > 0)
    binary = dp(C->data_offset);

#ifdef DEBUG_SHADERS
  log_console("h_glProgramBinary: program=%u format=0x%x length=%d", program,
              binaryFormat, length);

  GLint formats_count = 0;
  glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats_count);
  log_console("h_glProgramBinary: driver supports %d binary formats",
              formats_count);
  GLint *formats = alloca(formats_count * sizeof(GLint));
  glGetIntegerv(GL_PROGRAM_BINARY_FORMATS, formats);
  for (int i = 0; i < formats_count; i++)
    log_console("  format[%d] = 0x%x", i, formats[i]);
  log_console("  requested = 0x%x (%s)", binaryFormat,
              binaryFormat == 0x8f61 ? "GL_PROGRAM_BINARY_OES" : "unknown");

  uint32_t crc = 0;
  if (binary && length > 0)
    for (int i = 0; i < length; i++)
      crc += ((const uint8_t *)binary)[i];
  log_console("h_glProgramBinary proxy: len=%d crc=0x%08x", length, crc);

  GLenum prev_err = glGetError();
#endif

  if ((GLsizei)C->data_size != length)
    log_error("h_glProgramBinary: SIZE MISMATCH data_size=%zu vs length=%d",
              C->data_size, length);

  glProgramBinary(program, binaryFormat, binary, length);

#ifdef DEBUG_SHADERS
  GLenum err = glGetError();

  GLint link_status = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);

  GL_LOG_IF_ERR("h_glProgramBinary: prev_err=0x%x err=0x%x", prev_err,
                after_err);

  log_console("h_glProgramBinary: link_status=%d", link_status);

  GLint retrieved_length = 0;
  glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &retrieved_length);
  log_console("h_glProgramBinary: driver says program binary length = %d, "
              "we were given %d — %s",
              retrieved_length, length,
              retrieved_length == length ? "MATCH" : "MISMATCH");
#endif

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glProgramParameteri(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint program = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLint value = ar_i32(&r);

  glProgramParameteri(program, pname, value);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glTexStorage3DMultisample(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLsizei samples = ar_i32(&r);
  GLenum internalformat = ar_u32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);
  GLsizei depth = ar_i32(&r);
  GLboolean fixedsamplelocations = (GLboolean)ar_u32(&r);

  glTexStorage3DMultisample(target, samples, internalformat, width, height,
                            depth, fixedsamplelocations);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDrawElementsBaseVertex(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum mode = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum type = ar_u32(&r);
  uint64_t indices_raw = ar_u64(&r);
  GLint basevertex = ar_i32(&r);
  uint32_t index_mode = ar_u32(&r);

  uint32_t ebo_piggyback_buffer = ar_u32(&r);
  uint32_t ebo_piggyback_offset = ar_u32(&r);
  uint32_t ebo_piggyback_length = ar_u32(&r);

  uint32_t piggyback_buffer = ar_u32(&r);
  uint32_t piggyback_offset = ar_u32(&r);
  uint32_t piggyback_length = ar_u32(&r);

  if (ebo_piggyback_buffer && C->data_size > 0)
  {
    for (uint32_t mi = 1; mi < MAX_MAPS; mi++)
    {
      if (!maps[mi].real_ptr || maps[mi].buffer != ebo_piggyback_buffer)
        continue;
      if ((GLintptr)ebo_piggyback_offset < maps[mi].offset ||
          (GLintptr)(ebo_piggyback_offset + ebo_piggyback_length) >
              maps[mi].offset + maps[mi].length)
        break;

      uint8_t *dst = (uint8_t *)maps[mi].real_ptr +
                     ((GLintptr)ebo_piggyback_offset - maps[mi].offset);
      memcpy(dst, dp(C->data_offset), C->data_size);

#ifdef DEBUG_VERBOSE
      log_console("h_glDrawElementsBaseVertex: piggyback wrote %u bytes into "
                  "EBO=%u map at buffer-offset=%u",
                  C->data_size, ebo_piggyback_buffer, ebo_piggyback_offset);
#endif
      break;
    }
  }

  /* If the stub piggybacked a persistently-mapped VBO's dirty range (no
   * GL_MAP_FLUSH_EXPLICIT_BIT, so the GPU-mapped buffer was never updated
   * via unmap/flush), copy it into the real mapped pointer now, before the
   * draw reads from it */
  if (piggyback_buffer && C->data2_size > 0)
  {
    for (uint32_t mi = 1; mi < MAX_MAPS; mi++)
    {
      if (!maps[mi].real_ptr || maps[mi].buffer != piggyback_buffer)
        continue;
      if ((GLintptr)piggyback_offset < maps[mi].offset ||
          (GLintptr)(piggyback_offset + piggyback_length) >
              maps[mi].offset + maps[mi].length)
        break; /* out of range — stale/mismatched map, skip */

      uint8_t *dst = (uint8_t *)maps[mi].real_ptr +
                     ((GLintptr)piggyback_offset - maps[mi].offset);
      memcpy(dst, dp(C->data2_offset), C->data2_size);

#ifdef DEBUG_VERBOSE
      log_console("h_glDrawElementsBaseVertex: piggyback wrote %u bytes into "
                  "VBO=%u map at buffer-offset=%u",
                  C->data2_size, piggyback_buffer, piggyback_offset);
#endif
      break;
    }
  }

  GLContextState *ctx = &g_proxy_ctx[g_current_ctx];
  VAOState *vao = &ctx->vaos[ctx->current_vao];

#ifdef DEBUG_VERBOSE
  log_console(
      "h_glDrawElementsBaseVertex (tid=%ld): mode=0x%04x count=%d type=0x%04x "
      "indices=0x%lx basevertex=%d index_mode=%d g_current_ctx=%d",
      syscall(SYS_gettid), mode, count, type,
      (unsigned long)(uintptr_t)indices_raw, basevertex, index_mode,
      g_current_ctx);

  GLint vbo = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);

  GLint vboSize = 0;
  if (vbo != 0)
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &vboSize);

  GLint stride = 0;
  glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);

  log_console("[h_glDrawElementsBaseVertex] VBO=%d size=%d stride=%d "
              "basevertex=%d maxIndex=%d",
              vbo, vboSize, stride, basevertex, basevertex + 65535);

  for (GLuint i = 0; i < MAX_VERTEX_ATTRIBS; ++i)
  {
    GLint enabled = 0;
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
    log_console("[h_glDrawElementsBaseVertex] attrib%u enabled=%d", i, enabled);
  }

  GLint vp[4] = {0};
  glGetIntegerv(GL_VIEWPORT, vp);
  log_console("[h_glDrawElementsBaseVertex] viewport = %d %d %d %d", vp[0],
              vp[1], vp[2], vp[3]);

  GLint sc[4] = {0};
  glGetIntegerv(GL_SCISSOR_BOX, sc);
  GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
  log_console("[h_glDrawElementsBaseVertex] scissor = %d %d %d %d enabled=%d",
              sc[0], sc[1], sc[2], sc[3], scissorEnabled);
  GLint drawFBO = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
  log_console("[h_glDrawElementsBaseVertex] drawFBO = %d", drawFBO);
  log_console("DRAW: VAO=%u", ctx->current_vao);

  for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
  {
    AttribState *a = &vao->attribs[i];
    if (a->enabled)
      log_console("VAO %u ATTR %d: enabled=%d vbo=%u ptr=0x%lx size=%d "
                  "stride=%d type=0x%x integer=%d divisor=%d",
                  ctx->current_vao, i, a->enabled, a->vbo, a->pointer, a->size,
                  a->stride, a->type, a->integer, a->divisor);
  }

  for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
  {
    GLint en = 0;
    glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &en);
    if (en)
      log_console("GL ATTR %d: ENABLED", i);
  }

  GLint ebo = 0;
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);
  log_console("EBO=%d", ebo);
#endif

  GLint prev_array_buffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);
  bool touched_array_buffer = false;

  // Rebuild client-array attribs only (vbo==0).
  // VBO-backed attribs inside a named VAO are already correct in GPU state;
  for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
  {
    AttribState *a = &vao->attribs[i];

    if (!a->enabled)
      continue;

    // VBO-backed: GPU VAO already has the correct binding, skip.
    if (a->vbo != 0)
      continue;

    touched_array_buffer = true;
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const void *ptr = (const void *)a->pointer;

    if (a->integer)
      glVertexAttribIPointer(i, a->size, a->type, a->stride, ptr);
    else
      glVertexAttribPointer(i, a->size, a->type, a->normalized, a->stride, ptr);
  }

#ifdef DEBUG_VERBOSE
  GLboolean cm[4];
  glGetBooleanv(GL_COLOR_WRITEMASK, cm);
  GLint depth_test = glIsEnabled(GL_DEPTH_TEST);
  GLint blend = glIsEnabled(GL_BLEND);

  log_console("[STATE] colorMask=%d%d%d%d depth=%d blend=%d", cm[0], cm[1],
              cm[2], cm[3], depth_test, blend);

#endif

  if (index_mode == IDX_MODE_OFFSET)
  {
#ifdef DEBUG_VERBOSE
    log_console("h_glDrawElementsBaseVertex: ebo mode");
#endif
    const void *indices = (const void *)(uintptr_t)indices_raw;
    glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
  }
  else
  {
#ifdef DEBUG_VERBOSE
    log_console("h_glDrawElementsBaseVertex: alt mode");
#endif
    const void *indices = dp((uint32_t)indices_raw);

    glDrawElementsBaseVertex(mode, count, type, indices, basevertex);
  }

#ifdef DEBUG_VERBOSE
  performRenderingChecklist("h_glDrawElementsBaseVertex");

  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo);

  GLint bufSize = 0;
  glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufSize);

  log_console("h_glDrawElementsBaseVertex EBO=%d size=%d indices_offset=%u "
              "count=%d type=0x%04x",
              ebo, bufSize, (unsigned)indices_raw, count, type);

#endif

#ifdef DEBUG_VERBOSE
  {
    GLubyte px[4] = {0, 0, 0, 0};
    GLint vp[4] = {0};
    glGetIntegerv(GL_VIEWPORT, vp);

    glReadPixels(vp[0] + vp[2] / 2, vp[1] + vp[3] / 2, 1, 1, GL_RGBA,
                 GL_UNSIGNED_BYTE, px);

    log_console("[PROBE] center pixel = %u %u %u %u", px[0], px[1], px[2],
                px[3]);
  }
#endif

  C->result = 0;
  C->result_buf_len = 0;

  if (touched_array_buffer)
    glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
}

void h_glDrawElementsInstancedBaseVertex(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum mode = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum type = ar_u32(&r);
  const void *indices = (const void *)(uintptr_t)ar_u64(&r);
  GLsizei instancecount = ar_i32(&r);
  GLint basevertex = ar_i32(&r);

  glDrawElementsInstancedBaseVertex(mode, count, type, indices, instancecount,
                                    basevertex);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDrawRangeElementsBaseVertex(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum mode = ar_u32(&r);
  GLuint start = ar_u32(&r);
  GLuint end = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum type = ar_u32(&r);
  const void *indices = (const void *)(uintptr_t)ar_u64(&r);
  GLint basevertex = ar_i32(&r);

  glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices,
                                basevertex);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glMinSampleShading(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLfloat value = ar_f32(&r);

  glMinSampleShading(value);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDebugMessageInsert(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum source = ar_u32(&r);
  GLenum type = ar_u32(&r);
  GLuint id = ar_u32(&r);
  GLenum severity = ar_u32(&r);
  GLsizei length = ar_i32(&r);

  const GLchar *buf = (const GLchar *)D;

  glDebugMessageInsert(source, type, id, severity, length, buf);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glGetDebugMessageLog(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint count = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLenum *sources = alloca(sizeof(GLenum) * count);
  GLenum *types = alloca(sizeof(GLenum) * count);
  GLuint *ids = alloca(sizeof(GLuint) * count);
  GLenum *severities = alloca(sizeof(GLenum) * count);
  GLsizei *lengths = alloca(sizeof(GLsizei) * count);
  GLchar *log = alloca(bufSize);

  GLuint num = glGetDebugMessageLog(count, bufSize, sources, types, ids,
                                    severities, lengths, log);

  uint8_t *out = C->result_buf;
  memcpy(out, &num, sizeof(GLuint));
  out += sizeof(GLuint);

  for (GLuint i = 0; i < num; i++)
  {
    memcpy(out, &sources[i], sizeof(GLenum));
    out += sizeof(GLenum);
    memcpy(out, &types[i], sizeof(GLenum));
    out += sizeof(GLenum);
    memcpy(out, &ids[i], sizeof(GLuint));
    out += sizeof(GLuint);
    memcpy(out, &severities[i], sizeof(GLenum));
    out += sizeof(GLenum);
    memcpy(out, &lengths[i], sizeof(GLsizei));
    out += sizeof(GLsizei);

    memcpy(out, log, lengths[i]);
    out += lengths[i];
    log += lengths[i];
  }

  C->result_buf_len = out - C->result_buf;
  C->result = 0;
}

void h_glGetObjectLabel(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum identifier = ar_u32(&r);
  GLuint name = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLchar *tmp = alloca(bufSize);
  GLsizei len = 0;

  glGetObjectLabel(identifier, name, bufSize, &len, tmp);

  memcpy(C->result_buf, &len, sizeof(GLsizei));
  memcpy(C->result_buf + sizeof(GLsizei), tmp, len);

  C->result_buf_len = sizeof(GLsizei) + len;
  C->result = 0;
}

void h_glGetObjectPtrLabel(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  const void *ptr = (const void *)(uintptr_t)ar_u64(&r);
  GLsizei bufSize = ar_i32(&r);

  GLchar *tmp = alloca(bufSize);
  GLsizei len = 0;

  glGetObjectPtrLabel(ptr, bufSize, &len, tmp);

  memcpy(C->result_buf, &len, sizeof(GLsizei));
  memcpy(C->result_buf + sizeof(GLsizei), tmp, len);

  C->result_buf_len = sizeof(GLsizei) + len;
  C->result = 0;
}

void h_glPushDebugGroup(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum source = ar_u32(&r);
  GLuint id = ar_u32(&r);
  GLsizei length = ar_i32(&r);

  const GLchar *message = (const GLchar *)D;

  glPushDebugGroup(source, id, length, message);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glCopyImageSubData(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint srcName = ar_u32(&r);
  GLenum srcTarget = ar_u32(&r);
  GLint srcLevel = ar_i32(&r);
  GLint srcX = ar_i32(&r);
  GLint srcY = ar_i32(&r);
  GLint srcZ = ar_i32(&r);

  GLuint dstName = ar_u32(&r);
  GLenum dstTarget = ar_u32(&r);
  GLint dstLevel = ar_i32(&r);
  GLint dstX = ar_i32(&r);
  GLint dstY = ar_i32(&r);
  GLint dstZ = ar_i32(&r);

  GLsizei srcWidth = ar_i32(&r);
  GLsizei srcHeight = ar_i32(&r);
  GLsizei srcDepth = ar_i32(&r);

  glCopyImageSubData(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName,
                     dstTarget, dstLevel, dstX, dstY, dstZ, srcWidth, srcHeight,
                     srcDepth);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glMemoryBarrier(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLbitfield barriers = ar_u32(&r);

  glMemoryBarrier(barriers);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDispatchCompute(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint x = ar_u32(&r);
  GLuint y = ar_u32(&r);
  GLuint z = ar_u32(&r);

  glDispatchCompute(x, y, z);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDispatchComputeIndirect(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLintptr indirect = (GLintptr)ar_u64(&r);

  glDispatchComputeIndirect(indirect);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glTexStorage2DMultisample(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLsizei samples = ar_i32(&r);
  GLenum internalformat = ar_u32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);
  GLboolean fixedsamplelocations = (GLboolean)ar_u32(&r);

  glTexStorage2DMultisample(target, samples, internalformat, width, height,
                            fixedsamplelocations);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glObjectLabel(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum identifier = ar_u32(&r);
  GLuint name = ar_u32(&r);
  GLsizei length = ar_i32(&r);

  const GLchar *label = (const GLchar *)D;

  glObjectLabel(identifier, name, length, label);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glObjectPtrLabel(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  const void *ptr = (const void *)(uintptr_t)ar_u64(&r);
  GLsizei length = ar_i32(&r);

  const GLchar *label = (const GLchar *)D;

  glObjectPtrLabel(ptr, length, label);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glPrimitiveBoundingBox(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLfloat minX = ar_f32(&r);
  GLfloat minY = ar_f32(&r);
  GLfloat minZ = ar_f32(&r);
  GLfloat minW = ar_f32(&r);
  GLfloat maxX = ar_f32(&r);
  GLfloat maxY = ar_f32(&r);
  GLfloat maxZ = ar_f32(&r);
  GLfloat maxW = ar_f32(&r);

  glPrimitiveBoundingBox(minX, minY, minZ, minW, maxX, maxY, maxZ, maxW);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glUseProgramStages(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint pipeline = ar_u32(&r);
  GLbitfield stages = ar_u32(&r);
  GLuint program = ar_u32(&r);

  glUseProgramStages(pipeline, stages, program);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glBindProgramPipeline(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint pipeline = ar_u32(&r);

  glBindProgramPipeline(pipeline);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glGenProgramPipelines(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLsizei n = ar_i32(&r);
  GLuint *tmp = alloca(sizeof(GLuint) * n);

  glGenProgramPipelines(n, tmp);

  memcpy(C->result_buf, tmp, sizeof(GLuint) * n);
  C->result_buf_len = sizeof(GLuint) * n;
  C->result = 0;
}

void h_glDeleteProgramPipelines(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLsizei n = ar_i32(&r);
  GLuint *tmp = alloca(sizeof(GLuint) * n);

  for (int i = 0; i < n; i++)
    tmp[i] = ar_u32(&r);

  glDeleteProgramPipelines(n, tmp);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glIsProgramPipeline(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint pipeline = ar_u32(&r);

  GLboolean result = glIsProgramPipeline(pipeline);

  memcpy(C->result_buf, &result, sizeof(GLboolean));
  C->result_buf_len = sizeof(GLboolean);
  C->result = 0;
}

void h_glDrawArraysIndirect(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum mode = ar_u32(&r);
  const void *indirect = (const void *)(uintptr_t)ar_u64(&r);

  glDrawArraysIndirect(mode, indirect);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glDrawElementsIndirect(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum mode = ar_u32(&r);
  GLenum type = ar_u32(&r);
  const void *indirect = (const void *)(uintptr_t)ar_u64(&r);

  glDrawElementsIndirect(mode, type, indirect);

  C->result = 0;
  C->result_buf_len = 0;
}

// -----------------------------
// UniformMatrix* (GLES 3.0)
// -----------------------------
void h_glUniformMatrix2x3fv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint loc = ar_i32(&r);
  GLsizei count = ar_i32(&r);
  GLboolean transpose = ar_u32(&r);

  glUniformMatrix2x3fv(loc, count, transpose,
                       (const GLfloat *)dp(C->data_offset));

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glUniformMatrix3x2fv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint loc = ar_i32(&r);
  GLsizei count = ar_i32(&r);
  GLboolean transpose = ar_u32(&r);

  glUniformMatrix3x2fv(loc, count, transpose,
                       (const GLfloat *)dp(C->data_offset));

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glUniformMatrix2x4fv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint loc = ar_i32(&r);
  GLsizei count = ar_i32(&r);
  GLboolean transpose = ar_u32(&r);

  glUniformMatrix2x4fv(loc, count, transpose,
                       (const GLfloat *)dp(C->data_offset));

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glUniformMatrix4x2fv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint loc = ar_i32(&r);
  GLsizei count = ar_i32(&r);
  GLboolean transpose = ar_u32(&r);

  glUniformMatrix4x2fv(loc, count, transpose,
                       (const GLfloat *)dp(C->data_offset));

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glUniformMatrix3x4fv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint loc = ar_i32(&r);
  GLsizei count = ar_i32(&r);
  GLboolean transpose = ar_u32(&r);

  glUniformMatrix3x4fv(loc, count, transpose,
                       (const GLfloat *)dp(C->data_offset));

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glUniformMatrix4x3fv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint loc = ar_i32(&r);
  GLsizei count = ar_i32(&r);
  GLboolean transpose = ar_u32(&r);

  glUniformMatrix4x3fv(loc, count, transpose,
                       (const GLfloat *)dp(C->data_offset));

  C->result = 0;
  C->result_buf_len = 0;
}

// -----------------------------
// Program Interface (GLES 3.1)
// -----------------------------
void h_glGetProgramInterfaceiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLenum iface = ar_u32(&r);
  GLenum pname = ar_u32(&r);

  GLint outv = 0;

  glGetProgramInterfaceiv(program, iface, pname, &outv);

  memcpy(C->result_buf, &outv, sizeof(GLint));
  C->result_buf_len = sizeof(GLint);
  C->result = 0;
}

void h_glGetProgramResourceIndex(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint program = ar_u32(&r);
  GLenum iface = ar_u32(&r);

  const GLchar *name = NULL;
  if (C->data_size > 0)
    name = (const GLchar *)dp(C->data_offset);

  GLuint idx = glGetProgramResourceIndex(program, iface, name);

  memcpy(C->result_buf, &idx, sizeof(GLuint));
  C->result_buf_len = sizeof(GLuint);
  C->result = 0;
}

void h_glGetProgramResourceName(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLenum iface = ar_u32(&r);
  GLuint index = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLsizei length = 0;
  GLchar *tmp = alloca(bufSize);

  glGetProgramResourceName(program, iface, index, bufSize, &length, tmp);

  uint8_t *out = C->result_buf;
  memcpy(out, &length, sizeof(GLsizei));
  out += sizeof(GLsizei);
  memcpy(out, tmp, length);

  C->result_buf_len = sizeof(GLsizei) + length;
  C->result = 0;
}

void h_glGetProgramResourceiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLenum iface = ar_u32(&r);
  GLuint index = ar_u32(&r);
  GLsizei propCount = ar_i32(&r);
  GLsizei bufSize = ar_i32(&r);

  const GLenum *props = (propCount > 0 && C->data_size > 0)
                            ? (const GLenum *)dp(C->data_offset)
                            : NULL;

  GLint *vals = alloca(sizeof(GLint) * (bufSize > 0 ? bufSize : 1));
  GLsizei length = 0;

  glGetProgramResourceiv(program, iface, index, propCount, props, bufSize,
                         &length, vals);

  uint8_t *out = C->result_buf;
  memcpy(out, &length, sizeof(GLsizei));
  out += sizeof(GLsizei);
  memcpy(out, vals, sizeof(GLint) * length);

  C->result_buf_len = sizeof(GLsizei) + sizeof(GLint) * length;
  C->result = 0;
}

void h_glGetProgramResourceLocation(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint program = ar_u32(&r);
  GLenum iface = ar_u32(&r);

  const GLchar *name = NULL;
  if (C->data_size > 0)
    name = (const GLchar *)dp(C->data_offset);

  GLint loc = glGetProgramResourceLocation(program, iface, name);

  memcpy(C->result_buf, &loc, sizeof(GLint));
  C->result_buf_len = sizeof(GLint);
  C->result = 0;
}

// -----------------------------
// Program Pipeline (GLES 3.1)
// -----------------------------
void h_glActiveShaderProgram(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint pipeline = ar_u32(&r);
  GLuint program = ar_u32(&r);

  glActiveShaderProgram(pipeline, program);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glCreateShaderProgramv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum type = ar_u32(&r);
  GLsizei count = ar_i32(&r);

  /* Rebuild string array from shared data */
  const GLchar **strings = NULL;

  if (C->data_size > 0 && count > 0)
  {
    uint8_t *p = dp(C->data_offset);
    strings = alloca(sizeof(GLchar *) * count);

    for (int i = 0; i < count; i++)
    {
      strings[i] = (const GLchar *)p;
      size_t len = strlen((const char *)p) + 1;
      p += len;
    }
  }

  GLuint prog = glCreateShaderProgramv(type, count, strings);

  memcpy(C->result_buf, &prog, sizeof(GLuint));
  C->result_buf_len = sizeof(GLuint);
  C->result = 0;
}

void h_glValidateProgramPipeline(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint pipeline = ar_u32(&r);

  glValidateProgramPipeline(pipeline);

  C->result = 0;
  C->result_buf_len = 0;
}

void h_glGetProgramPipelineInfoLog(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint pipeline = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  GLsizei length = 0;
  GLchar *tmp = alloca(bufSize);

  glGetProgramPipelineInfoLog(pipeline, bufSize, &length, tmp);

  uint8_t *out = C->result_buf;
  memcpy(out, &length, sizeof(GLsizei));
  out += sizeof(GLsizei);
  memcpy(out, tmp, length);

  C->result_buf_len = sizeof(GLsizei) + length;
  C->result = 0;
}

// -----------------------------
// MemoryBarrierByRegion (GLES 3.1)
// -----------------------------
void h_glMemoryBarrierByRegion(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLbitfield barriers = ar_u32(&r);

  glMemoryBarrierByRegion(barriers);

  C->result = 0;
  C->result_buf_len = 0;
}

// -----------------------------
// BlendBarrier (KHR extension)
// -----------------------------
void h_glBlendBarrier(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  glBlendBarrier();

  C->result = 0;
  C->result_buf_len = 0;
}

// -----------------------------
// Robustness (GLES 3.2)
// -----------------------------
void h_glGetGraphicsResetStatus(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  GLenum st = glGetGraphicsResetStatus();

  memcpy(C->result_buf, &st, sizeof(GLenum));
  C->result_buf_len = sizeof(GLenum);
  C->result = 0;
}

void h_glReadnPixels(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLint x = ar_i32(&r);
  GLint y = ar_i32(&r);
  GLsizei w = ar_i32(&r);
  GLsizei h = ar_i32(&r);
  GLenum format = ar_u32(&r);
  GLenum type = ar_u32(&r);
  GLsizei bufSize = ar_i32(&r);

  void *tmp = alloca(bufSize);

  glReadnPixels(x, y, w, h, format, type, bufSize, tmp);

  memcpy(C->result_buf, tmp, bufSize);
  C->result_buf_len = bufSize;
  C->result = 0;
}

void h_glGetnUniformfv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLint loc = ar_i32(&r);
  GLsizei bufSize = ar_i32(&r);

  void *tmp = alloca(bufSize);

  glGetnUniformfv(program, loc, bufSize, tmp);

  memcpy(C->result_buf, tmp, bufSize);
  C->result_buf_len = bufSize;
  C->result = 0;
}

void h_glGetnUniformiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLint loc = ar_i32(&r);
  GLsizei bufSize = ar_i32(&r);

  void *tmp = alloca(bufSize);

  glGetnUniformiv(program, loc, bufSize, tmp);

  memcpy(C->result_buf, tmp, bufSize);
  C->result_buf_len = bufSize;
  C->result = 0;
}

void h_glGetnUniformuiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLuint program = ar_u32(&r);
  GLint loc = ar_i32(&r);
  GLsizei bufSize = ar_i32(&r);

  void *tmp = alloca(bufSize);

  glGetnUniformuiv(program, loc, bufSize, tmp);

  memcpy(C->result_buf, tmp, bufSize);
  C->result_buf_len = bufSize;
  C->result = 0;
}

// -----------------------------
// PatchParameteri (GLES 3.2)
// -----------------------------
void h_glPatchParameteri(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum pname = ar_u32(&r);
  GLint value = ar_i32(&r);

  glPatchParameteri(pname, value);

  C->result = 0;
  C->result_buf_len = 0;
}

// -----------------------------
// TexBufferRange (GLES 3.2)
// -----------------------------
void h_glTexBufferRange(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum target = ar_u32(&r);
  GLenum internalformat = ar_u32(&r);
  GLuint buffer = ar_u32(&r);
  GLintptr offset = (GLintptr)ar_u64(&r);
  GLsizeiptr size = (GLsizeiptr)ar_u64(&r);

  glTexBufferRange(target, internalformat, buffer, offset, size);

  C->result = 0;
  C->result_buf_len = 0;
}
