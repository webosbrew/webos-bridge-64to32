/*
 * gles2_stub.c  —  aarch64 implementation of every GL ES 2.0 entry point.
 *
 * Each function:
 *   1. Calls BRIDGE_BEGIN()  — locks, ensures init.
 *   2. Fills BridgeCtrl (opcode, packed scalar args, optional data pointer).
 *   3. Calls BRIDGE_SEND_VOID() or BRIDGE_SEND_CALL().
 *
 * Patterns used:
 *   A — pure scalar args, void return         (e.g. glEnable)
 *   B — scalar args, scalar return            (e.g. glCreateShader)
 *   C — scalar args + pointer IN (data shm)   (e.g. glBufferData)
 *   D — scalar args + pointer OUT (data shm)  (e.g. glGetIntegerv)
 *   E — scalar args + pointer IN+OUT          (e.g. glReadPixels)
 *   F — string in (shader source etc.)
 *   G — n-object gen/delete arrays
 */

#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <alloca.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_PREFIX "[gles_stub]"
#include "../bridge/shared_util.h"
#include "bridge_core.h"
#include "gles_util_stub.h"

GLState stub_gl_state;

AttribState g_attrib_stub_state[MAX_VERTEX_ATTRIBS];
GLContextState g_stub_ctx[MAX_CONTEXTS];

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

#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
/* ── Uniform/attrib location cache ───────────────────────────────────────── */
#define LOC_CACHE_SLOTS 4096
#define LOC_CACHE_MASK (LOC_CACHE_SLOTS - 1)
#define LOC_CACHE_NAME_MAX 64

typedef struct
{
  GLuint program; /* 0 = empty slot (GL program names are nonzero) */
  char name[LOC_CACHE_NAME_MAX];
  GLint location;
  uint8_t is_attrib; /* separate namespace from uniforms */
} LocCacheEntry;

static LocCacheEntry g_loc_cache[LOC_CACHE_SLOTS];

static uint32_t loc_hash(GLuint program, const char *name, uint8_t is_attrib)
{
  uint32_t h = program * 2654435761u + (uint32_t)is_attrib * 0x9e3779b9u;
  for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    h = h * 131u + *p;
  return h;
}

static int loc_cache_lookup(GLuint program, const char *name, uint8_t is_attrib,
                            GLint *out)
{
  uint32_t h = loc_hash(program, name, is_attrib) & LOC_CACHE_MASK;
  for (uint32_t i = 0; i < LOC_CACHE_SLOTS; i++)
  {
    LocCacheEntry *e = &g_loc_cache[(h + i) & LOC_CACHE_MASK];
    if (e->program == 0)
      return 0;
    if (e->program == program && e->is_attrib == is_attrib &&
        strncmp(e->name, name, LOC_CACHE_NAME_MAX) == 0)
    {
      *out = e->location;
      return 1;
    }
  }
  return 0;
}

static void loc_cache_insert(GLuint program, const char *name,
                             uint8_t is_attrib, GLint location)
{
  uint32_t h = loc_hash(program, name, is_attrib) & LOC_CACHE_MASK;
  for (uint32_t i = 0; i < LOC_CACHE_SLOTS; i++)
  {
    LocCacheEntry *e = &g_loc_cache[(h + i) & LOC_CACHE_MASK];
    if (e->program == 0 ||
        (e->program == program && e->is_attrib == is_attrib &&
         strncmp(e->name, name, LOC_CACHE_NAME_MAX) == 0))
    {
      e->program = program;
      e->is_attrib = is_attrib;
      strncpy(e->name, name, LOC_CACHE_NAME_MAX - 1);
      e->name[LOC_CACHE_NAME_MAX - 1] = '\0';
      e->location = location;
      return;
    }
  }
  /* table full — skip caching this */
}

/* Locations become invalid on relink/binary-load/delete. */
void loc_cache_invalidate_program(GLuint program)
{
  for (uint32_t i = 0; i < LOC_CACHE_SLOTS; i++)
    if (g_loc_cache[i].program == program)
      g_loc_cache[i].program = 0;
}
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * Textures
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL void GL_APIENTRY glActiveTexture(GLenum texture)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->active_texture == texture)
    return;
  s->active_texture = texture;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glActiveTexture);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, texture);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBindTexture(GLenum target, GLuint texture)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  GLuint *slot = shadow_tex_slot(s, target, s->active_texture);
  if (slot)
  {
    if (*slot == texture)
      return;
    *slot = texture;
  }
#endif

#ifdef DEBUG_VERBOSE
  log_console("glBindTexture: target=%d tex=%u g_stub_current_ctx=%d", target,
              texture, g_stub_current_ctx);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindTexture);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, texture);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGenTextures(GLsizei n, GLuint *textures)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGenTextures;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;
  uint32_t out_off = bridge_data_write(textures, (size_t)n * sizeof(GLuint));
  C->data_offset = out_off;
  C->data_size = (uint32_t)n * sizeof(GLuint);
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_CALL();
  bridge_data_read(textures, out_off, (size_t)n * sizeof(GLuint));

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("glGenTextures: n:%d textures[%d]=%u", n, i, textures[i]);
#endif
}

GL_APICALL void GL_APIENTRY glDeleteTextures(GLsizei n, const GLuint *textures)
{
#ifdef CACHE_GL_STATE
  for (GLsizei i = 0; i < n; i++)
    shadow_invalidate_texture(textures[i]);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glDeleteTextures;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;
  C->data_offset = bridge_data_write(textures, (size_t)n * sizeof(GLuint));
  C->data_size = (uint32_t)n * sizeof(GLuint);
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glTexImage2D(GLenum target, GLint level,
                                         GLint internalformat, GLsizei width,
                                         GLsizei height, GLint border,
                                         GLenum format, GLenum type,
                                         const void *pixels)
{
  size_t pixel_bytes = 0;
  if (pixels && width > 0 && height > 0)
    pixel_bytes = gl_unpack_image_size_2d(width, height, format, type);

#ifdef DEBUG_VERBOSE
  log_console("glTexImage2D: tgt=%d lvl=%d ifmt=%d %dx%d fmt=%d type=%d pix=%p "
              "pixel_bytes=%zu",
              target, level, internalformat, width, height, format, type,
              pixels, pixel_bytes);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glTexImage2D;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_i32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_i32(&W, border);
  aw_u32(&W, format);
  aw_u32(&W, type);
  aw_u32(&W, pixels ? 1u : 0u);
  C->args_len = W.pos;

  if (pixels && pixel_bytes)
  {
    C->data_offset = bridge_data_write(pixels, pixel_bytes);
    C->data_size = (uint32_t)pixel_bytes;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glTexSubImage2D(GLenum target, GLint level,
                                            GLint xoffset, GLint yoffset,
                                            GLsizei width, GLsizei height,
                                            GLenum format, GLenum type,
                                            const void *pixels)
{
#ifdef DEBUG_VERBOSE
  log_console(
      "glTexSubImage2D: tgt=%d lvl=%d x=%d y=%d %dx%d fmt=%d type=%d pix=%p",
      target, level, xoffset, yoffset, width, height, format, type, pixels);
#endif

  size_t pixel_bytes = 0;
  if (pixels && width > 0 && height > 0)
    pixel_bytes = gl_unpack_image_size_2d(width, height, format, type);

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glTexSubImage2D;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_i32(&W, xoffset);
  aw_i32(&W, yoffset);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_u32(&W, format);
  aw_u32(&W, type);

  uint32_t pixel_mode;

  if (!pixels)
  {
    pixel_mode = 0;
    C->data_offset = 0;
    C->data_size = 0;
  }
  else if (stub_gl_state.pixel_unpack_buffer)
  {
    pixel_mode = 1;

    uintptr_t off = (uintptr_t)pixels;
    if (off > UINT32_MAX)
      log_error("WARNING: PBO offset >4GB truncated: %p", pixels);

    C->data_offset = (uint32_t)(uintptr_t)pixels;
    C->data_size = 0;
  }
  else
  {
    pixel_mode = 2;
    C->data_offset = pixel_bytes ? bridge_data_write(pixels, pixel_bytes) : 0;
    C->data_size = (uint32_t)pixel_bytes;
  }

  aw_u32(&W, pixel_mode);
  C->args_len = W.pos;

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glCompressedTexImage2D(
    GLenum target, GLint level, GLenum internalformat, GLsizei width,
    GLsizei height, GLint border, GLsizei imageSize, const void *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glCompressedTexImage2D;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_u32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
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

  aw_u32(&W, pixel_mode);

  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glCompressedTexSubImage2D(
    GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
    GLsizei height, GLenum format, GLsizei imageSize, const void *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glCompressedTexSubImage2D;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_i32(&W, xoffset);
  aw_i32(&W, yoffset);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_u32(&W, format);
  aw_i32(&W, imageSize);

  uint32_t pixel_mode;
  if (!data)
  {
    pixel_mode = 0;
    C->data_offset = 0;
    C->data_size = 0;
  }
  else if (stub_gl_state.pixel_unpack_buffer)
  {
    pixel_mode = 1;
    C->data_offset = (uint32_t)(uintptr_t)data; // raw PBO offset
    C->data_size = 0;
  }
  else
  {
    pixel_mode = 2;
    C->data_offset = bridge_data_write(data, imageSize);
    C->data_size = (uint32_t)imageSize;
  }

  aw_u32(&W, pixel_mode);
  C->args_len = W.pos;

  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glCopyTexImage2D(GLenum target, GLint level,
                                             GLenum internalformat, GLint x,
                                             GLint y, GLsizei width,
                                             GLsizei height, GLint border)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCopyTexImage2D);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_u32(&W, internalformat);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_i32(&W, border);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glCopyTexSubImage2D(GLenum target, GLint level,
                                                GLint xoffset, GLint yoffset,
                                                GLint x, GLint y, GLsizei width,
                                                GLsizei height)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCopyTexSubImage2D);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_i32(&W, level);
  aw_i32(&W, xoffset);
  aw_i32(&W, yoffset);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, width);
  aw_i32(&W, height);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

#define TEX_PARAM_BODY(OP, target, pname, val, aw_fn)                          \
  BRIDGE_BEGIN();                                                              \
  BridgeCtrl *C = BRIDGE_CTRL();                                               \
  setup_scalar(OP);                                                            \
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                            \
  aw_u32(&W, target);                                                          \
  aw_u32(&W, pname);                                                           \
  aw_fn(&W, val);                                                              \
  C->args_len = W.pos;                                                         \
  BRIDGE_SEND_VOID()

GL_APICALL void GL_APIENTRY glTexParameterf(GLenum t, GLenum p, GLfloat v)
{
#ifdef DEBUG_VERBOSE
  log_console("glTexParameterf: target=%d pname=%d param=%f", t, p, v);
#endif
  TEX_PARAM_BODY(OP_glTexParameterf, t, p, v, aw_f32);
}
GL_APICALL void GL_APIENTRY glTexParameteri(GLenum t, GLenum p, GLint v)
{
#ifdef DEBUG_VERBOSE
  log_console("glTexParameteri: target=%d pname=%d param=%d", t, p, v);
#endif
  TEX_PARAM_BODY(OP_glTexParameteri, t, p, v, aw_i32);
}

GL_APICALL void GL_APIENTRY glTexParameterfv(GLenum target, GLenum pname,
                                             const GLfloat *params)
{
#ifdef DEBUG_VERBOSE
  log_console("glTexParameterfv: target=%d pname=%d params=%p", target, pname,
              (void *)params);
#endif

  // Determine element count
  size_t count = 1;
  if (pname == GL_TEXTURE_BORDER_COLOR)
    count = 4;

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glTexParameterfv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  size_t bytes = count * sizeof(GLfloat);
  C->data_offset = bridge_data_write(params, bytes);
  C->data_size = (uint32_t)bytes;

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glTexParameteriv(GLenum target, GLenum pname,
                                             const GLint *params)
{
#ifdef DEBUG_VERBOSE
  log_console("glTexParameteriv: target=%d pname=%d params=%p", target, pname,
              (void *)params);
#endif

  // Determine element count
  size_t count = 1;
  if (pname == GL_TEXTURE_BORDER_COLOR)
    count = 4;

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glTexParameteriv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  size_t bytes = count * sizeof(GLint);
  C->data_offset = bridge_data_write(params, bytes);
  C->data_size = (uint32_t)bytes;

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetTexParameterfv(GLenum target, GLenum pname,
                                                GLfloat *params)
{
#ifdef DEBUG_VERBOSE
  log_console("glGetTexParameterfv: target=%d pname=%d params=%p", target,
              pname, (void *)params);
#endif

  // Determine element count based on pname (GLES 3.2)
  size_t count = 1;
  if (pname == GL_TEXTURE_BORDER_COLOR)
    count = 4;

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetTexParameterfv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  size_t bytes = count * sizeof(GLfloat);

  uint32_t out = bridge_data_write(params, bytes);
  C->data_offset = out;
  C->data_size = (uint32_t)bytes;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (params)
    bridge_data_read(params, out, bytes);
}

GL_APICALL void GL_APIENTRY glGetTexParameteriv(GLenum target, GLenum pname,
                                                GLint *params)
{
#ifdef DEBUG_VERBOSE
  log_console("glGetTexParameteriv: target=%d pname=%d params=%p", target,
              pname, (void *)params);
#endif

  size_t count = 1;
  if (pname == GL_TEXTURE_BORDER_COLOR)
    count = 4;

  size_t bytes = count * sizeof(GLint);

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetTexParameteriv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  uint32_t out = bridge_data_write(params, bytes);
  C->data_offset = out;
  C->data_size = (uint32_t)bytes;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (params)
    bridge_data_read(params, out, bytes);
}

GL_APICALL void GL_APIENTRY glGenerateMipmap(GLenum target)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGenerateMipmap);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glPixelStorei(GLenum pname, GLint param)
{
  switch (pname)
  {
  case GL_UNPACK_ALIGNMENT:
    stub_gl_state.unpack_alignment = param;
    break;
#ifdef GL_UNPACK_ROW_LENGTH
  case GL_UNPACK_ROW_LENGTH:
    stub_gl_state.unpack_row_length = param;
    break;
#endif
#if defined(GL_UNPACK_ROW_LENGTH_EXT) &&                                       \
    (!defined(GL_UNPACK_ROW_LENGTH) ||                                         \
     GL_UNPACK_ROW_LENGTH_EXT != GL_UNPACK_ROW_LENGTH)
  case GL_UNPACK_ROW_LENGTH_EXT:
    stub_gl_state.unpack_row_length = param;
    break;
#endif
#ifdef GL_UNPACK_SKIP_ROWS
  case GL_UNPACK_SKIP_ROWS:
    stub_gl_state.unpack_skip_rows = param;
    break;
#endif
#if defined(GL_UNPACK_SKIP_ROWS_EXT) &&                                        \
    (!defined(GL_UNPACK_SKIP_ROWS) ||                                          \
     GL_UNPACK_SKIP_ROWS_EXT != GL_UNPACK_SKIP_ROWS)
  case GL_UNPACK_SKIP_ROWS_EXT:
    stub_gl_state.unpack_skip_rows = param;
    break;
#endif
#ifdef GL_UNPACK_SKIP_PIXELS
  case GL_UNPACK_SKIP_PIXELS:
    stub_gl_state.unpack_skip_pixels = param;
    break;
#endif
#if defined(GL_UNPACK_SKIP_PIXELS_EXT) &&                                      \
    (!defined(GL_UNPACK_SKIP_PIXELS) ||                                        \
     GL_UNPACK_SKIP_PIXELS_EXT != GL_UNPACK_SKIP_PIXELS)
  case GL_UNPACK_SKIP_PIXELS_EXT:
    stub_gl_state.unpack_skip_pixels = param;
    break;
#endif
#ifdef GL_UNPACK_IMAGE_HEIGHT
  case GL_UNPACK_IMAGE_HEIGHT:
    stub_gl_state.unpack_image_height = param;
    break;
#endif
#ifdef GL_UNPACK_SKIP_IMAGES
  case GL_UNPACK_SKIP_IMAGES:
    stub_gl_state.unpack_skip_images = param;
    break;
#endif
  default:
    break;
  }

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glPixelStorei);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pname);
  aw_i32(&W, param);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Buffers
 * ═══════════════════════════════════════════════════════════════════════════
 */
GL_APICALL void GL_APIENTRY glGenBuffers(GLsizei n, GLuint *buffers)
{
  if (!buffers || n <= 0)
  {
    log_console("[glGenBuffers] INVALID ARGS n=%d buffers=%p", n, buffers);
    return;
  }

#ifdef DEBUG_VERBOSE
  log_console("[glGenBuffers] BEGIN n=%d buffers=%p", n, buffers);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glGenBuffers;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;

  uint32_t out_off = bridge_data_write(buffers, n * sizeof(GLuint));

  C->data_offset = out_off;
  C->data_size = n * sizeof(GLuint);

#ifdef DEBUG_VERBOSE
  log_console("[glGenBuffers] send n=%d off=%u size=%u", n, C->data_offset,
              C->data_size);
#endif

  BRIDGE_SEND_CALL();

  bridge_data_read(buffers, out_off, n * sizeof(GLuint));

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("[glGenBuffers] buffers[%d]=%u", i, buffers[i]);

  log_console("[glGenBuffers] END");
#endif
}

GL_APICALL void GL_APIENTRY glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
#ifdef CACHE_GL_STATE
  for (GLsizei i = 0; i < n; i++)
    bufrange_cache_invalidate_buffer(buffers[i]);
#endif

#ifdef DEBUG_VERBOSE
  log_console("[glDeleteBuffers] n=%d", n);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glDeleteBuffers;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_i32(&W, n);
  C->args_len = W.pos;

  uint32_t off = bridge_data_write(buffers, n * sizeof(GLuint));

  C->data_offset = off;
  C->data_size = n * sizeof(GLuint);

  BRIDGE_SEND_VOID();

  for (GLsizei i = 0; i < n; ++i)
  {
    GLuint b = buffers[i];
    for (uint32_t j = 1; j < MAX_MAPS; ++j)
    {
      StubMapEntry *m = &stub_maps[j];
      if (m->ptr && m->buffer == b)
      {
#ifdef DEBUG_VERBOSE
        log_console("glDeleteBuffers: deleting m->buffer=%d", m->buffer);
#endif
        if (m->ptr)
        {
#ifdef DEBUG_VERBOSE
          log_console("glDeleteBuffers: free m->ptr: %p", m->ptr);
#endif
          free(m->ptr);
        }
        m->ptr = NULL;
        m->length = 0;
        m->offset = 0;
        m->buffer = 0;
        m->target = 0;
        m->id = 0;
      }
    }
  }
}

GL_APICALL void GL_APIENTRY glBindBuffer(GLenum target, GLuint buffer)
{
#ifdef DEBUG_VERBOSE
  log_console("glBindBuffer: target=%d buffer=%u g_stub_current_ctx:%d", target,
              buffer, g_stub_current_ctx);
#endif

  switch (target)
  {
  case GL_ARRAY_BUFFER:
    if (stub_gl_state.array_buffer == buffer)
      return;
    stub_gl_state.array_buffer = buffer;
    break;
  case GL_ELEMENT_ARRAY_BUFFER:
  {
    GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    if (vao->ebo == buffer)
      return;
    vao->ebo = buffer;
    break;
  }
  case GL_PIXEL_PACK_BUFFER:
    if (stub_gl_state.pixel_pack_buffer == buffer)
      return;
    stub_gl_state.pixel_pack_buffer = buffer;
    break;
  case GL_PIXEL_UNPACK_BUFFER:
    if (stub_gl_state.pixel_unpack_buffer == buffer)
      return;
    stub_gl_state.pixel_unpack_buffer = buffer;
    break;
  case GL_UNIFORM_BUFFER:
    if (stub_gl_state.uniform_buffer == buffer)
      return;
    stub_gl_state.uniform_buffer = buffer;
    break;
  case GL_TEXTURE_BUFFER:
    if (stub_gl_state.texture_buffer == buffer)
      return;
    stub_gl_state.texture_buffer = buffer;
    break;
  case GL_TRANSFORM_FEEDBACK_BUFFER:
    if (stub_gl_state.transform_feedback_buffer == buffer)
      return;
    stub_gl_state.transform_feedback_buffer = buffer;
    break;
  case GL_COPY_READ_BUFFER:
    if (stub_gl_state.copy_read_buffer == buffer)
      return;
    stub_gl_state.copy_read_buffer = buffer;
    break;
  case GL_COPY_WRITE_BUFFER:
    if (stub_gl_state.copy_write_buffer == buffer)
      return;
    stub_gl_state.copy_write_buffer = buffer;
    break;
  case GL_SHADER_STORAGE_BUFFER:
    if (stub_gl_state.shader_storage_buffer == buffer)
      return;
    stub_gl_state.shader_storage_buffer = buffer;
    break;
  case GL_ATOMIC_COUNTER_BUFFER:
    if (stub_gl_state.atomic_counter_buffer == buffer)
      return;
    stub_gl_state.atomic_counter_buffer = buffer;
    break;
  case GL_DISPATCH_INDIRECT_BUFFER:
    if (stub_gl_state.dispatch_indirect_buffer == buffer)
      return;
    stub_gl_state.dispatch_indirect_buffer = buffer;
    break;
  case GL_DRAW_INDIRECT_BUFFER:
    if (stub_gl_state.draw_indirect_buffer == buffer)
      return;
    stub_gl_state.draw_indirect_buffer = buffer;
    break;
  default:
#ifdef DEBUG_VERBOSE
    log_console("glBindBuffer: unhandled target=0x%04x buffer=%u", target,
                buffer);
#endif
    break;
  }

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindBuffer);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, buffer);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBufferData(GLenum target, GLsizeiptr size,
                                         const void *data, GLenum usage)
{
#ifdef CACHE_GL_STATE
  bufrange_cache_invalidate_buffer(get_bound_buffer(target));
#endif

#ifdef DEBUG_VERBOSE
  log_console("glBufferData: target:%d size:%d data:%p usage:%d", target, size,
              data, usage);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glBufferData;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u64(&W, (uint64_t)size);
  aw_u32(&W, usage);
  aw_u32(&W, data ? 1u : 0u);
  C->args_len = W.pos;
  if (data && size > 0)
  {
    C->data_offset = bridge_data_write(data, (size_t)size);
    C->data_size = (uint32_t)size;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBufferSubData(GLenum target, GLintptr offset,
                                            GLsizeiptr size, const void *data)
{
#ifdef CACHE_GL_STATE
  bufrange_cache_invalidate_buffer(get_bound_buffer(target));
#endif

#ifdef DEBUG_VERBOSE
  log_console("glBufferSubData: target:%d offset:%d size:%d data:%p", target,
              offset, size, data);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glBufferSubData;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u64(&W, (uint64_t)offset);
  aw_u64(&W, (uint64_t)size);
  C->args_len = W.pos;
  C->data_offset = bridge_data_write(data, (size_t)size);
  C->data_size = (uint32_t)size;
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetBufferParameteriv(GLenum target, GLenum pname,
                                                   GLint *data)
{
#ifdef DEBUG_VERBOSE
  log_console("glGetBufferParameteriv: target=%u pname=%u data=%p", target,
              pname, (void *)data);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetBufferParameteriv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  // allocate 1 GLint in shared memory
  uint32_t out = bridge_data_write(data, sizeof(GLint));
  C->data_offset = out;
  C->data_size = sizeof(GLint);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  // read back the proxy-written value
  if (data)
    bridge_data_read(data, out, sizeof(GLint));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Framebuffers & Renderbuffers
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL void GL_APIENTRY glGenFramebuffers(GLsizei n, GLuint *fbs)
{
  if (!fbs || n <= 0)
  {
    log_console("[glGenFramebuffers] INVALID");
    return;
  }

#ifdef DEBUG_VERBOSE
  log_console("[glGenFramebuffers] BEGIN n=%d", n);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glGenFramebuffers;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;

  uint32_t out_off = bridge_data_write(fbs, n * sizeof(GLuint));

  C->data_offset = out_off;
  C->data_size = n * sizeof(GLuint);

  BRIDGE_SEND_CALL();

  bridge_data_read(fbs, out_off, n * sizeof(GLuint));

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("[glGenFramebuffers] fbs[%d]=%u", i, fbs[i]);

  log_console("[glGenFramebuffers] END");
#endif
}

GL_APICALL void GL_APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint *fbs)
{
#ifdef DEBUG_VERBOSE
  log_console("[glDeleteFramebuffers] n=%d", n);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glDeleteFramebuffers;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_i32(&W, n);
  C->args_len = W.pos;

  uint32_t off = bridge_data_write(fbs, n * sizeof(GLuint));

  C->data_offset = off;
  C->data_size = n * sizeof(GLuint);

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();

  if (target == GL_FRAMEBUFFER)
  {
    if (s->draw_fb == framebuffer && s->read_fb == framebuffer)
      return;
    s->draw_fb = s->read_fb = framebuffer;
  }
#ifdef GL_DRAW_FRAMEBUFFER
  else if (target == GL_DRAW_FRAMEBUFFER)
  {
    if (s->draw_fb == framebuffer)
      return;
    s->draw_fb = framebuffer;
  }
#endif
#ifdef GL_READ_FRAMEBUFFER
  else if (target == GL_READ_FRAMEBUFFER)
  {
    if (s->read_fb == framebuffer)
      return;
    s->read_fb = framebuffer;
  }
#endif
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBindFramebuffer);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, framebuffer);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glFramebufferTexture2D(GLenum target,
                                                   GLenum attachment,
                                                   GLenum textarget,
                                                   GLuint texture, GLint level)
{
#ifdef DEBUG_VERBOSE
  log_console("[glFramebufferTexture2D] BEGIN");
  log_console("    target=0x%04x", target);
  log_console("    attachment=0x%04x", attachment);
  log_console("    textarget=0x%04x", textarget);
  log_console("    texture=%u", texture);
  log_console("    level=%d", level);
  log_console("    g_stub_current_ctx=%d", g_stub_current_ctx);

  if (textarget == GL_TEXTURE_EXTERNAL_OES)
  {
    log_console("[glFramebufferTexture2D] ERROR: GL_TEXTURE_EXTERNAL_OES not "
                "supported in framebuffer attachments");
    // textarget = GL_TEXTURE_2D; // avoid sending unsupported target to proxy
  }
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFramebufferTexture2D);

#ifdef DEBUG_VERBOSE
  log_console("[glFramebufferTexture2D] opcode=%u", C->opcode);
#endif

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, attachment);
  aw_u32(&W, textarget);
  aw_u32(&W, texture);
  aw_i32(&W, level);
  C->args_len = W.pos;

#ifdef DEBUG_VERBOSE
  log_console("[glFramebufferTexture2D] args_len=%d", C->args_len);
  log_console("[glFramebufferTexture2D] sending call");
#endif

  BRIDGE_SEND_VOID();

#ifdef DEBUG_VERBOSE
  log_console("[glFramebufferTexture2D] END");
#endif
}

GL_APICALL void GL_APIENTRY glFramebufferRenderbuffer(GLenum target,
                                                      GLenum attachment,
                                                      GLenum rbtarget,
                                                      GLuint renderbuffer)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFramebufferRenderbuffer);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, attachment);
  aw_u32(&W, rbtarget);
  aw_u32(&W, renderbuffer);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL GLenum GL_APIENTRY glCheckFramebufferStatus(GLenum target)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCheckFramebufferStatus);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  C->args_len = W.pos;
  return (GLenum)BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGetFramebufferAttachmentParameteriv(
    GLenum target, GLenum attachment, GLenum pname, GLint *params)
{
#ifdef DEBUG_VERBOSE
  log_console(
      "glGetFramebufferAttachmentParameteriv: tgt=%u att=%u pname=%u params=%p",
      target, attachment, pname, (void *)params);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetFramebufferAttachmentParameteriv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, attachment);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (params)
    *params = (GLint)(int32_t)C->result;
}

GL_APICALL void GL_APIENTRY glGenRenderbuffers(GLsizei n, GLuint *rbs)
{
  if (!rbs || n <= 0)
  {
    log_console("[glGenRenderbuffers] INVALID ARGS: n=%d rbs=%p", n, rbs);
    return;
  }

#ifdef DEBUG
  log_console("[glGenRenderbuffers] BEGIN n=%d rbs=%p", n, rbs);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  C->opcode = OP_glGenRenderbuffers;

#ifdef DEBUG
  log_console("[glGenRenderbuffers] opcode=%u", C->opcode);
#endif

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);
  C->args_len = W.pos;

  uint32_t out_off = bridge_data_write(rbs, n * sizeof(GLuint));

  C->data_offset = out_off;
  C->data_size = n * sizeof(GLuint);

#ifdef DEBUG
  log_console("[glGenRenderbuffers] "
              "sending call: n=%d args_len=%d data_off=%u size=%u",
              n, C->args_len, C->data_offset, C->data_size);
#endif

  BRIDGE_SEND_CALL();

  bridge_data_read(rbs, out_off, n * sizeof(GLuint));

#ifdef DEBUG
  for (int i = 0; i < n; i++)
    log_console("[glGenRenderbuffers] rbs[%d]=%u", i, rbs[i]);

  log_console("[glGenRenderbuffers] END");
#endif
}

GL_APICALL void GL_APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint *rbs)
{
  if (!rbs || n <= 0)
    return;

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glDeleteRenderbuffers;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, n);

  /* Pack n GLuints */
  for (int i = 0; i < n; i++)
    aw_u32(&W, rbs[i]);

  C->args_len = W.pos;

  BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glBindRenderbuffer(GLenum target,
                                               GLuint renderbuffer)
{
#ifdef DEBUG
  log_console(" glBindRenderbuffer target=%x rb=%u", target, renderbuffer);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glBindRenderbuffer);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, renderbuffer);

  C->args_len = W.pos;

  BRIDGE_SEND_VOID();

#ifdef DEBUG
  log_console(" glBindRenderbuffer END");
#endif
}

GL_APICALL void GL_APIENTRY glRenderbufferStorage(GLenum target,
                                                  GLenum internalformat,
                                                  GLsizei width, GLsizei height)
{
#ifdef DEBUG_VERBOSE
  log_console("[glRenderbufferStorage] target=0x%04x fmt=0x%04x %dx%d", target,
              internalformat, width, height);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glRenderbufferStorage);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, internalformat);
  aw_i32(&W, width);
  aw_i32(&W, height);
  C->args_len = W.pos;

#ifdef DEBUG_VERBOSE
  log_console("glRenderbufferStorage: target: %d internal format: %d width: %d "
              "height: %d",
              target, internalformat, width, height);
#endif

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetRenderbufferParameteriv(GLenum target,
                                                         GLenum pname,
                                                         GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetRenderbufferParameteriv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (params)
    *params = (GLint)(int32_t)C->result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Shaders
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL GLuint GL_APIENTRY glCreateShader(GLenum type)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCreateShader);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, type);
  C->args_len = W.pos;

#ifdef DEBUG_SHADERS
  GLuint shader = BRIDGE_SEND_CALL();
  log_console("glCreateShader received %d", shader);
  return shader;
#else
  // returns non-zero by value by which it can be referenced
  return (GLuint)BRIDGE_SEND_CALL();
#endif
}

GL_APICALL void GL_APIENTRY glDeleteShader(GLuint shader)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDeleteShader);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, shader);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glShaderSource(GLuint shader, GLsizei count,
                                           const GLchar *const *string,
                                           const GLint *length)
{
#ifdef DEBUG_DUMP_SHADERS
  log_console("glShaderSource shader:%d count:%d string:%p length:%d", shader,
              count, string, length);
#endif

  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glShaderSource;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, shader);
  aw_i32(&W, count);

  uint32_t meta_pos = W.pos;

  // Reserve space for [offset,len] pairs
  for (int i = 0; i < count; i++)
  {
    aw_u32(&W, 0);
    aw_u32(&W, 0);
  }

  // Fill metadata
  for (int i = 0; i < count; i++)
  {
    uint32_t off = 0;
    uint32_t len = 0;

    if (string && string[i])
    {
      len = (length && length[i] >= 0) ? (uint32_t)length[i]
                                       : (uint32_t)strlen(string[i]);

#ifdef DEBUG_DUMP_SHADERS
      log_console("glShaderSource: chunk[%d]: len=%u", i, len);
      char tmp[513];
      uint32_t dump = len > 512 ? 512 : len;
      memcpy(tmp, string[i], dump);
      tmp[dump] = 0;

      log_console("glShaderSource: RAW CHUNK[%d]:\n%s", i, tmp);
#endif
      off = bridge_data_write(string[i], len);
    }

    // Write metadata back into reserved slots
    *(uint32_t *)(C->args + meta_pos + i * 8 + 0) = off;
    *(uint32_t *)(C->args + meta_pos + i * 8 + 4) = len;
  }

  C->args_len = W.pos;
  C->data_offset = 0;
  C->data_size = 0;

  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glCompileShader(GLuint shader)
{
#ifdef DEBUG_SHADERS
  log_console("glCompileShader shader=%u", shader);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCompileShader);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, shader);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetShaderiv(GLuint shader, GLenum pname,
                                          GLint *params)
{
#ifdef DEBUG_SHADERS
  log_console("glGetShaderiv recv: (shader = %d, pname = %d)", shader, pname);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetShaderiv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, shader);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  /* No shared memory buffer needed */
  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (params)
    *params = (GLint)(int32_t)C->result;

#ifdef DEBUG_SHADERS
  log_console("glGetShaderiv from stub: (*params = %d)", *params);
#endif
}

GL_APICALL void GL_APIENTRY glGetShaderInfoLog(GLuint shader, GLsizei bufSize,
                                               GLsizei *length, GLchar *infoLog)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetShaderInfoLog;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, shader);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  if (bufSize > 0 && infoLog)
  {
    uint32_t out = bridge_data_write(infoLog, bufSize); // allocate buffer
    C->data_offset = out;
    C->data_size = bufSize;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (bufSize > 0 && infoLog && C->data_offset)
    bridge_data_read(infoLog, C->data_offset, bufSize);

  if (length)
    *length = (GLsizei)C->result;
}

GL_APICALL void GL_APIENTRY glGetShaderSource(GLuint shader, GLsizei bufSize,
                                              GLsizei *length, GLchar *source)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetShaderSource;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, shader);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;

  if (bufSize > 0 && source)
  {
    uint32_t out = bridge_data_write(source, bufSize);
    C->data_offset = out;
    C->data_size = bufSize;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (bufSize > 0 && source && C->data_offset)
    bridge_data_read(source, C->data_offset, bufSize);

  if (length)
    *length = (GLsizei)C->result;
}

GL_APICALL void GL_APIENTRY glShaderBinary(GLsizei count, const GLuint *shaders,
                                           GLenum binaryformat,
                                           const void *binary, GLsizei length)
{
  size_t shader_bytes = (size_t)count * sizeof(GLuint);
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glShaderBinary;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, count);
  aw_u32(&W, binaryformat);
  aw_i32(&W, length);
  C->args_len = W.pos;
  C->data_offset = bridge_data_write(shaders, shader_bytes);
  C->data_size = (uint32_t)shader_bytes;
  C->data2_offset = bridge_data_write(binary, (size_t)length);
  C->data2_size = (uint32_t)length;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glReleaseShaderCompiler(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glReleaseShaderCompiler);
  C->args_len = 0;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetShaderPrecisionFormat(GLenum shadertype,
                                                       GLenum precisiontype,
                                                       GLint *range,
                                                       GLint *precision)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetShaderPrecisionFormat;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, shadertype);
  aw_u32(&W, precisiontype);
  C->args_len = W.pos;

  GLint tmp[3] = {0, 0, 0};
  uint32_t out = bridge_data_write(tmp, sizeof(tmp));
  C->data_offset = out;
  C->data_size = sizeof(tmp);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();
  bridge_data_read(tmp, out, sizeof(tmp));

  if (range)
  {
    range[0] = tmp[0];
    range[1] = tmp[1];
  }
  if (precision)
  {
    *precision = tmp[2];
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Programs
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL GLuint GL_APIENTRY glCreateProgram(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCreateProgram);
  C->args_len = 0;

#ifdef DEBUG_SHADERS
  GLuint result = (GLuint)BRIDGE_SEND_CALL();

  log_console("glCreateProgram -> %u (0x%x)", result, result);

  return result;
#else
  // returns a non-zero value by which it can be referenced
  return (GLuint)BRIDGE_SEND_CALL();
#endif
}

GL_APICALL void GL_APIENTRY glDeleteProgram(GLuint program)
{
#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
  loc_cache_invalidate_program(program);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDeleteProgram);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glAttachShader(GLuint program, GLuint shader)
{
#ifdef DEBUG_SHADERS
  log_console("glAttachShader(prog=%u shader=%u)", program, shader);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glAttachShader);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, shader);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDetachShader(GLuint program, GLuint shader)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDetachShader);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, shader);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glLinkProgram(GLuint program)
{
#ifdef DEBUG_SHADERS
  log_console("glLinkProgram(program = %u)", program);
#endif

#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
  loc_cache_invalidate_program(program);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glLinkProgram);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glUseProgram(GLuint program)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->program == program)
    return;
  s->program = program;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glUseProgram);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glValidateProgram(GLuint program)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glValidateProgram);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetProgramiv(GLuint program, GLenum pname,
                                           GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetProgramiv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  /* No shared memory needed */
  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;

#ifdef DEBUG_SHADERS
  log_console("glGetProgramiv (program=%d pname=%d)", program, pname);
#endif

  BRIDGE_SEND_CALL();

  if (params)
    *params = (GLint)(int32_t)C->result;

#ifdef DEBUG_SHADERS
  log_console("glGetProgramiv RECEIVED: (program=%d pname=%d result=%d)",
              program, pname, *params);
#endif
}

GL_APICALL void GL_APIENTRY glGetProgramInfoLog(GLuint program, GLsizei bufSize,
                                                GLsizei *length,
                                                GLchar *infoLog)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetProgramInfoLog;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;
  uint32_t out = bridge_data_write(infoLog, (size_t)bufSize);
  C->data_offset = out;
  C->data_size = (uint32_t)bufSize;
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_CALL();
  bridge_data_read(infoLog, out, (size_t)bufSize);
  if (length)
    *length = (GLsizei)C->result;
}

GL_APICALL void GL_APIENTRY glGetAttachedShaders(GLuint program,
                                                 GLsizei maxCount,
                                                 GLsizei *count,
                                                 GLuint *shaders)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetAttachedShaders;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, maxCount);
  C->args_len = W.pos;

  if (maxCount > 0 && shaders)
  {
    uint32_t out = bridge_data_write(shaders, maxCount * sizeof(GLuint));
    C->data_offset = out;
    C->data_size = maxCount * sizeof(GLuint);
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();

  if (maxCount > 0 && shaders && C->data_offset)
    bridge_data_read(shaders, C->data_offset, maxCount * sizeof(GLuint));

  if (count)
    *count = (GLsizei)C->result;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Uniforms
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL GLint GL_APIENTRY glGetUniformLocation(GLuint program,
                                                  const GLchar *name)
{
#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
  GLint cached;
  if (loc_cache_lookup(program, name, 0, &cached))
  {
#ifdef DEBUG_VERBOSE
    log_console("glGetUniformLocation - program:%d name:%s (cached)", program,
                name);
#endif
    return cached;
  }
#endif

#ifdef DEBUG_VERBOSE
  log_console("glGetUniformLocation - program:%d name:%s", program, name);
#endif

  size_t namelen = strlen(name) + 1;
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetUniformLocation;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  C->args_len = W.pos;
  C->data_offset = bridge_data_write(name, namelen);
  C->data_size = (uint32_t)namelen;
  C->data2_offset = 0;
  C->data2_size = 0;

  GLint loc = (GLint)BRIDGE_SEND_CALL();
#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
  loc_cache_insert(program, name, 0, loc);
#endif
  return loc;
}

GL_APICALL void GL_APIENTRY glGetActiveUniform(GLuint program, GLuint index,
                                               GLsizei bufSize, GLsizei *length,
                                               GLint *size, GLenum *type,
                                               GLchar *name)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetActiveUniform;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, index);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;
  size_t name_buf_sz = (size_t)(bufSize > 0 ? bufSize : 1);
  uint32_t out = bridge_data_write(name ? name : "", name_buf_sz);
  C->data_offset = out;
  C->data_size = (uint32_t)name_buf_sz;
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_CALL();
  /* Proxy packs: result = (type<<32 | size), name in data, length in result_buf
   */
  if (size)
    *size = (GLint)(C->result & 0xFFFFFFFF);
  if (type)
    *type = (GLenum)(C->result >> 32);
  if (length)
    *length = *(GLsizei *)C->result_buf;
  if (name)
    bridge_data_read(name, out, name_buf_sz);
}

/* Uniform scalars — pattern: OP + location + values */
#define UNIFORM_1(suffix, GL_T, aw_fn)                                         \
  GL_APICALL void GL_APIENTRY glUniform1##suffix(GLint loc, GL_T v0)           \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    setup_scalar(OP_glUniform1##suffix);                                       \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_i32(&W, loc);                                                           \
    aw_fn(&W, v0);                                                             \
    C->args_len = W.pos;                                                       \
    BRIDGE_SEND_VOID();                                                        \
  }

#define UNIFORM_2(suffix, GL_T, aw_fn)                                         \
  GL_APICALL void GL_APIENTRY glUniform2##suffix(GLint loc, GL_T v0, GL_T v1)  \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    setup_scalar(OP_glUniform2##suffix);                                       \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_i32(&W, loc);                                                           \
    aw_fn(&W, v0);                                                             \
    aw_fn(&W, v1);                                                             \
    C->args_len = W.pos;                                                       \
    BRIDGE_SEND_VOID();                                                        \
  }

#define UNIFORM_3(suffix, GL_T, aw_fn)                                         \
  GL_APICALL void GL_APIENTRY glUniform3##suffix(GLint loc, GL_T v0, GL_T v1,  \
                                                 GL_T v2)                      \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    setup_scalar(OP_glUniform3##suffix);                                       \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_i32(&W, loc);                                                           \
    aw_fn(&W, v0);                                                             \
    aw_fn(&W, v1);                                                             \
    aw_fn(&W, v2);                                                             \
    C->args_len = W.pos;                                                       \
    BRIDGE_SEND_VOID();                                                        \
  }

#define UNIFORM_4(suffix, GL_T, aw_fn)                                         \
  GL_APICALL void GL_APIENTRY glUniform4##suffix(GLint loc, GL_T v0, GL_T v1,  \
                                                 GL_T v2, GL_T v3)             \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    setup_scalar(OP_glUniform4##suffix);                                       \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_i32(&W, loc);                                                           \
    aw_fn(&W, v0);                                                             \
    aw_fn(&W, v1);                                                             \
    aw_fn(&W, v2);                                                             \
    aw_fn(&W, v3);                                                             \
    C->args_len = W.pos;                                                       \
    BRIDGE_SEND_VOID();                                                        \
  }

#define UNIFORM_NV(suffix, GL_T, comp)                                         \
  GL_APICALL void GL_APIENTRY glUniform##comp##suffix##v(                      \
      GLint loc, GLsizei count, const GL_T *v)                                 \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    C->opcode = OP_glUniform##comp##suffix##v;                                 \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_i32(&W, loc);                                                           \
    aw_i32(&W, count);                                                         \
    C->args_len = W.pos;                                                       \
    C->data_offset =                                                           \
        bridge_data_write(v, (size_t)count * comp * sizeof(GL_T));             \
    C->data_size = (uint32_t)count * comp * sizeof(GL_T);                      \
    C->data2_offset = 0;                                                       \
    C->data2_size = 0;                                                         \
    BRIDGE_SEND_VOID();                                                        \
  }

UNIFORM_1(f, GLfloat, aw_f32)
UNIFORM_1(i, GLint, aw_i32)
UNIFORM_2(f, GLfloat, aw_f32)
UNIFORM_2(i, GLint, aw_i32)
UNIFORM_3(f, GLfloat, aw_f32)
UNIFORM_3(i, GLint, aw_i32)
UNIFORM_4(f, GLfloat, aw_f32)
UNIFORM_4(i, GLint, aw_i32)

UNIFORM_NV(f, GLfloat, 1)
UNIFORM_NV(i, GLint, 1)
UNIFORM_NV(f, GLfloat, 2)
UNIFORM_NV(i, GLint, 2)
UNIFORM_NV(f, GLfloat, 3)
UNIFORM_NV(i, GLint, 3)
UNIFORM_NV(f, GLfloat, 4)
UNIFORM_NV(i, GLint, 4)

#define UNIFORM_MAT(dim)                                                       \
  GL_APICALL void GL_APIENTRY glUniformMatrix##dim##fv(                        \
      GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value)     \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    C->opcode = OP_glUniformMatrix##dim##fv;                                   \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_i32(&W, loc);                                                           \
    aw_i32(&W, count);                                                         \
    aw_u32(&W, transpose);                                                     \
    C->args_len = W.pos;                                                       \
    C->data_offset = bridge_data_write(value, (size_t)count * (dim * dim) *    \
                                                  sizeof(GLfloat));            \
    C->data_size = (uint32_t)count * (dim * dim) * sizeof(GLfloat);            \
    C->data2_offset = 0;                                                       \
    C->data2_size = 0;                                                         \
    BRIDGE_SEND_VOID();                                                        \
  }

UNIFORM_MAT(2)
UNIFORM_MAT(3)
UNIFORM_MAT(4)

GL_APICALL void GL_APIENTRY glGetUniformfv(GLuint program, GLint location,
                                           GLfloat *params)
{
  /* Conservative upper bound: up to 16 float components per location
     (covers vec4/mat4 and first element of arrays). */
  const size_t max_components = 16;
  const size_t bytes = max_components * sizeof(GLfloat);

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetUniformfv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, location);
  C->args_len = W.pos;

  uint32_t out = bridge_data_write(params, bytes);
  C->data_offset = out;
  C->data_size = (uint32_t)bytes;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();
  bridge_data_read(params, out, bytes);
}

GL_APICALL void GL_APIENTRY glGetUniformiv(GLuint program, GLint location,
                                           GLint *params)
{
  /* Conservative upper bound: up to 4 int components per location
     (covers ivec4 and first element of arrays). */
  const size_t max_components = 4;
  const size_t bytes = max_components * sizeof(GLint);

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetUniformiv;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_i32(&W, location);
  C->args_len = W.pos;

  uint32_t out = bridge_data_write(params, bytes);
  C->data_offset = out;
  C->data_size = (uint32_t)bytes;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();
  bridge_data_read(params, out, bytes);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Vertex attributes
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL GLint GL_APIENTRY glGetAttribLocation(GLuint program,
                                                 const GLchar *name)
{
#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
  GLint cached;
  if (loc_cache_lookup(program, name, 1, &cached))
  {
#ifdef DEBUG_VERBOSE
    log_console("glGetAttribLocation - program:%d name:%s (cached)", program,
                name);
#endif
    return cached;
  }
#endif

#ifdef DEBUG_VERBOSE
  log_console("glGetAttribLocation - program:%d name:%s", program, name);
#endif

  size_t namelen = strlen(name) + 1;
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetAttribLocation;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  C->args_len = W.pos;
  C->data_offset = bridge_data_write(name, namelen);
  C->data_size = (uint32_t)namelen;
  C->data2_offset = 0;
  C->data2_size = 0;

  GLint loc = (GLint)BRIDGE_SEND_CALL();
#ifdef CACHE_UNIFORM_ATTRIB_LOCATION
  loc_cache_insert(program, name, 1, loc);
#endif
  return loc;
}

GL_APICALL void GL_APIENTRY glGetActiveAttrib(GLuint program, GLuint index,
                                              GLsizei bufSize, GLsizei *length,
                                              GLint *size, GLenum *type,
                                              GLchar *name)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetActiveAttrib;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, index);
  aw_i32(&W, bufSize);
  C->args_len = W.pos;
  uint32_t out = bridge_data_write(name, (size_t)bufSize);
  C->data_offset = out;
  C->data_size = (uint32_t)bufSize;
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_CALL();
  if (size)
    *size = (GLint)(C->result & 0xFFFFFFFF);
  if (type)
    *type = (GLenum)(C->result >> 32);
  if (length)
    *length = *(GLsizei *)C->result_buf;
  bridge_data_read(name, out, (size_t)bufSize);
}

GL_APICALL void GL_APIENTRY glBindAttribLocation(GLuint program, GLuint index,
                                                 const GLchar *name)
{
  size_t namelen = strlen(name) + 1;
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glBindAttribLocation;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, program);
  aw_u32(&W, index);
  C->args_len = W.pos;
  C->data_offset = bridge_data_write(name, namelen);
  C->data_size = (uint32_t)namelen;
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_VOID();
}

static size_t gl_type_size(GLenum type)
{
  switch (type)
  {
  case GL_FLOAT:
    return 4;

  case GL_HALF_FLOAT: /* GLES 3.x */
    return 2;

  case GL_UNSIGNED_BYTE:
  case GL_BYTE:
    return 1;

  case GL_SHORT:
  case GL_UNSIGNED_SHORT:
    return 2;

  case GL_INT:          /* GLES 3.x */
  case GL_UNSIGNED_INT: /* GLES 3.x */
  case GL_FIXED:
    return 4;

  case GL_UNSIGNED_INT_2_10_10_10_REV: /* GLES 3.x packed formats */
  case GL_INT_2_10_10_10_REV:
    return 4;

  default:
    /* Fallback */
    return 4;
  }
}

GL_APICALL void GL_APIENTRY glVertexAttribPointer(GLuint index, GLint size,
                                                  GLenum type,
                                                  GLboolean normalized,
                                                  GLsizei stride,
                                                  const void *pointer)
{
  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_stub_state[index].size = size;
    g_attrib_stub_state[index].type = type;
    g_attrib_stub_state[index].normalized = normalized;
    g_attrib_stub_state[index].stride = stride;
    g_attrib_stub_state[index].pointer = (uintptr_t)pointer;
    g_attrib_stub_state[index].integer = GL_FALSE;
    g_attrib_stub_state[index].vbo = stub_gl_state.array_buffer;

    GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index] = g_attrib_stub_state[index];
  }
  else
    log_error("glVertexAttribPointer: index:%d > MAX_VERTEX_ATTRIBS!", index);

#ifdef DEBUG_VERBOSE
  log_console("[glVertexAttribPointer]");
  log_console(" index=%u", index);
  log_console(" size=%d", size);
  log_console(" type=0x%x", type);
  log_console(" stride=%d", stride);
  log_console(" ptr=%p", pointer);
  log_console(" vbo=%d", stub_gl_state.array_buffer);
  log_console(" g_stub_current_ctx=%d", g_stub_current_ctx);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttribPointer);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, index);
  aw_i32(&W, size);
  aw_u32(&W, type);
  aw_u32(&W, normalized);
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
}

GL_APICALL void GL_APIENTRY glEnableVertexAttribArray(GLuint index)
{
#ifdef DEBUG_VERBOSE
  log_console("glEnableVertexAttribArray: index:%d", index);
#endif
  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_stub_state[index].enabled = GL_TRUE;
    GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index].enabled = GL_TRUE;
  }

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glEnableVertexAttribArray);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDisableVertexAttribArray(GLuint index)
{
  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_stub_state[index].enabled = GL_FALSE;
    GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index].enabled = GL_FALSE;
  }
  else
    log_error("glDisableVertexAttribArray: index:%d >= MAX_VERTEX_ATTRIBS",
              index);

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDisableVertexAttribArray);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glGetVertexAttribfv(GLuint index, GLenum pname,
                                                GLfloat *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetVertexAttribfv;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_u32(&W, pname);
  C->args_len = W.pos;
  uint32_t out = bridge_data_write(params, 4 * sizeof(GLfloat));
  C->data_offset = out;
  C->data_size = 4 * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;
  BRIDGE_SEND_CALL();
  bridge_data_read(params, out, 4 * sizeof(GLfloat));
}

GL_APICALL void GL_APIENTRY glGetVertexAttribiv(GLuint index, GLenum pname,
                                                GLint *params)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetVertexAttribiv;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  /* Determine element count */
  size_t count = (pname == GL_CURRENT_VERTEX_ATTRIB) ? 4 : 1;
  size_t bytes = count * sizeof(GLint);

  uint32_t out = bridge_data_write(params, bytes);
  C->data_offset = out;
  C->data_size = (uint32_t)bytes;
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();
  bridge_data_read(params, out, bytes);
}

GL_APICALL void GL_APIENTRY glGetVertexAttribPointerv(GLuint index,
                                                      GLenum pname,
                                                      void **pointer)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetVertexAttribPointerv);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, index);
  aw_u32(&W, pname);
  C->args_len = W.pos;
  *pointer = (void *)(uintptr_t)BRIDGE_SEND_CALL();
}

/* glVertexAttrib constant setters */
#define VA1(suffix, GL_T, aw_fn)                                               \
  GL_APICALL void GL_APIENTRY glVertexAttrib1##suffix(GLuint idx, GL_T x)      \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    setup_scalar(OP_glVertexAttrib1##suffix);                                  \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_u32(&W, idx);                                                           \
    aw_fn(&W, x);                                                              \
    C->args_len = W.pos;                                                       \
    BRIDGE_SEND_VOID();                                                        \
  }

#define VA_FV(comp)                                                            \
  GL_APICALL void GL_APIENTRY glVertexAttrib##comp##fv(GLuint idx,             \
                                                       const GLfloat *v)       \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    C->opcode = OP_glVertexAttrib##comp##fv;                                   \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_u32(&W, idx);                                                           \
    C->args_len = W.pos;                                                       \
    C->data_offset = bridge_data_write(v, comp * sizeof(GLfloat));             \
    C->data_size = comp * sizeof(GLfloat);                                     \
    C->data2_offset = 0;                                                       \
    C->data2_size = 0;                                                         \
    BRIDGE_SEND_VOID();                                                        \
  }

VA1(f, GLfloat, aw_f32)
VA_FV(1)
GL_APICALL void GL_APIENTRY glVertexAttrib2f(GLuint i, GLfloat x, GLfloat y)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttrib2f);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, i);
  aw_f32(&W, x);
  aw_f32(&W, y);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}
VA_FV(2)
GL_APICALL void GL_APIENTRY glVertexAttrib3f(GLuint i, GLfloat x, GLfloat y,
                                             GLfloat z)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttrib3f);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, i);
  aw_f32(&W, x);
  aw_f32(&W, y);
  aw_f32(&W, z);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}
VA_FV(3)
GL_APICALL void GL_APIENTRY glVertexAttrib4f(GLuint i, GLfloat x, GLfloat y,
                                             GLfloat z, GLfloat w)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glVertexAttrib4f);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, i);
  aw_f32(&W, x);
  aw_f32(&W, y);
  aw_f32(&W, z);
  aw_f32(&W, w);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}
VA_FV(4)

/* ═══════════════════════════════════════════════════════════════════════════
 * Draw calls
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL void GL_APIENTRY glDrawArrays(GLenum mode, GLint first,
                                         GLsizei count)
{
  BRIDGE_BEGIN();

  BridgeCtrl *C = BRIDGE_CTRL();

  setup_scalar(OP_glDrawArrays);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u32(&W, mode);
  aw_i32(&W, first);
  aw_i32(&W, count);

  /*
   * count copied attributes
   */

  uint32_t attrib_count = 0;

  uint32_t count_pos = W.pos;
  aw_u32(&W, 0);

#ifdef DEBUG_VERBOSE
  log_console("glDrawArrays: vao:%d mode:%d first:%d count:%d",
              g_stub_ctx[g_stub_current_ctx].current_vao, mode, first, count);
#endif

  for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
  {
    AttribState *a = &g_attrib_stub_state[i];

    if (!a->enabled)
      continue;

    if (a->vbo) // VBO-backed - DO NOT COPY
      continue;

    if (!a->pointer) // client array but null pointer - skip
      continue;

    size_t elem = gl_type_size(a->type);

    size_t stride = a->stride ? (size_t)a->stride : (size_t)(a->size * elem);

    size_t bytes =
        ((size_t)first + (size_t)count - 1) * stride + (size_t)(a->size * elem);

    uint32_t off = bridge_data_write((void *)a->pointer, bytes);

#ifdef DEBUG_VERBOSE
    log_console("[DrawArrays] copy attrib=%d bytes=%u off=%u", i,
                (unsigned)bytes, off);
#endif

    aw_u32(&W, i);
    aw_u32(&W, off);

    attrib_count++;
  }

  memcpy(C->args + count_pos, &attrib_count, sizeof(uint32_t));

  /* Piggyback the dirty range of any persistently-mapped, unsynchronized
   * VBO backing an enabled attrib */
  GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
  VAOState *vao = &ctx->vaos[ctx->current_vao];

  uint32_t piggyback_buffer = 0;
  uint32_t piggyback_offset = 0;
  uint32_t piggyback_length = 0;
  uint32_t piggyback_data_off = stub_vbo_piggyback_range(
      vao, (GLintptr)first, (GLintptr)first + (GLintptr)count,
      &piggyback_buffer, &piggyback_offset, &piggyback_length);

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

  BRIDGE_SEND_VOID();
}

/*
 * glDrawElements — when an element array VBO is bound (the normal
 * case), indices is a byte offset; we pass it as uint64.
 * For client-side index arrays we copy the index data to the bridge and pass a
 * pointer
 */
GL_APICALL void GL_APIENTRY glDrawElements(GLenum mode, GLsizei count,
                                           GLenum type, const void *indices)
{
#ifdef DEBUG_VERBOSE
  log_console("glDrawElements: mode:%d count:%d type:%d indices:%p "
              "g_stub_current_ctx:%d",
              mode, count, type, indices, g_stub_current_ctx);
#endif
  GLContextState *ctx = &g_stub_ctx[g_stub_current_ctx];
  VAOState *vao = &ctx->vaos[ctx->current_vao];

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glDrawElements;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mode);
  aw_i32(&W, count);
  aw_u32(&W, type);

  uint32_t is_client_ptr = 0;
  uintptr_t idx = 0;
  const uint8_t *index_bytes = NULL;

  uint32_t ebo_piggyback_buffer = 0;
  uint32_t ebo_piggyback_offset = 0;
  uint32_t ebo_piggyback_length = 0;

  if (vao->ebo != 0)
  {
#ifdef DEBUG_VERBOSE
    log_console("glDrawElements: ebo mode");
#endif
    is_client_ptr = 0;
    idx = (uintptr_t)indices;
    C->data_offset = 0;
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
    log_console("glDrawElements: alt mode");
#endif
    is_client_ptr = 1;
    size_t index_size = (type == GL_UNSIGNED_SHORT) ? 2
                        : (type == GL_UNSIGNED_INT) ? 4
                                                    : 1;
    size_t bytes = count * index_size;
    C->data_offset = bridge_data_write(indices, bytes);
    C->data_size = bytes;
    index_bytes = (const uint8_t *)indices;
    idx = 0;
  }

  aw_u64(&W, idx);
  aw_u32(&W, is_client_ptr);
  aw_u32(&W, ebo_piggyback_buffer);
  aw_u32(&W, ebo_piggyback_offset);
  aw_u32(&W, ebo_piggyback_length);

  GLintptr min_vertex = 0, max_vertex = 0;
  bool have_index_bounds = mapped_index_bounds(index_bytes, count, type, 0,
                                               &min_vertex, &max_vertex);

  GLintptr first_vertex = have_index_bounds ? min_vertex : 0;
  GLintptr last_vertex = have_index_bounds ? max_vertex : (GLintptr)count;

  uint32_t piggyback_buffer = 0;
  uint32_t piggyback_offset = 0;
  uint32_t piggyback_length = 0;
  uint32_t piggyback_data_off = stub_vbo_piggyback_range(
      vao, first_vertex, last_vertex, &piggyback_buffer, &piggyback_offset,
      &piggyback_length);

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

  BRIDGE_SEND_VOID();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Rasterisation state
 * ═══════════════════════════════════════════════════════════════════════════
 */
GL_APICALL void GL_APIENTRY glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
#ifdef DEBUG_VERBOSE
  log_console("glViewport - x: %d y: %d w: %d h: %d", x, y, w, h);
#endif

#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->viewport_valid && s->viewport[0] == x && s->viewport[1] == y &&
      s->viewport[2] == w && s->viewport[3] == h)
    return;
  s->viewport[0] = x;
  s->viewport[1] = y;
  s->viewport[2] = w;
  s->viewport[3] = h;
  s->viewport_valid = 1;
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glViewport);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, w);
  aw_i32(&W, h);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glScissor(GLint x, GLint y, GLsizei width,
                                      GLsizei height)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->scissor_valid && s->scissor[0] == x && s->scissor[1] == y &&
      s->scissor[2] == width && s->scissor[3] == height)
    return;
  s->scissor[0] = x;
  s->scissor[1] = y;
  s->scissor[2] = width;
  s->scissor[3] = height;
  s->scissor_valid = 1;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glScissor);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, width);
  aw_i32(&W, height);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glEnable(GLenum cap)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  GLboolean *slot = shadow_cap_slot(s, cap);
  if (slot)
  {
    if (*slot == GL_TRUE)
      return;
    *slot = GL_TRUE;
  }
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glEnable);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, cap);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDisable(GLenum cap)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  GLboolean *slot = shadow_cap_slot(s, cap);
  if (slot)
  {
    if (*slot == GL_FALSE)
      return;
    *slot = GL_FALSE;
  }
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDisable);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, cap);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL GLboolean GL_APIENTRY glIsEnabled(GLenum cap)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glIsEnabled);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, cap);
  C->args_len = W.pos;
  return (GLboolean)BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glCullFace(GLenum mode)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glCullFace);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mode);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glFrontFace(GLenum mode)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->front_face == mode)
    return;
  s->front_face = mode;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFrontFace);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mode);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glLineWidth(GLfloat width)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glLineWidth);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, width);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glPolygonOffset(GLfloat factor, GLfloat units)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glPolygonOffset);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, factor);
  aw_f32(&W, units);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glSampleCoverage(GLfloat value, GLboolean invert)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glSampleCoverage);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, value);
  aw_u32(&W, invert);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glHint(GLenum target, GLenum mode)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glHint);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, target);
  aw_u32(&W, mode);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Blend / Depth / Stencil / Clear
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL void GL_APIENTRY glBlendColor(GLfloat r, GLfloat g, GLfloat b,
                                         GLfloat a)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBlendColor);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, r);
  aw_f32(&W, g);
  aw_f32(&W, b);
  aw_f32(&W, a);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBlendEquation(GLenum mode)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->blend_eq_rgb == mode && s->blend_eq_alpha == mode)
    return;
  s->blend_eq_rgb = s->blend_eq_alpha = mode;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBlendEquation);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mode);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBlendEquationSeparate(GLenum modeRGB,
                                                    GLenum modeAlpha)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->blend_eq_rgb == modeRGB && s->blend_eq_alpha == modeAlpha)
    return;
  s->blend_eq_rgb = modeRGB;
  s->blend_eq_alpha = modeAlpha;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBlendEquationSeparate);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, modeRGB);
  aw_u32(&W, modeAlpha);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBlendFunc(GLenum sfactor, GLenum dfactor)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->blend_src_rgb == sfactor && s->blend_dst_rgb == dfactor &&
      s->blend_src_alpha == sfactor && s->blend_dst_alpha == dfactor)
    return;
  s->blend_src_rgb = s->blend_src_alpha = sfactor;
  s->blend_dst_rgb = s->blend_dst_alpha = dfactor;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBlendFunc);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, sfactor);
  aw_u32(&W, dfactor);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB,
                                                GLenum srcAlpha,
                                                GLenum dstAlpha)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->blend_src_rgb == srcRGB && s->blend_dst_rgb == dstRGB &&
      s->blend_src_alpha == srcAlpha && s->blend_dst_alpha == dstAlpha)
    return;
  s->blend_src_rgb = srcRGB;
  s->blend_dst_rgb = dstRGB;
  s->blend_src_alpha = srcAlpha;
  s->blend_dst_alpha = dstAlpha;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glBlendFuncSeparate);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, srcRGB);
  aw_u32(&W, dstRGB);
  aw_u32(&W, srcAlpha);
  aw_u32(&W, dstAlpha);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDepthFunc(GLenum func)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->depth_func == func)
    return;
  s->depth_func = func;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDepthFunc);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, func);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDepthMask(GLboolean flag)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->depth_mask == flag)
    return;
  s->depth_mask = flag;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDepthMask);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, flag);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glDepthRangef(GLfloat n, GLfloat f)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->depth_range[0] == n && s->depth_range[1] == f)
    return;
  s->depth_range[0] = n;
  s->depth_range[1] = f;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glDepthRangef);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, n);
  aw_f32(&W, f);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glColorMask(GLboolean r, GLboolean g, GLboolean b,
                                        GLboolean a)
{
#ifdef CACHE_GL_STATE
  StubShadowState *s = shadow_state_for_current_ctx();
  if (s->color_mask[0] == r && s->color_mask[1] == g && s->color_mask[2] == b &&
      s->color_mask[3] == a)
    return;
  s->color_mask[0] = r;
  s->color_mask[1] = g;
  s->color_mask[2] = b;
  s->color_mask[3] = a;
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glColorMask);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, r);
  aw_u32(&W, g);
  aw_u32(&W, b);
  aw_u32(&W, a);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glStencilFunc);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, func);
  aw_i32(&W, ref);
  aw_u32(&W, mask);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glStencilFuncSeparate(GLenum face, GLenum func,
                                                  GLint ref, GLuint mask)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glStencilFuncSeparate);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, face);
  aw_u32(&W, func);
  aw_i32(&W, ref);
  aw_u32(&W, mask);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glStencilMask(GLuint mask)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glStencilMask);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mask);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glStencilMaskSeparate(GLenum face, GLuint mask)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glStencilMaskSeparate);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, face);
  aw_u32(&W, mask);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glStencilOp);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, fail);
  aw_u32(&W, zfail);
  aw_u32(&W, zpass);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glStencilOpSeparate(GLenum face, GLenum sfail,
                                                GLenum dpfail, GLenum dppass)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glStencilOpSeparate);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, face);
  aw_u32(&W, sfail);
  aw_u32(&W, dpfail);
  aw_u32(&W, dppass);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glClear(GLbitfield mask)
{
#ifdef DEBUG_VERBOSE
  log_console("glClear mask=0x%x", mask);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClear);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, mask);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glClearColor(GLfloat r, GLfloat g, GLfloat b,
                                         GLfloat a)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClearColor);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, r);
  aw_f32(&W, g);
  aw_f32(&W, b);
  aw_f32(&W, a);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glClearDepthf(GLfloat d)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClearDepthf);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_f32(&W, d);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

GL_APICALL void GL_APIENTRY glClearStencil(GLint s)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glClearStencil);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, s);
  C->args_len = W.pos;
  BRIDGE_SEND_VOID();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Query / State readback
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL GLenum GL_APIENTRY glGetError(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetError);
  C->args_len = 0;
  return (GLenum)BRIDGE_SEND_CALL();
}

GL_APICALL void GL_APIENTRY glGetBooleanv(GLenum pname, GLboolean *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetBooleanv;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  // GLES 3.2: allow up to 32 components
  const size_t max_elems = 32;
  uint32_t out = bridge_data_write(data, max_elems * sizeof(GLboolean));
  C->data_offset = out;
  C->data_size = max_elems * sizeof(GLboolean);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();
  bridge_data_read(data, out, max_elems * sizeof(GLboolean));
}

GL_APICALL void GL_APIENTRY glGetFloatv(GLenum pname, GLfloat *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetFloatv;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  // GLES 3.2: allow up to 32 components
  const size_t max_elems = 32;
  uint32_t out = bridge_data_write(data, max_elems * sizeof(GLfloat));
  C->data_offset = out;
  C->data_size = max_elems * sizeof(GLfloat);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();
  bridge_data_read(data, out, max_elems * sizeof(GLfloat));
}

GL_APICALL void GL_APIENTRY glGetIntegerv(GLenum pname, GLint *data)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glGetIntegerv;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, pname);
  C->args_len = W.pos;

  // GLES 3.2: allow up to 32 components
  const size_t max_elems = 32;
  uint32_t out = bridge_data_write(data, max_elems * sizeof(GLint));
  C->data_offset = out;
  C->data_size = max_elems * sizeof(GLint);
  C->data2_offset = 0;
  C->data2_size = 0;

  BRIDGE_SEND_CALL();
  bridge_data_read(data, out, max_elems * sizeof(GLint));
}

/*
 * glGetString — proxy stores the result in result_buf; we return a pointer to
 * a static copy so the caller gets a stable address.
 */

GL_APICALL const GLubyte *GL_APIENTRY glGetString(GLenum name)
{
  enum
  {
    IDX_VENDOR = 0,
    IDX_RENDERER,
    IDX_VERSION,
    IDX_EXTENSIONS,
    IDX_COUNT
  };

  static char cache[IDX_COUNT][BRIDGE_RESULT_SIZE];
  static int cached[IDX_COUNT];

  int idx = -1;

  if (name == GL_VENDOR)
    idx = IDX_VENDOR;
  else if (name == GL_RENDERER)
    idx = IDX_RENDERER;
  else if (name == GL_VERSION)
    idx = IDX_VERSION;
  else if (name == GL_EXTENSIONS)
    idx = IDX_EXTENSIONS;

  /* Cached path */
  if (idx >= 0 && cached[idx])
  {
#ifdef DEBUG_GL_GETSTRING
    log_console("glGetString(%u) -> \"%s\" (cached)", name, cache[idx]);
#endif
    return (const GLubyte *)cache[idx];
  }

#ifdef DEBUG_GL_GETSTRING
  log_console("glGetString(%u) -> index: %d", name, idx);
#endif

  /* Uncached path */
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glGetString);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, name);
  C->args_len = W.pos;

  BRIDGE_SEND_CALL();

  BridgeCtrl *C2 = BRIDGE_CTRL();
  const char *src = (const char *)C2->result_buf;

  if (idx >= 0)
  {
    strncpy(cache[idx], src, BRIDGE_RESULT_SIZE - 1);
    cache[idx][BRIDGE_RESULT_SIZE - 1] = '\0';
    cached[idx] = 1;

#ifdef DEBUG_GL_GETSTRING
    log_console("glGetString(%u) -> \"%s\"", name, cache[idx]);
#endif
    return (const GLubyte *)cache[idx];
  }

  /* Unknown enum: return directly */
#ifdef DEBUG_GL_GETSTRING
  log_console("glGetString(%u) -> \"%s\" (direct)", name, src);
#endif
  return (const GLubyte *)src;
}

GL_APICALL void GL_APIENTRY glReadPixels(GLint x, GLint y, GLsizei width,
                                         GLsizei height, GLenum format,
                                         GLenum type, void *pixels)
{
#ifdef DEBUG_VERBOSE
  log_console("glReadPixels x=%d y=%d w=%d h=%d fmt=0x%x type=0x%x pixels=%p "
              "g_pixel_pack_buffer=%u",
              x, y, width, height, format, type, pixels,
              stub_gl_state.pixel_pack_buffer);
#endif

  size_t pix_bytes = gl_image_size_2d(width, height, format, type);

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_glReadPixels;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, x);
  aw_i32(&W, y);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_u32(&W, format);
  aw_u32(&W, type);
  C->args_len = W.pos;

  if (stub_gl_state.pixel_pack_buffer != 0)
  {
    /* PBO is bound: 'pixels' is a byte offset into the PBO, not a real
     * pointer.  Pass the offset to the proxy via data_offset and signal
     * PBO mode with data_size == 0 so the proxy uses it directly as the
     * glReadPixels offset argument instead of the shared-memory buffer.
     * No bridge_data_write / bridge_data_read — the result lands in the
     * PBO on the proxy side and is mapped by the application separately. */
    C->data_offset = (uint32_t)(uintptr_t)pixels;
    C->data_size = 0;
    C->data2_offset = 0;
    C->data2_size = 0;
    BRIDGE_SEND_VOID();
    /* No bridge_data_read: output is in the proxy-side PBO, not shared mem */
  }
  else
  {
    /* Normal (non-PBO) path: use shared memory to transfer pixel data back */
    uint32_t out = bridge_data_write(pixels, pix_bytes);
    C->data_offset = out;
    C->data_size = (uint32_t)pix_bytes;
    C->data2_offset = 0;
    C->data2_size = 0;
    BRIDGE_SEND_CALL();
    if (pixels)
      bridge_data_read(pixels, out, pix_bytes);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Misc
 * ═══════════════════════════════════════════════════════════════════════════
 */

GL_APICALL void GL_APIENTRY glFinish(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFinish);
  C->args_len = 0;
  BRIDGE_SEND_CALL(); /* synchronous — wait for GPU drain */
}

GL_APICALL void GL_APIENTRY glFlush(void)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_glFlush);
  C->args_len = 0;
  BRIDGE_SEND_VOID();
}

#define IS_QUERY(fn, OP)                                                       \
  GL_APICALL GLboolean GL_APIENTRY fn(GLuint v)                                \
  {                                                                            \
    BRIDGE_BEGIN();                                                            \
    BridgeCtrl *C = BRIDGE_CTRL();                                             \
    setup_scalar(OP);                                                          \
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);                          \
    aw_u32(&W, v);                                                             \
    C->args_len = W.pos;                                                       \
    return (GLboolean)BRIDGE_SEND_CALL();                                      \
  }

IS_QUERY(glIsBuffer, OP_glIsBuffer)
IS_QUERY(glIsFramebuffer, OP_glIsFramebuffer)
IS_QUERY(glIsProgram, OP_glIsProgram)
IS_QUERY(glIsRenderbuffer, OP_glIsRenderbuffer)
IS_QUERY(glIsShader, OP_glIsShader)
IS_QUERY(glIsTexture, OP_glIsTexture)
