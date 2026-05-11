#pragma once

#include <stddef.h>

#include <GLES3/gl32.h>

#include "gles_util_shared.h"

#define MAX_MAPS 64

typedef struct
{
  uint32_t id;       // map ID
  void *ptr;         // mapped CPU pointer
  GLsizeiptr length; // size of mapped region
  GLenum target;     // GL_ARRAY_BUFFER, GL_UNIFORM_BUFFER, etc.
  GLintptr offset;   // offset inside the buffer
  GLuint buffer;
  GLbitfield access;
} StubMapEntry;

extern StubMapEntry stub_maps[MAX_MAPS];

typedef struct
{
  GLuint array_buffer; // GL_ARRAY_BUFFER

  GLuint pixel_pack_buffer;   // GL_PIXEL_PACK_BUFFER
  GLuint pixel_unpack_buffer; // GL_PIXEL_UNPACK_BUFFER

  GLuint uniform_buffer;            // GL_UNIFORM_BUFFER
  GLuint texture_buffer;            // GL_TEXTURE_BUFFER
  GLuint transform_feedback_buffer; // GL_TRANSFORM_FEEDBACK_BUFFER

  GLuint copy_read_buffer;  // GL_COPY_READ_BUFFER
  GLuint copy_write_buffer; // GL_COPY_WRITE_BUFFER

  GLuint shader_storage_buffer; // GL_SHADER_STORAGE_BUFFER
  GLuint atomic_counter_buffer; // GL_ATOMIC_COUNTER_BUFFER

  GLuint dispatch_indirect_buffer; // GL_DISPATCH_INDIRECT_BUFFER
  GLuint draw_indirect_buffer;     // GL_DRAW_INDIRECT_BUFFER

  GLint unpack_alignment;
  GLint unpack_row_length;
  GLint unpack_skip_rows;
  GLint unpack_skip_pixels;
  GLint unpack_image_height;
  GLint unpack_skip_images;
} GLState;

extern GLState stub_gl_state;

extern AttribState g_attrib_stub_state[MAX_VERTEX_ATTRIBS];
extern GLContextState g_stub_ctx[MAX_CONTEXTS];

static inline size_t gl_bytes_per_pixel(GLenum format, GLenum type)
{
  /* ---- PACKED TYPES ---- */
  switch (type)
  {
  case GL_UNSIGNED_SHORT_5_6_5:
  case GL_UNSIGNED_SHORT_4_4_4_4:
  case GL_UNSIGNED_SHORT_5_5_5_1:
    return 2;

  case GL_UNSIGNED_INT_2_10_10_10_REV:
  case GL_UNSIGNED_INT_10F_11F_11F_REV:
  case GL_UNSIGNED_INT_5_9_9_9_REV:
    return 4;

  case GL_UNSIGNED_INT_24_8:
    return 4;

#ifdef GL_FLOAT_32_UNSIGNED_INT_24_8_REV
  case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
    return 8;
#endif
  default:
    break;
  }

  /* ---- COMPONENT COUNT ---- */
  size_t comps = 0;

  switch (format)
  {
  case GL_RED:
  case GL_RED_INTEGER:
  case GL_ALPHA:
  case GL_LUMINANCE:
  case GL_DEPTH_COMPONENT:
    comps = 1;
    break;

  case GL_RG:
  case GL_RG_INTEGER:
  case GL_LUMINANCE_ALPHA:
    comps = 2;
    break;

  case GL_RGB:
  case GL_RGB_INTEGER:
    comps = 3;
    break;

  case GL_RGBA:
  case GL_RGBA_INTEGER:
    comps = 4;
    break;

  case GL_DEPTH_STENCIL:
    comps = 2;
    break;

  default:
    return 4;
  }

  /* ---- BYTES PER COMPONENT ---- */
  size_t bpc = 0;

  switch (type)
  {
  case GL_UNSIGNED_BYTE:
  case GL_BYTE:
    bpc = 1;
    break;

  case GL_UNSIGNED_SHORT:
  case GL_SHORT:
  case GL_HALF_FLOAT:
    bpc = 2;
    break;

  case GL_UNSIGNED_INT:
  case GL_INT:
  case GL_FLOAT:
    bpc = 4;
    break;

  default:
    return 4;
  }

  return comps * bpc;
}

static inline size_t gl_align_up_size(size_t value, size_t alignment)
{
  if (alignment <= 1)
    return value;
  return ((value + alignment - 1) / alignment) * alignment;
}

static inline size_t gl_unpack_alignment(void)
{
  return stub_gl_state.unpack_alignment > 0
             ? (size_t)stub_gl_state.unpack_alignment
             : 4u;
}

static inline size_t gl_unpack_image_size_2d(GLsizei width, GLsizei height,
                                             GLenum format, GLenum type)
{
  if (width <= 0 || height <= 0)
    return 0;

  size_t bpp = gl_bytes_per_pixel(format, type);
  size_t row_pixels = stub_gl_state.unpack_row_length > 0
                          ? (size_t)stub_gl_state.unpack_row_length
                          : (size_t)width;
  size_t row_stride = gl_align_up_size(row_pixels * bpp, gl_unpack_alignment());
  size_t start = (size_t)stub_gl_state.unpack_skip_rows * row_stride +
                 (size_t)stub_gl_state.unpack_skip_pixels * bpp;

  return start + (size_t)(height - 1) * row_stride + (size_t)width * bpp;
}

static inline size_t gl_unpack_image_size_3d(GLsizei width, GLsizei height,
                                             GLsizei depth, GLenum format,
                                             GLenum type)
{
  if (width <= 0 || height <= 0 || depth <= 0)
    return 0;

  size_t bpp = gl_bytes_per_pixel(format, type);
  size_t row_pixels = stub_gl_state.unpack_row_length > 0
                          ? (size_t)stub_gl_state.unpack_row_length
                          : (size_t)width;
  size_t image_rows = stub_gl_state.unpack_image_height > 0
                          ? (size_t)stub_gl_state.unpack_image_height
                          : (size_t)height;
  size_t row_stride = gl_align_up_size(row_pixels * bpp, gl_unpack_alignment());
  size_t image_stride = row_stride * image_rows;
  size_t start = (size_t)stub_gl_state.unpack_skip_images * image_stride +
                 (size_t)stub_gl_state.unpack_skip_rows * row_stride +
                 (size_t)stub_gl_state.unpack_skip_pixels * bpp;

  return start + (size_t)(depth - 1) * image_stride +
         (size_t)(height - 1) * row_stride + (size_t)width * bpp;
}

static inline size_t gl_image_size_2d(GLsizei w, GLsizei h, GLenum format,
                                      GLenum type)
{
  return (size_t)w * (size_t)h * gl_bytes_per_pixel(format, type);
}

static inline size_t gl_image_size_3d(GLsizei w, GLsizei h, GLsizei d,
                                      GLenum format, GLenum type)
{
  return (size_t)w * (size_t)h * (size_t)d * gl_bytes_per_pixel(format, type);
}

static inline int is_client_pointer(const void *ptr)
{
  uintptr_t p = (uintptr_t)ptr;

  // NULL is not a client pointer
  if (p == 0)
    return 0;

  // Small integers (0x1–0xFFFF) are offsets, not pointers
  if (p < 0x10000)
    return 0;

  // Otherwise treat as real client pointer
  return 1;
}

extern void loc_cache_invalidate_program(GLuint program);
