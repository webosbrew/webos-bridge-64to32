#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

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

extern uint32_t bridge_data_write(const void *src, size_t size);

static inline size_t index_type_size(GLenum type)
{
  switch (type)
  {
  case GL_UNSIGNED_BYTE:
    return 1;
  case GL_UNSIGNED_SHORT:
    return 2;
  case GL_UNSIGNED_INT:
    return 4;
  default:
    return 0;
  }
}

static inline size_t attrib_type_size(GLenum type)
{
  switch (type)
  {
  case GL_BYTE:
  case GL_UNSIGNED_BYTE:
    return 1;
  case GL_SHORT:
  case GL_UNSIGNED_SHORT:
  case GL_HALF_FLOAT:
    return 2;
  case GL_INT:
  case GL_UNSIGNED_INT:
  case GL_FLOAT:
  case GL_FIXED:
  case GL_INT_2_10_10_10_REV:
  case GL_UNSIGNED_INT_2_10_10_10_REV:
    return 4;
  default:
    return 4;
  }
}

static inline StubMapEntry *find_stub_map(GLuint buffer, GLenum target)
{
  for (uint32_t mi = 1; mi < MAX_MAPS; mi++)
  {
    StubMapEntry *m = &stub_maps[mi];
    if (m->ptr && m->buffer == buffer && m->target == target)
      return m;
  }
  return NULL;
}

/* Scan index values to find the [min,max] vertex range */
static inline bool mapped_index_bounds(const uint8_t *idx, GLsizei count,
                                       GLenum type, GLint basevertex,
                                       GLintptr *min_vertex,
                                       GLintptr *max_vertex)
{
  if (!idx || count <= 0)
    return false;

  bool have_index = false;
  GLintptr min_v = 0;
  GLintptr max_v = 0;

  for (GLsizei i = 0; i < count; i++)
  {
    uint32_t raw = 0;

    switch (type)
    {
    case GL_UNSIGNED_BYTE:
      raw = idx[i];
      break;
    case GL_UNSIGNED_SHORT:
    {
      uint16_t v;
      memcpy(&v, idx + (size_t)i * sizeof(v), sizeof(v));
      raw = v;
      break;
    }
    case GL_UNSIGNED_INT:
    {
      uint32_t v;
      memcpy(&v, idx + (size_t)i * sizeof(v), sizeof(v));
      raw = v;
      break;
    }
    default:
      return false;
    }

    GLintptr vertex = (GLintptr)raw + (GLintptr)basevertex;
    if (!have_index)
    {
      min_v = vertex;
      max_v = vertex;
      have_index = true;
    }
    else
    {
      if (vertex < min_v)
        min_v = vertex;
      if (vertex > max_v)
        max_v = vertex;
    }
  }

  if (!have_index || max_v < 0)
    return false;

  if (min_v < 0)
    min_v = 0;

  *min_vertex = min_v;
  *max_vertex = max_v;
  return true;
}

/* Shared by glDrawArrays,
 * glDrawElements, and glDrawElementsBaseVertex. */
static inline uint32_t
stub_vbo_piggyback_range(VAOState *vao, GLintptr first_vertex,
                         GLintptr last_vertex, uint32_t *out_buffer,
                         uint32_t *out_offset, uint32_t *out_length)
{
  *out_buffer = 0;
  *out_offset = 0;
  *out_length = 0;

  for (int ai = 0; ai < MAX_VERTEX_ATTRIBS; ai++)
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

      size_t stride = a->stride ? (size_t)a->stride : 1;
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
        break; /* not the buffer this draw actually uses */

      GLintptr local_start = byte_start - m->offset;
      size_t copy_len = (size_t)(byte_end - byte_start);
      if ((size_t)local_start >= (size_t)m->length)
        break;
      if ((size_t)local_start + copy_len > (size_t)m->length)
        copy_len = (size_t)m->length - (size_t)local_start;

      *out_buffer = a->vbo;
      *out_offset = (uint32_t)byte_start;
      *out_length = (uint32_t)copy_len;
      return bridge_data_write((uint8_t *)m->ptr + local_start, copy_len);
    }
  }

  return 0;
}

extern void loc_cache_invalidate_program(GLuint program);
