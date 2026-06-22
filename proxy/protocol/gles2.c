#include <GLES3/gl32.h>

#define LOG_PREFIX "[proxy/gles2]"
#include "../bridge/shared_util.h"
#include "../include/gles_util_proxy.h"
#include "../proxy.h"

#ifdef DEBUG_VERBOSE
#include <sys/syscall.h>
#include <unistd.h>
#endif

AttribState g_attrib_proxy_state[MAX_VERTEX_ATTRIBS];
GLContextState g_proxy_ctx[MAX_CONTEXTS];

/* ══════════════════════════════════════════════════════════════════════════
 * Per-opcode handlers
 * Each takes (BridgeCtrl *C, uint8_t *D) where D = data base address.
 * They unpack args, call the real GL function, write ctrl->result if needed.
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Textures ───────────────────────────────────────────────────────────────
 */
void h_glActiveTexture(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum texture = ar_u32(&r);
#ifdef DEBUG_VERBOSE
  log_console("h_glActiveTexture (tid=%ld): tex=%u", syscall(SYS_gettid),
              texture);
#endif
  glActiveTexture(texture);
  (void)D;
}

void h_glBindTexture(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum target = ar_u32(&r);
  GLuint texture = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glBindTexture (tid=%ld): target=%d tex=%u g_current_ctx=%d",
              syscall(SYS_gettid), target, texture, g_current_ctx);
  GLenum prev_err = glGetError();
#endif

  glBindTexture(target, texture);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR(
      "h_glBindTexture FAILED: prev_err=0x%x after_err=0x%x target=%d tex=%u",
      prev_err, after_err, target, texture);
#endif
  (void)D;
}

void h_glGenTextures(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLsizei n = ar_i32(&r);
#ifdef DEBUG_VERBOSE
  GLuint *textures = (GLuint *)dp(C->data_offset);
  GLenum prev_err = glGetError();
  glGenTextures(n, textures);
  for (int i = 0; i < n; i++)
    log_console("h_glGenTextures n=%d textures[%d]=%u g_current_ctx=%d", n, i,
                textures[i], g_current_ctx);
  GL_LOG_IF_ERR("h_glGenTextures: prev_err=0x%x after_err=0x%x", prev_err,
                after_err);
#else
  glGenTextures(n, (GLuint *)dp(C->data_offset));
#endif
  C->result = 0;
}

void h_glDeleteTextures(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLsizei n = ar_i32(&r);
  glDeleteTextures(n, (const GLuint *)dp(C->data_offset));
  (void)D;
}

void h_glTexImage2D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum target = ar_u32(&r);
  GLint level = ar_i32(&r);
  GLint ifmt = ar_i32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);
  GLint border = ar_i32(&r);
  GLenum format = ar_u32(&r);
  GLenum type = ar_u32(&r);
  GLuint has_pixels = ar_u32(&r);

  const void *pixels = (has_pixels && C->data_size) ? dp(C->data_offset) : NULL;

#ifdef DEBUG_VERBOSE
  log_console("h_glTexImage2D: target=%d level=%d ifmt=%d width=%d height=%d "
              "border=%d format=%d type=%d has_pixels=%d",
              target, level, ifmt, width, height, border, format, type,
              has_pixels);
  GLenum err_before = glGetError();
#endif

  glTexImage2D(target, level, ifmt, width, height, border, format, type,
               pixels);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("AUTO-ALLOC glTexImage2D FAILED: err_before=0x%x "
                "after_err=0x%x (%dx%d)",
                err_before, after_err, width, height);
#endif

  (void)D;
}

void h_glTexSubImage2D(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLint level = ar_i32(&r);
  GLint xoff = ar_i32(&r);
  GLint yoff = ar_i32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);
  GLenum format = ar_u32(&r);
  GLenum type = ar_u32(&r);
  uint32_t pixel_mode = ar_u32(&r);

  void *data = NULL;

  switch (pixel_mode)
  {
  case 0:
    data = NULL;
    break;

  case 1:
    // PBO offset: pointer value passed directly from client
    data = (void *)(uintptr_t)C->data_offset; // matches stub's raw offset
    break;

  default:
    // Raw client data in bridge shm
    data = dp(C->data_offset);
    break;
  }

#ifdef DEBUG_VERBOSE
  GLint bound = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);

  log_console(
      "h_glTexSubImage2D: IN target=%d level=%d xoff=%d yoff=%d "
      "width=%d height=%d format=%d type=%d data=%p bound=%d pixel_mode=%d",
      target, level, xoff, yoff, width, height, format, type, data, bound,
      pixel_mode);

  GLenum err_before = glGetError();
#endif

  glTexSubImage2D(target, level, xoff, yoff, width, height, format, type, data);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("h_glTexSubImage2D err before=0x%x after_err=0x%x", err_before,
                after_err);
#endif
}

void h_glCompressedTexImage2D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum t = ar_u32(&r);
  GLint lv = ar_i32(&r);
  GLenum ifmt = ar_u32(&r);
  GLsizei w = ar_i32(&r);
  GLsizei h = ar_i32(&r);
  GLint border = ar_i32(&r);
  GLsizei imgsz = ar_i32(&r);

  uint32_t pixel_mode = ar_u32(&r);
  const void *data;

  switch (pixel_mode)
  {
  case 0:
    data = NULL;
    break;
  case 1:
    // PBO offset
    data = (void *)(uintptr_t)C->data_offset;
    break;
  default:
    data = dp(C->data_offset);
    break;
  }

  glCompressedTexImage2D(t, lv, ifmt, w, h, border, imgsz, data);
  (void)D;
}

void h_glCompressedTexSubImage2D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum t = ar_u32(&r);
  GLint lv = ar_i32(&r);
  GLint xo = ar_i32(&r);
  GLint yo = ar_i32(&r);
  GLsizei w = ar_i32(&r);
  GLsizei h = ar_i32(&r);
  GLenum fmt = ar_u32(&r);
  GLsizei imgsz = ar_i32(&r);

  uint32_t pixel_mode = ar_u32(&r);
  const void *data;

  switch (pixel_mode)
  {
  case 0:
    data = NULL;
    break;
  case 1:
    // PBO offset
    data = (void *)(uintptr_t)C->data_offset;
    break;
  default:
    data = dp(C->data_offset);
    break;
  }

  glCompressedTexSubImage2D(t, lv, xo, yo, w, h, fmt, imgsz, data);
  (void)D;
}

void h_glCopyTexImage2D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum t = ar_u32(&r);
  GLint lv = ar_i32(&r);
  GLenum ifmt = ar_u32(&r);
  GLint x = ar_i32(&r);
  GLint y = ar_i32(&r);
  GLsizei w = ar_i32(&r);
  GLsizei h = ar_i32(&r);
  GLint b = ar_i32(&r);
  glCopyTexImage2D(t, lv, ifmt, x, y, w, h, b);
  (void)D;
}

void h_glCopyTexSubImage2D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum t = ar_u32(&r);
  GLint lv = ar_i32(&r);
  GLint xo = ar_i32(&r);
  GLint yo = ar_i32(&r);
  GLint x = ar_i32(&r);
  GLint y = ar_i32(&r);
  GLsizei w = ar_i32(&r);
  GLsizei h = ar_i32(&r);
  glCopyTexSubImage2D(t, lv, xo, yo, x, y, w, h);
  (void)D;
}

void h_glTexParameterf(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glTexParameterf(ar_u32(&r), ar_u32(&r), ar_f32(&r));
  (void)D;
}

void h_glTexParameteri(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glTexParameteri(ar_u32(&r), ar_u32(&r), ar_i32(&r));
  (void)D;
}

void h_glTexParameterfv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum t = ar_u32(&r);
  GLenum p = ar_u32(&r);
  glTexParameterfv(t, p, (const GLfloat *)dp(C->data_offset));
}

void h_glTexParameteriv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum t = ar_u32(&r);
  GLenum p = ar_u32(&r);
  glTexParameteriv(t, p, (const GLint *)dp(C->data_offset));
}

void h_glGetTexParameterfv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  GLenum t = ar_u32(&r);
  GLenum p = ar_u32(&r);
  glGetTexParameterfv(t, p, (GLfloat *)dp(C->data_offset));
  C->result = 0;
}

void h_glGetTexParameteriv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum t = ar_u32(&r);
  GLenum p = ar_u32(&r);

  GLint tmp[4] = {0, 0, 0, 0};
  size_t count = (p == GL_TEXTURE_BORDER_COLOR) ? 4 : 1;

  glGetTexParameteriv(t, p, tmp);

  GLint *out = (GLint *)dp(C->data_offset);
  if (out)
    for (size_t i = 0; i < count; i++)
      out[i] = tmp[i];

  C->result = 0;
}

void h_glGenerateMipmap(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glGenerateMipmap(ar_u32(&r));
  (void)D;
}
void h_glPixelStorei(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  int pname = ar_u32(&r);
  int param = ar_i32(&r);
#ifdef DEBUG_VERBOSE
  log_console("h_glPixelStorei: pname=%d param=%d", pname, param);
#endif
  glPixelStorei(pname, param);
#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("glPixelStorei(%x,%d) after_err=%x", pname, param, after_err);
#endif
  (void)D;
}

/* ── Buffers ─────────────────────────────────────────────────────────────── */
void h_glGenBuffers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;

  AR(r);
  GLsizei n = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glGenBuffers n=%d", n);
#endif

  GLuint *buffers = (GLuint *)dp(C->data_offset);

#ifdef DEBUG_VERBOSE
  GLenum prev_err = glGetError();
#endif

  glGenBuffers(n, buffers);

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("h_glGenBuffers buffers[%d]=%u", i, buffers[i]);

  GL_LOG_IF_ERR("h_glGenBuffers prev_err=0x%x after_err=0x%x", prev_err,
                after_err);
#endif

  C->result = 0;
}

void h_glDeleteBuffers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;

  AR(r);
  GLsizei n = ar_i32(&r);

  GLuint *buffers = (GLuint *)dp(C->data_offset);

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("h_glDeleteBuffers buffers[%d]=%u", i, buffers[i]);

  for (uint32_t j = 1; j < MAX_MAPS; ++j)
  {
    if (maps[j].real_ptr)
      log_console(
          "h_glDeleteBuffers: MAP[%u]: buffer=%u target=0x%04x len=%lld", j,
          maps[j].buffer, maps[j].target, (long long)maps[j].length);
  }
#endif

  glDeleteBuffers(n, buffers);

  for (GLsizei i = 0; i < n; ++i)
  {
    GLuint b = buffers[i];

#ifdef DEBUG_VERBOSE
    log_console("h_glDeleteBuffers: i:%d n:%d b:%d", i, n, b);
#endif

    for (uint32_t j = 1; j < MAX_MAPS; ++j)
    {
      if (maps[j].real_ptr && maps[j].buffer == b)
      {
#ifdef DEBUG_VERBOSE
        log_console("h_glDeleteBuffers: deleting maps[%d].buffer=%d", j,
                    maps[j].buffer);
#endif
        maps[j].real_ptr = NULL;
        maps[j].length = 0;
        maps[j].offset = 0;
        maps[j].target = 0;
        maps[j].buffer = 0;
        maps[j].access = 0;
      }
    }
  }
}

void h_glBindBuffer(BridgeCtrl *C, uint8_t *D)
{

  AR(r);

  GLuint target = ar_u32(&r);
  GLuint buffer = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glBindBuffer: target=%d buffer=%u g_current_ctx:%d", target,
              buffer, g_current_ctx);
#endif

  if (target == GL_ELEMENT_ARRAY_BUFFER)
  {
    GLContextState *ctx = &g_proxy_ctx[g_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->ebo = buffer;
  }

  glBindBuffer(target, buffer);
  (void)D;
}

void h_glBufferData(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum target = ar_u32(&r);
  GLsizeiptr size = (GLsizeiptr)ar_u64(&r);
  GLenum usage = ar_u32(&r);
  uint32_t has_data = ar_u32(&r);
  const void *buf = (has_data && C->data_size) ? dp(C->data_offset) : NULL;

#ifdef DEBUG_VERBOSE
  log_console("h_glBufferData: target=%d size=%d usage:%d has_data=%d buf=%p "
              "g_current_ctx=%d",
              target, size, usage, has_data, buf, g_current_ctx);
  uint8_t *p = dp(C->data_offset);

  if (has_data && C->data_size)
  {
    log_console("h_glBufferData: UBO UPLOAD FIRST 32 BYTES:");
    for (int i = 0; i < 32; i++)
      log_console("%02x ", p[i]);
  }
#endif

  glBufferData(target, size, buf, usage);
  (void)D;
}

void h_glBufferSubData(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum t = ar_u32(&r);
  GLintptr off = (GLintptr)ar_u64(&r);
  GLsizeiptr sz = (GLsizeiptr)ar_u64(&r);

#ifdef DEBUG_VERBOSE
  if (t == GL_UNIFORM_BUFFER)
    log_console("h_glBufferSubData: target=0x%x off=%lld sz=%lld", t, off, sz);
#endif

  glBufferSubData(t, off, sz, dp(C->data_offset));
  (void)D;
}

void h_glGetBufferParameteriv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum t = ar_u32(&r);
  GLenum p = ar_u32(&r);

  GLint val = 0;
  glGetBufferParameteriv(t, p, &val);

  // write into shared memory
  GLint *out = (GLint *)dp(C->data_offset);
  if (out)
    *out = val;

  C->result = 0;
}

/* ── Framebuffers / Renderbuffers ────────────────────────────────────────── */
void h_glGenFramebuffers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;

  AR(r);
  GLsizei n = ar_i32(&r);
  GLuint *fbs = (GLuint *)dp(C->data_offset);

#ifdef DEBUG_VERBOSE
  log_console("h_glGenFramebuffers (tid=%ld) n=%d fbs=%p",
              (long)syscall(SYS_gettid), n, fbs);
  GLenum prev_err = glGetError();
#endif

  glGenFramebuffers(n, fbs);

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("h_glGenFramebuffers fbs[%d]=%u", i, fbs[i]);

  GL_LOG_IF_ERR("h_glGenFramebuffers prev=0x%x after_err=0x%x", prev_err,
                after_err);
#endif

  C->result = 0;
}

void h_glDeleteFramebuffers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;

  AR(r);
  GLsizei n = ar_i32(&r);

  GLuint *fbs = (GLuint *)dp(C->data_offset);

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("h_glDeleteFramebuffers fbs[%d]=%u", i, fbs[i]);
#endif

  glDeleteFramebuffers(n, fbs);
}

static GLuint current_draw_fb = 0;
static GLuint current_read_fb = 0;

void h_glBindFramebuffer(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

#ifdef DEBUG_VERBOSE
  GLenum target = ar_u32(&r);
  GLuint fb = ar_u32(&r);

  log_console("[h_glBindFramebuffer] BEGIN");
  log_console("    target=0x%04x fb=%u g_current_ctx=%d", target, fb,
              g_current_ctx);

  GLenum prev_err = glGetError();
  log_console("    prev_gl_err=0x%04x", prev_err);

  glBindFramebuffer(target, fb);

  GL_LOG_IF_ERR("    glBindFramebuffer -> after_err=0x%04x", after_err);

  switch (target)
  {
  case GL_FRAMEBUFFER:
    current_draw_fb = fb;
    current_read_fb = fb;
    break;
#ifdef GL_DRAW_FRAMEBUFFER
  case GL_DRAW_FRAMEBUFFER:
    current_draw_fb = fb;
    break;
#endif
#ifdef GL_READ_FRAMEBUFFER
  case GL_READ_FRAMEBUFFER:
    current_read_fb = fb;
    break;
#endif
  }

  log_console("    BindFB target=0x%x fb=%u draw=%u read=%u", target, fb,
              current_draw_fb, current_read_fb);

  static int fbo4_logged = 0;
  static int fbo1_logged = 0;

  if (!fbo4_logged && current_draw_fb == 4)
  {
    fbo4_logged = 1;

    GLint attachment = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          &attachment);

    log_console("[FBO4] FIRST DRAW BIND: color attachment = %d", attachment);
  }

  if (!fbo1_logged && current_draw_fb == 1)
  {
    fbo1_logged = 1;

    GLint attachment = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
                                          &attachment);

    log_console("[FBO1] FIRST DRAW BIND: color attachment = %d", attachment);
  }

  log_console("[h_glBindFramebuffer] END");

#else
  glBindFramebuffer(ar_u32(&r), ar_u32(&r));
#endif
}

void h_glFramebufferTexture2D(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum target = ar_u32(&r);
  GLenum attachment = ar_u32(&r);
  GLenum textarget = ar_u32(&r);
  GLuint texture = ar_u32(&r);
  GLint level = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glFramebufferTexture2D: target=%d attachment=%d textarget=%d "
              "tex=%u level=%d g_current_ctx=%d",
              target, attachment, textarget, texture, level, g_current_ctx);
  GLint before_err = glGetError();
#endif

  glFramebufferTexture2D(target, attachment, textarget, texture, level);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("h_glFramebufferTexture2D: before_err=0x%x after_err=0x%x",
                before_err, after_err);

  GLint type = 0;
  GLint obj = 0;

  glGetFramebufferAttachmentParameteriv(
      target, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);

  glGetFramebufferAttachmentParameteriv(
      target, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &obj);

  log_console("attached type=%x object=%d", type, obj);

  // dump_fbo_state("h_glFramebufferTexture2D");
#endif

  (void)D;
}

void h_glFramebufferRenderbuffer(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
#ifdef DEBUG_VERBOSE
  GLenum target = ar_u32(&r);
  GLenum attachment = ar_u32(&r);
  GLenum renderbuffertarget = ar_u32(&r);
  GLuint renderbuffer = ar_u32(&r);

  log_console(
      "h_glFramebufferRenderbuffer: target=%d attachment=%d rbtgt=%d rb=%u",
      target, attachment, renderbuffertarget, renderbuffer);

  EGLint before_egl_err = eglGetError();
  GLuint prev_err = glGetError();

  glFramebufferRenderbuffer(target, attachment, renderbuffertarget,
                            renderbuffer);
  GLenum after_egl_err = eglGetError();

  GL_LOG_IF_ERR("h_glGenRenderbuffers: prev_err=0x%x err=0x%x "
                "before_egl_err=0x%x before_egl_err=0x%x",
                prev_err, after_err, before_egl_err, after_egl_err);

#ifdef DEBUG_ABORT_ON_GL_ERROR
  if (after_egl_err != EGL_SUCCESS)
    abort();
#endif

#else
  glFramebufferRenderbuffer(ar_u32(&r), ar_u32(&r), ar_u32(&r), ar_u32(&r));
#endif
  (void)D;
}
void h_glCheckFramebufferStatus(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
#ifdef DEBUG_VERBOSE
  GLint target = ar_u32(&r);
  GLenum st = glCheckFramebufferStatus(target);
  GLenum err = glGetError();
  log_console("h_glCheckFramebufferStatus target=0x%x result=0x%x err=0x%x",
              target, st, err);
  C->result = st;
#else
  C->result = glCheckFramebufferStatus(ar_u32(&r));
#endif
  (void)D;
}

void h_glGetFramebufferAttachmentParameteriv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum t = ar_u32(&r);
  GLenum att = ar_u32(&r);
  GLenum p = ar_u32(&r);

  GLint val = 0;
  glGetFramebufferAttachmentParameteriv(t, att, p, &val);

  C->result = (uint64_t)(int64_t)val;
}

void h_glGenRenderbuffers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;

  AR(r);

  GLsizei n = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glGenRenderbuffers: n=%d", n);
  GLenum prev_err = glGetError();
#endif

  GLuint *rbs = (GLuint *)dp(C->data_offset);

  glGenRenderbuffers(n, rbs);

#ifdef DEBUG_VERBOSE
  for (int i = 0; i < n; i++)
    log_console("h_glGenRenderbuffers rbs[%d]=%u", i, rbs[i]);

  GL_LOG_IF_ERR("h_glGenRenderbuffers: prev_err=0x%x after_err=0x%x", prev_err,
                after_err);
#endif

  C->result = 0;
}

void h_glDeleteRenderbuffers(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLsizei n = ar_i32(&r);
  if (n <= 0)
  {
    C->result = 0;
    return;
  }

  GLuint *tmp = alloca(sizeof(GLuint) * n);

  for (int i = 0; i < n; i++)
    tmp[i] = ar_u32(&r);

  glDeleteRenderbuffers(n, tmp);

  C->result = 0;
}

void h_glBindRenderbuffer(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLuint rb = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("[h_glBindRenderbuffer] BEGIN");
  log_console("    target=0x%04x", target);
  log_console("    rb=%u", rb);

  GLenum prev_err = glGetError();
  log_console("    prev_gl_err=0x%04x", prev_err);
#endif

  glBindRenderbuffer(target, rb);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("    glBindRenderbuffer -> after_err=0x%04x", after_err);

  GLint cur_rb = 0;
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &cur_rb);
  log_console("    now bound rb=%d", cur_rb);

  log_console("[h_glBindRenderbuffer] END");
#endif
}

void h_glRenderbufferStorage(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLenum target = ar_u32(&r);
  GLenum internalformat = ar_u32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  log_console("[h_glRenderbufferStorage] BEGIN");
  log_console("    target=0x%04x", target);
  log_console("    internalformat=0x%04x", internalformat);
  log_console("    width=%d height=%d", width, height);

  GLenum prev_err = glGetError();
  log_console("    prev_gl_err=0x%04x", prev_err);
#endif

  glRenderbufferStorage(target, internalformat, width, height);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("    glRenderbufferStorage -> after_err=0x%04x", after_err);

  GLint cur_rb = 0;
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &cur_rb);
  log_console("    currently bound rb=%d", cur_rb);

  log_console("[h_glRenderbufferStorage] END");
#endif
}

void h_glGetRenderbufferParameteriv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum t = ar_u32(&r);
  GLenum p = ar_u32(&r);

  GLint val = 0;
  glGetRenderbufferParameteriv(t, p, &val);

  C->result = (uint64_t)(int64_t)val;
  (void)D;
}

/* ── Shaders ─────────────────────────────────────────────────────────────── */
void h_glCreateShader(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
#ifdef DEBUG_SHADERS
  GLenum shaderType = ar_u32(&r);
  log_console("h_glCreateShader: shaderType: %d", shaderType);
  C->result = glCreateShader(shaderType);
  log_console("glCreateShader returned %llu", C->result);
#else
  // returns 0 if an error occurs creating the shader object
  C->result = glCreateShader(ar_u32(&r));
#endif

  (void)D;
}

void h_glDeleteShader(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glDeleteShader(ar_u32(&r));
  (void)D;
}

void h_glShaderSource(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint shader = ar_u32(&r);
  GLsizei count = ar_i32(&r);

  if (count < 0)
    count = 0;
  if (count > 32)
  {
    log_error("h_glShaderSource: count:%d > 32", count);
    count = 32;
  }

  const GLchar *src[32];
  GLint lens[32];

  for (int i = 0; i < count; i++)
  {
    uint32_t off = ar_u32(&r);
    uint32_t len = ar_u32(&r);

    lens[i] = (GLint)len;

    if (off == 0 && len == 0)
      src[i] = NULL;
    else
      src[i] = (off == 0 && len == 0) ? NULL : (const GLchar *)dp(off);

#ifdef DEBUG_DUMP_SHADERS
    if (src[i] && len > 0)
    {
      char tmp[512];
      uint32_t dump = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
      memcpy(tmp, src[i], dump);
      tmp[dump] = '\0';

      log_console("h_glShaderSource: src[%d] len=%u:\n%s", i, len, tmp);
    }
#endif
  }

  glShaderSource(shader, count, src, lens);
#ifdef DEBUG_SHADERS
  GL_LOG_IF_ERR("glShaderSource ERROR=0x%04x", after_err);
#endif
}

void h_glCompileShader(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLuint shader = ar_u32(&r);

#ifdef DEBUG_SHADERS
  log_console("ENTER glCompileShader(%u)", shader);

  GLenum e = glGetError();

  if (e != GL_NO_ERROR)
    log_console("pre=0x%x", e);

  log_console("calling real glCompileShader...");
#endif

  glCompileShader(shader);

#ifdef DEBUG_SHADERS
  log_console("returned from glCompileShader");

  GL_LOG_IF_ERR("post=0x%x", after_err);

  GLint status = 0;
  GLint loglen = 0;

  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &loglen);

  log_console("compile=%d loglen=%d", status, loglen);

  if (loglen > 1)
  {
    char *buf = calloc(1, loglen + 1);

    glGetShaderInfoLog(shader, loglen, NULL, buf);

    log_console("shader log:\n%s", buf);

    free(buf);
  }
#endif

  (void)D;
}

void h_glGetShaderiv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint shader = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLint val = 0;
  glGetShaderiv(shader, pname, &val);

#ifdef DEBUG_SHADERS
  log_console("h_glGetShaderiv: shader: %d pname: %d, params: %d", shader,
              pname, val);
#endif

  C->result = (uint64_t)(int64_t)val;
  (void)D;
}

void h_glGetShaderInfoLog(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint sh = ar_u32(&r);
  GLsizei size = ar_i32(&r);

  GLsizei len = 0;
  GLchar *buf = (GLchar *)dp(C->data_offset);

  if (size > 0 && buf)
    glGetShaderInfoLog(sh, size, &len, buf);
  else
    glGetShaderInfoLog(sh, 0, &len, NULL);

  C->result = (uint64_t)len;
  (void)D;
}

void h_glGetShaderSource(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint sh = ar_u32(&r);
  GLsizei size = ar_i32(&r);

  GLsizei len = 0;
  GLchar *buf = (GLchar *)dp(C->data_offset);

  if (size > 0 && buf)
    glGetShaderSource(sh, size, &len, buf);
  else
    glGetShaderSource(sh, 0, &len, NULL);

  C->result = (uint64_t)len;
  (void)D;
}

void h_glShaderBinary(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLsizei cnt = ar_i32(&r);
  GLenum fmt = ar_u32(&r);
  GLsizei len = ar_i32(&r);
  glShaderBinary(cnt, (const GLuint *)dp(C->data_offset), fmt,
                 dp(C->data2_offset), len);
  (void)D;
}
void h_glReleaseShaderCompiler(BridgeCtrl *C, uint8_t *D)
{
  glReleaseShaderCompiler();
  (void)C;
  (void)D;
}
void h_glGetShaderPrecisionFormat(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLenum st = ar_u32(&r);
  GLenum pt = ar_u32(&r);
  GLint *tmp = (GLint *)dp(C->data_offset); /* [range0,range1,precision] */
  GLint range[2], prec;
  glGetShaderPrecisionFormat(st, pt, range, &prec);
  tmp[0] = range[0];
  tmp[1] = range[1];
  tmp[2] = prec;
  C->result = 0;
  (void)D;
}

/* ── Programs ────────────────────────────────────────────────────────────── */
void h_glCreateProgram(BridgeCtrl *C, uint8_t *D)
{
  C->result = glCreateProgram();

#ifdef DEBUG_SHADERS
  log_console("glCreateProgram -> %lluu (0x%llx)", C->result, C->result);
#endif

  (void)D;
}

void h_glDeleteProgram(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glDeleteProgram(ar_u32(&r));
  (void)D;
}
void h_glAttachShader(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

#ifdef DEBUG_SHADERS
  GLuint program = ar_u32(&r);
  GLuint shader = ar_u32(&r);
  log_console("h_glAttachShader (program %d, shader %d)", program, shader);
  glAttachShader(program, shader);

  GL_LOG_IF_ERR("h_glAttachShader ERROR 0x%04x (program=%u shader=%u)",
                after_err, program, shader);
#else
  glAttachShader(ar_u32(&r), ar_u32(&r));
#endif
  (void)D;
}

void h_glDetachShader(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glDetachShader(ar_u32(&r), ar_u32(&r));
  (void)D;
}

void h_glLinkProgram(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLuint program = ar_u32(&r);

#ifdef DEBUG_SHADER_TEST
  log_console("egl_displays[DEFAULT_EGL_IDX] %p egl_configs[DEFAULT_EGL_IDX] "
              "%p egl_contexts[DEFAULT_EGL_IDX] %p",
              egl_displays[DEFAULT_EGL_IDX].handle,
              egl_configs[DEFAULT_EGL_IDX].handle,
              egl_contexts[DEFAULT_EGL_IDX].handle);

  log_console("proxy_wl_compositor: %p proxy_wl_shell %p proxy_wl_webos_shell "
              "%p proxy_wl_registry %p",
              proxy_wl_compositor, proxy_wl_shell, proxy_wl_webos_shell,
              proxy_wl_registry);

  log_console("g_surfs[DEFAULT_WL_IDX] %p g_shell_surfs[DEFAULT_WL_IDX] %p  "
              "g_webos_shell_surfaces[DEFAULT_WL_IDX] %p "
              "proxy_wl_egl_windows[DEFAULT_WL_EGL_IDX]: %p",
              g_surfs[DEFAULT_WL_IDX], g_shell_surfs[DEFAULT_WL_IDX],
              g_webos_shell_surfaces[DEFAULT_WL_IDX],
              proxy_wl_egl_windows[DEFAULT_WL_EGL_IDX]);

  shaderTestSimple();
  shaderTestComplete();

  log_console("finished final shader tst");
  log_console("=== h_glLinkProgram - before glLinkProgram(program = %u) ===",
              program);

  dump_ctx("h_glLinkProgram - before link");

  GLint count = 0;
  GLuint shaders[8] = {0};

  glGetAttachedShaders(program, 8, &count, shaders);

  log_console("attached count=%d", count);

  for (int i = 0; i < count; i++)
    log_console("attached[%d]=%u", i, shaders[i]);

  GLint current = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &current);

  GLint framebuffer = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);

  log_console("before link: current_program=%d framebuffer=%d", current,
              framebuffer);

  log_console("current ctx=%p display=%p draw=%p", eglGetCurrentContext(),
              eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW));

#endif

  glLinkProgram(program);

#ifdef DEBUG_SHADERS
  GL_LOG_IF_ERR("glLinkProgram err=0x%x", after_err);

  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);

  GLint loglen = 0;
  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &loglen);

  log_console("link status=%d loglen=%d", ok, loglen);

  if (loglen > 1)
  {
    char *log = calloc(1, loglen + 1);

    glGetProgramInfoLog(program, loglen, NULL, log);

    log_console("program log:%s", log);

    free(log);
  }
#ifdef DEBUG_ABORT_ON_GL_ERROR
  if (ok = GL_FALSE)
    abort();
#endif
#endif

  (void)D;
}

void h_glUseProgram(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint program = ar_u32(&r);
#ifdef DEBUG_VERBOSE
  log_console("h_glUseProgram (tid=%ld) program=%u", syscall(SYS_gettid),
              program);
#endif
  glUseProgram(program);
  (void)D;
}

void h_glValidateProgram(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glValidateProgram(ar_u32(&r));
  (void)D;
}

void h_glGetProgramiv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint program = ar_u32(&r);
  GLenum pname = ar_u32(&r);
  GLint val = 0;
  glGetProgramiv(program, pname, &val);
  C->result = (uint64_t)(int64_t)val;
#ifdef DEBUG_SHADERS
  log_console("glGetProgramiv (program=%d pname=%d result=%d)", program, pname,
              (uint64_t)(int64_t)val);

  // failed validation
  if (pname == GL_VALIDATE_STATUS && val == 0)
  {
    glValidateProgram(program);
    GLint logLen = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
    if (logLen > 1)
    {
      char *buf = alloca(logLen);
      GLsizei written = 0;
      glGetProgramInfoLog(program, logLen, &written, buf);
      log_error("[prog %d] validate log: %s", program, buf);
#ifdef DEBUG_ABORT_ON_GL_ERROR
      abort();
#endif
    }
  }
#endif
  (void)D;
}

void h_glGetProgramInfoLog(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  GLsizei bufsz = ar_i32(&r);
  GLsizei len = 0;
  glGetProgramInfoLog(p, bufsz, &len, (GLchar *)dp(C->data_offset));
  C->result = (uint64_t)len;
  (void)D;
}

void h_glGetAttachedShaders(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint p = ar_u32(&r);
  GLsizei max = ar_i32(&r);

  GLsizei cnt = 0;
  GLuint *out = (GLuint *)dp(C->data_offset);

  if (max > 0 && out)
    glGetAttachedShaders(p, max, &cnt, out);
  else
    glGetAttachedShaders(p, 0, &cnt, NULL);

  C->result = (uint64_t)cnt;

#ifdef DEBUG_SHADERS
  log_console("h_glGetAttachedShaders count=%d", cnt);
  for (int i = 0; i < cnt; i++)
    log_console("attached[%d]=%u", i, out ? out[i] : 0);
#endif
}

/* ── Uniforms ────────────────────────────────────────────────────────────── */
void h_glGetUniformLocation(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  C->result = (uint64_t)(int64_t)glGetUniformLocation(
      p, (const GLchar *)dp(C->data_offset));
  (void)D;
}
void h_glGetActiveUniform(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  GLuint idx = ar_u32(&r);
  GLsizei bufsz = ar_i32(&r);
  GLsizei len = 0;
  GLint size = 0;
  GLenum type = 0;
  glGetActiveUniform(p, idx, bufsz, &len, &size, &type,
                     (GLchar *)dp(C->data_offset));
  C->result = ((uint64_t)type << 32) | (uint64_t)(uint32_t)size;
  *(GLsizei *)C->result_buf = len;
  (void)D;
}

void h_glGetUniformfv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  GLint loc = ar_i32(&r);
  glGetUniformfv(p, loc, (GLfloat *)dp(C->data_offset));
  C->result = 0;
  (void)D;
}

void h_glGetUniformiv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  GLint loc = ar_i32(&r);
  glGetUniformiv(p, loc, (GLint *)dp(C->data_offset));
  C->result = 0;
  (void)D;
}

/* Scalar uniform setters */
#define H_U1(s, GL_T, ar_fn, glfn)                                             \
  void h_glUniform1##s(BridgeCtrl *C, uint8_t *D)                              \
  {                                                                            \
    AR(r);                                                                     \
    GLint l = ar_i32(&r);                                                      \
    glfn(l, ar_fn(&r));                                                        \
    (void)D;                                                                   \
  }
#define H_U2(s, GL_T, ar_fn, glfn)                                             \
  void h_glUniform2##s(BridgeCtrl *C, uint8_t *D)                              \
  {                                                                            \
    AR(r);                                                                     \
    GLint l = ar_i32(&r);                                                      \
    GL_T a = ar_fn(&r), b = ar_fn(&r);                                         \
    glfn(l, a, b);                                                             \
    (void)D;                                                                   \
  }
#define H_U3(s, GL_T, ar_fn, glfn)                                             \
  void h_glUniform3##s(BridgeCtrl *C, uint8_t *D)                              \
  {                                                                            \
    AR(r);                                                                     \
    GLint l = ar_i32(&r);                                                      \
    GL_T a = ar_fn(&r), b = ar_fn(&r), c = ar_fn(&r);                          \
    glfn(l, a, b, c);                                                          \
    (void)D;                                                                   \
  }
#define H_U4(s, GL_T, ar_fn, glfn)                                             \
  void h_glUniform4##s(BridgeCtrl *C, uint8_t *D)                              \
  {                                                                            \
    AR(r);                                                                     \
    GLint l = ar_i32(&r);                                                      \
    GL_T a = ar_fn(&r), b = ar_fn(&r), c = ar_fn(&r), d = ar_fn(&r);           \
    glfn(l, a, b, c, d);                                                       \
    (void)D;                                                                   \
  }
#define H_UV(s, GL_T, comp, glfn)                                              \
  void h_glUniform##comp##s##v(BridgeCtrl *C, uint8_t *D)                      \
  {                                                                            \
    AR(r);                                                                     \
    GLint l = ar_i32(&r);                                                      \
    GLsizei cnt = ar_i32(&r);                                                  \
    glfn(l, cnt, (const GL_T *)dp(C->data_offset));                            \
    (void)D;                                                                   \
  }

H_U1(f, GLfloat, ar_f32, glUniform1f)
H_U1(i, GLint, ar_i32, glUniform1i)
H_U2(f, GLfloat, ar_f32, glUniform2f)
H_U2(i, GLint, ar_i32, glUniform2i)
H_U3(f, GLfloat, ar_f32, glUniform3f)
H_U3(i, GLint, ar_i32, glUniform3i)
H_U4(f, GLfloat, ar_f32, glUniform4f)
H_U4(i, GLint, ar_i32, glUniform4i)

H_UV(f, GLfloat, 1, glUniform1fv)
H_UV(i, GLint, 1, glUniform1iv)
H_UV(f, GLfloat, 2, glUniform2fv)
H_UV(i, GLint, 2, glUniform2iv)
H_UV(f, GLfloat, 3, glUniform3fv)
H_UV(i, GLint, 3, glUniform3iv)
H_UV(f, GLfloat, 4, glUniform4fv)
H_UV(i, GLint, 4, glUniform4iv)

#define H_UMAT(dim)                                                            \
  void h_glUniformMatrix##dim##fv(BridgeCtrl *C, uint8_t *D)                   \
  {                                                                            \
    AR(r);                                                                     \
    GLint l = ar_i32(&r);                                                      \
    GLsizei cnt = ar_i32(&r);                                                  \
    GLboolean t = ar_u32(&r);                                                  \
    glUniformMatrix##dim##fv(l, cnt, t, (const GLfloat *)dp(C->data_offset));  \
    (void)D;                                                                   \
  }
H_UMAT(2)
H_UMAT(3)
H_UMAT(4)

/* ── Attributes ───────────────────────────────────────────────────────────
 */
void h_glGetAttribLocation(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  C->result = (uint64_t)(int64_t)glGetAttribLocation(
      p, (const GLchar *)dp(C->data_offset));
  (void)D;
}
void h_glGetActiveAttrib(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  GLuint idx = ar_u32(&r);
  GLsizei bufsz = ar_i32(&r);
  GLsizei len = 0;
  GLint size = 0;
  GLenum type = 0;
  glGetActiveAttrib(p, idx, bufsz, &len, &size, &type,
                    (GLchar *)dp(C->data_offset));
  C->result = ((uint64_t)type << 32) | (uint64_t)(uint32_t)size;
  *(GLsizei *)C->result_buf = len;
  (void)D;
}
void h_glBindAttribLocation(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint p = ar_u32(&r);
  GLuint idx = ar_u32(&r);
  glBindAttribLocation(p, idx, (const GLchar *)dp(C->data_offset));
  (void)D;
}

void h_glVertexAttribPointer(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLuint index = ar_u32(&r);
  GLint size = ar_i32(&r);
  GLenum type = ar_u32(&r);
  GLboolean norm = (GLboolean)ar_u32(&r);
  GLsizei stride = ar_i32(&r);
  const void *ptr = (const void *)(uintptr_t)ar_u64(&r);
  GLuint vbo = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("[h_glVertexAttribPointer] index=%u size=%d type=0x%x "
              "norm=%d stride=%d ptr=%p vbo=%u g_current_ctx:%d",
              index, size, type, norm, stride, ptr, vbo, g_current_ctx);
#endif

  /* Update proxy-side VAO shadow state */
  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_proxy_state[index].size = size;
    g_attrib_proxy_state[index].type = type;
    g_attrib_proxy_state[index].normalized = norm;
    g_attrib_proxy_state[index].stride = stride;
    g_attrib_proxy_state[index].pointer = (uintptr_t)ptr;
    g_attrib_proxy_state[index].vbo = vbo;
    g_attrib_proxy_state[index].integer = GL_FALSE;

    g_proxy_ctx[g_current_ctx]
        .vaos[g_proxy_ctx[g_current_ctx].current_vao]
        .attribs[index] = g_attrib_proxy_state[index];
  }
  else
    log_error("h_glVertexAttribPointer: index:%d >= MAX_VERTEX_ATTRIBS", index);

  GLint prev_array_buffer = 0;
  if (vbo != 0)
  {
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

    /* If VBO != 0 - pointer is an offset, so bind VBO first.
       If VBO == 0 - pointer is a client pointer.
    */
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
  }

  glVertexAttribPointer(index, size, type, norm, stride, ptr);

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("glVertexAttribPointer error=0x%04x", after_err);
#endif

  if (vbo != 0)
    glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);

  (void)D;
}

void h_glEnableVertexAttribArray(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint index = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glEnableVertexAttribArray: index:%d", index);
#endif

  glEnableVertexAttribArray(index);

  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_proxy_state[index].enabled = GL_TRUE;
    GLContextState *ctx = &g_proxy_ctx[g_current_ctx];
    VAOState *vao = &ctx->vaos[ctx->current_vao];
    vao->attribs[index].enabled = GL_TRUE;
  }
  else
    log_error("h_glEnableVertexAttribArray: index:%d >= MAX_VERTEX_ATTRIBS",
              index);

  (void)D;
}
void h_glDisableVertexAttribArray(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLuint index = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glDisableVertexAttribArray: index:%d", index);
#endif

  glDisableVertexAttribArray(index);

  if (index < MAX_VERTEX_ATTRIBS)
  {
    g_attrib_proxy_state[index].enabled = GL_FALSE;
    g_proxy_ctx[g_current_ctx]
        .vaos[g_proxy_ctx[g_current_ctx].current_vao]
        .attribs[index]
        .enabled = GL_FALSE;
  }
  else
    log_error("h_glDisableVertexAttribArray: index:%d >= MAX_VERTEX_ATTRIBS",
              index);

  (void)D;
}

void h_glGetVertexAttribfv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  GLuint index = ar_u32(&r);
  GLenum pname = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glGetVertexAttribfv: index=%u pname=0x%x out=%p", index, pname,
              dp(C->data_offset));
#endif

  glGetVertexAttribfv(index, pname, (GLfloat *)dp(C->data_offset));

  C->result = 0;
}

void h_glGetVertexAttribiv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  glGetVertexAttribiv(ar_u32(&r), ar_u32(&r), (GLint *)dp(C->data_offset));
  C->result = 0;
}
void h_glGetVertexAttribPointerv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  void *ptr = NULL;
  glGetVertexAttribPointerv(ar_u32(&r), ar_u32(&r), &ptr);
  C->result = (uint64_t)(uintptr_t)ptr;
  (void)D;
}

/* VertexAttrib constant setters */
#define H_VA1(s, GL_T, ar_fn, fn)                                              \
  void h_glVertexAttrib1##s(BridgeCtrl *C, uint8_t *D)                         \
  {                                                                            \
    AR(r);                                                                     \
    fn(ar_u32(&r), ar_fn(&r));                                                 \
    (void)D;                                                                   \
  }
#define H_VAFV(comp, fn)                                                       \
  void h_glVertexAttrib##comp##fv(BridgeCtrl *C, uint8_t *D)                   \
  {                                                                            \
    AR(r);                                                                     \
    fn(ar_u32(&r), (const GLfloat *)dp(C->data_offset));                       \
    (void)D;                                                                   \
  }

H_VA1(f, GLfloat, ar_f32, glVertexAttrib1f)
H_VAFV(1, glVertexAttrib1fv)
void h_glVertexAttrib2f(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glVertexAttrib2f(ar_u32(&r), ar_f32(&r), ar_f32(&r));
  (void)D;
}
H_VAFV(2, glVertexAttrib2fv)
void h_glVertexAttrib3f(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glVertexAttrib3f(ar_u32(&r), ar_f32(&r), ar_f32(&r), ar_f32(&r));
  (void)D;
}
H_VAFV(3, glVertexAttrib3fv)
void h_glVertexAttrib4f(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glVertexAttrib4f(ar_u32(&r), ar_f32(&r), ar_f32(&r), ar_f32(&r), ar_f32(&r));
  (void)D;
}
H_VAFV(4, glVertexAttrib4fv)

/* ── Draw ────────────────────────────────────────────────────────────────── */

void h_glDrawArrays(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum mode = ar_u32(&r);
  GLint first = ar_i32(&r);
  GLsizei count = ar_i32(&r);

  uint32_t attrib_count = ar_u32(&r);

  GLContextState *ctx = &g_proxy_ctx[g_current_ctx];
  VAOState *vao = &ctx->vaos[ctx->current_vao];

  // First apply client-array patches
  for (uint32_t i = 0; i < attrib_count; i++)
  {
    GLuint attrib = ar_u32(&r);
    uint32_t off = ar_u32(&r);
    void *ptr = dp(off);

    g_attrib_proxy_state[attrib].pointer = (uintptr_t)ptr;
    vao->attribs[attrib].pointer = (uintptr_t)ptr;
  }

  uint32_t piggyback_buffer = ar_u32(&r);
  uint32_t piggyback_offset = ar_u32(&r);
  uint32_t piggyback_length = ar_u32(&r);

  if (piggyback_buffer && C->data2_size > 0)
  {
    for (uint32_t mi = 1; mi < MAX_MAPS; mi++)
    {
      if (!maps[mi].real_ptr || maps[mi].buffer != piggyback_buffer)
        continue;
      if ((GLintptr)piggyback_offset < maps[mi].offset ||
          (GLintptr)(piggyback_offset + piggyback_length) >
              maps[mi].offset + maps[mi].length)
        break;

      uint8_t *dst = (uint8_t *)maps[mi].real_ptr +
                     ((GLintptr)piggyback_offset - maps[mi].offset);
      memcpy(dst, dp(C->data2_offset), C->data2_size);

#ifdef DEBUG_VERBOSE
      log_console("h_glDrawArrays: piggyback wrote %u bytes into VBO=%u map "
                  "at buffer-offset=%u",
                  C->data2_size, piggyback_buffer, piggyback_offset);
#endif
      break;
    }
  }

#ifdef DEBUG_VERBOSE
  GLint fb = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
  GLint prog = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);

  GLint tex2D = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2D);

  GLint tex2DArr = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &tex2DArr);
  log_console(
      "h_glDrawArrays (tid=%ld): mode:%d first:%d count:%d attrib_count:%d "
      "vao:%d FB=%d prog=%d tex2D=%d tex2DArr=%d",
      syscall(SYS_gettid), mode, first, count, attrib_count, ctx->current_vao,
      fb, prog, tex2D, tex2DArr);

  if (tex2DArr != 0)
  {
    GLint w = 0, h = 0, d = 0;
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex2DArr);

    glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_DEPTH, &d);

    log_console("h_glDrawArrays tex2DArr:%d w:%d x %d x %d", tex2DArr, w, h, d);
  }

  for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
  {
    AttribState *a = &vao->attribs[i];
    log_console("VAO %u ATTR %d: enabled=%d vbo=%u ptr=0x%lx size=%d stride=%d "
                "type=0x%x integer=%d divisor=%d",
                ctx->current_vao, i, a->enabled, a->vbo, a->pointer, a->size,
                a->stride, a->type, a->integer, a->divisor);
  }
#endif

  GLint prev_array_buffer = 0;
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_array_buffer);

  bool restoreArrayBuffer = false;

  // Rebuild client-array attribs only (vbo==0).
  // VBO-backed attribs inside a named VAO are already correct in GPU state;
  // re-submitting them with stale proxy-side pointer values corrupts the draw.
  for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++)
  {
    AttribState *a = &vao->attribs[i];

    if (!a || !a->enabled)
      continue;

    // VBO-backed: GPU VAO already has the correct binding, skip.
    if (a->vbo != 0)
      continue;

    restoreArrayBuffer = true;
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const void *ptr = (const void *)a->pointer;

#ifdef DEBUG_VERBOSE
    log_console("h_glDrawArrays: i:%d a->vbo:%d ptr:%p integer:%d", i, a->vbo,
                ptr, a->integer);
#endif

    if (a->integer)
    {
      glVertexAttribIPointer(i, a->size, a->type, a->stride, ptr);
    }
    else
    {
      glVertexAttribPointer(i, a->size, a->type, a->normalized, a->stride, ptr);
    }
  }

  glDrawArrays(mode, first, count);

#ifdef DEBUG_VERBOSE
  if (fb == 1 && prog == 27)
  {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    uint8_t p[4] = {0};
    glReadPixels(vp[0] + vp[2] / 2, vp[1] + vp[3] / 2, 1, 1, GL_RGBA,
                 GL_UNSIGNED_BYTE, p);
    log_console("[PROBE] FBO1/prog27 center pixel = %u %u %u %u", p[0], p[1],
                p[2], p[3]);
  }
#endif

  if (restoreArrayBuffer)
    glBindBuffer(GL_ARRAY_BUFFER, prev_array_buffer);
}

void h_glDrawElements(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum mode = ar_u32(&r);
  GLsizei count = ar_i32(&r);
  GLenum type = ar_u32(&r);
  uintptr_t indices = ar_u64(&r);
  uint32_t is_client_ptr = ar_u32(&r);

  uint32_t ebo_piggyback_buffer = ar_u32(&r);
  uint32_t ebo_piggyback_offset = ar_u32(&r);
  uint32_t ebo_piggyback_length = ar_u32(&r);

  uint32_t piggyback_buffer = ar_u32(&r);
  uint32_t piggyback_offset = ar_u32(&r);
  uint32_t piggyback_length = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glDrawElements (tid=%ld) mode=0x%x count=%d type=0x%x idx=%p "
              "is_client_ptr=%d g_current_ctx=%d",
              syscall(SYS_gettid), mode, count, type, indices, is_client_ptr,
              g_current_ctx);
#endif

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
      break;
    }
  }

  if (piggyback_buffer && C->data2_size > 0)
  {
    for (uint32_t mi = 1; mi < MAX_MAPS; mi++)
    {
      if (!maps[mi].real_ptr || maps[mi].buffer != piggyback_buffer)
        continue;
      if ((GLintptr)piggyback_offset < maps[mi].offset ||
          (GLintptr)(piggyback_offset + piggyback_length) >
              maps[mi].offset + maps[mi].length)
        break;

      uint8_t *dst = (uint8_t *)maps[mi].real_ptr +
                     ((GLintptr)piggyback_offset - maps[mi].offset);
      memcpy(dst, dp(C->data2_offset), C->data2_size);
      break;
    }
  }

  if (!is_client_ptr)
  {
#ifdef DEBUG_VERBOSE
    log_console("h_glDrawElements: ebo mode");
#endif
    glDrawElements(mode, count, type, (const void *)indices);
    return;
  }

#ifdef DEBUG_VERBOSE
  log_console("h_glDrawElements: alt mode");
#endif

  const void *local = dp(C->data_offset);
  glDrawElements(mode, count, type, local);

  (void)D;
}

/* ── Rasterisation state ─────────────────────────────────────────────────── */
void h_glViewport(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
#ifdef DEBUG_VERBOSE
  GLint x = ar_i32(&r);
  GLint y = ar_i32(&r);
  GLsizei w = ar_i32(&r);
  GLsizei h = ar_i32(&r);

  GLenum prev = glGetError();
  glViewport(x, y, w, h);
  GL_LOG_IF_ERR("h_glViewport: %d,%d %dx%d prev_err=0x%x err=0x%x", x, y, w, h,
                prev, after_err);
#else
  glViewport(ar_i32(&r), ar_i32(&r), ar_i32(&r), ar_i32(&r));
#endif
  (void)D;
}
void h_glScissor(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
#ifdef DEBUG_VERBOSE
  GLint x = ar_i32(&r);
  GLint y = ar_i32(&r);
  GLsizei width = ar_i32(&r);
  GLsizei height = ar_i32(&r);

  GLenum prev = glGetError();
  glScissor(x, y, width, height);
  GL_LOG_IF_ERR("h_glScissor: %d,%d %dx%d prev_err=0x%x err=0x%x", x, y, width,
                height, prev, after_err);
#else
  glScissor(ar_i32(&r), ar_i32(&r), ar_i32(&r), ar_i32(&r));
#endif
  (void)D;
}
void h_glEnable(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glEnable(ar_u32(&r));
  (void)D;
}
void h_glDisable(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
#ifdef DEBUG_VERBOSE
  GLenum cap = ar_u32(&r);

  GLenum prev = glGetError();
  EGLint prev_egl = eglGetError();

  glDisable(cap);

  EGLint new_egl = eglGetError();

  GL_LOG_IF_ERR(
      "h_glDisable: cap=0x%x prev_err=0x%x err=0x%x prev_egl_err=0x%x "
      "new_egl_err=0x%x",
      cap, prev, after_err, prev_egl, new_egl);

#ifdef DEBUG_ABORT_ON_GL_ERROR
  if (new_egl != EGL_SUCCESS)
    abort();
#endif

#else
  glDisable(ar_u32(&r));
#endif
  (void)D;
}

void h_glIsEnabled(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  C->result = glIsEnabled(ar_u32(&r));
  (void)D;
}
void h_glCullFace(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glCullFace(ar_u32(&r));
  (void)D;
}
void h_glFrontFace(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glFrontFace(ar_u32(&r));
  (void)D;
}
void h_glLineWidth(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glLineWidth(ar_f32(&r));
  (void)D;
}
void h_glPolygonOffset(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glPolygonOffset(ar_f32(&r), ar_f32(&r));
  (void)D;
}
void h_glSampleCoverage(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glSampleCoverage(ar_f32(&r), ar_u32(&r));
  (void)D;
}
void h_glHint(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glHint(ar_u32(&r), ar_u32(&r));
  (void)D;
}

/* ── Blend / Depth / Stencil / Clear ────────────────────────────────────── */
void h_glBlendColor(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glBlendColor(ar_f32(&r), ar_f32(&r), ar_f32(&r), ar_f32(&r));
  (void)D;
}
void h_glBlendEquation(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glBlendEquation(ar_u32(&r));
  (void)D;
}
void h_glBlendEquationSeparate(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glBlendEquationSeparate(ar_u32(&r), ar_u32(&r));
  (void)D;
}
void h_glBlendFunc(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glBlendFunc(ar_u32(&r), ar_u32(&r));
  (void)D;
}
void h_glBlendFuncSeparate(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glBlendFuncSeparate(ar_u32(&r), ar_u32(&r), ar_u32(&r), ar_u32(&r));
  (void)D;
}
void h_glDepthFunc(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glDepthFunc(ar_u32(&r));
  (void)D;
}
void h_glDepthMask(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glDepthMask(ar_u32(&r));
  (void)D;
}
void h_glDepthRangef(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glDepthRangef(ar_f32(&r), ar_f32(&r));
  (void)D;
}
void h_glColorMask(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glColorMask(ar_u32(&r), ar_u32(&r), ar_u32(&r), ar_u32(&r));
  (void)D;
}
void h_glStencilFunc(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glStencilFunc(ar_u32(&r), ar_i32(&r), ar_u32(&r));
  (void)D;
}
void h_glStencilFuncSeparate(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glStencilFuncSeparate(ar_u32(&r), ar_u32(&r), ar_i32(&r), ar_u32(&r));
  (void)D;
}
void h_glStencilMask(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glStencilMask(ar_u32(&r));
  (void)D;
}
void h_glStencilMaskSeparate(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glStencilMaskSeparate(ar_u32(&r), ar_u32(&r));
  (void)D;
}
void h_glStencilOp(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glStencilOp(ar_u32(&r), ar_u32(&r), ar_u32(&r));
  (void)D;
}
void h_glStencilOpSeparate(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glStencilOpSeparate(ar_u32(&r), ar_u32(&r), ar_u32(&r), ar_u32(&r));
  (void)D;
}
void h_glClear(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
#ifdef DEBUG_VERBOSE
  GLbitfield mask = ar_u32(&r);
  GLenum prev = glGetError();
  glClear(mask);
  GL_LOG_IF_ERR("h_glClear: mask=0x%x prev_err=0x%x err=0x%x", mask, prev,
                after_err);
#else
  glClear(ar_u32(&r));
#endif
  (void)D;
}
void h_glClearColor(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glClearColor(ar_f32(&r), ar_f32(&r), ar_f32(&r), ar_f32(&r));
  (void)D;
}
void h_glClearDepthf(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glClearDepthf(ar_f32(&r));
  (void)D;
}
void h_glClearStencil(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  glClearStencil(ar_i32(&r));
  (void)D;
}

/* ── Query ───────────────────────────────────────────────────────────────── */
void h_glGetError(BridgeCtrl *C, uint8_t *D)
{
  C->result = glGetError();
  (void)D;
}
void h_glGetBooleanv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  glGetBooleanv(ar_u32(&r), (GLboolean *)dp(C->data_offset));
  C->result = 0;
}
void h_glGetFloatv(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);
  glGetFloatv(ar_u32(&r), (GLfloat *)dp(C->data_offset));
  C->result = 0;
}

void h_glGetIntegerv(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  GLenum pname = ar_u32(&r);
  GLint *out = (GLint *)dp(C->data_offset);

#ifdef DEBUG_VERBOSE
  log_console("h_glGetIntegerv pname=0x%x out=%p", pname, out);
#endif

  glGetIntegerv(pname, out);

#ifdef DEBUG_VERBOSE
  log_console("h_glGetIntegerv result=%d", out[0]);
#endif

  C->result = 0;
}

void h_glGetString(BridgeCtrl *C, uint8_t *D)
{

  AR(r);
  GLenum name = ar_u32(&r);

#ifdef DEBUG_GL_GETSTRING
  log_console("h_glGetString: name=0x%x", name);
#endif

  const GLubyte *s = glGetString(name);

  if (s)
  {
    strncpy((char *)C->result_buf, (const char *)s, BRIDGE_RESULT_SIZE - 1);
    C->result_buf[BRIDGE_RESULT_SIZE - 1] = '\0';
  }
  else
  {
    C->result_buf[0] = '\0';
  }

#ifdef DEBUG_GL_GETSTRING
  log_console("h_glGetString: name=0x%x result=\"%s\"", name, C->result_buf);
#endif

  C->result = 0;
  (void)D;
}

void h_glReadPixels(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  GLint x = ar_i32(&r), y = ar_i32(&r);
  GLsizei w = ar_i32(&r), h = ar_i32(&r);
  GLenum fmt = ar_u32(&r), type = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_glReadPixels: x=%d y=%d w=%d h=%d fmt=0x%04x type=0x%04x "
              "data_size=%zu data_offset=%zu",
              x, y, w, h, fmt, type, C->data_size, C->data_offset);

  size_t bpp = 4;
  if (type == GL_UNSIGNED_SHORT_5_6_5 || type == GL_UNSIGNED_SHORT_4_4_4_4 ||
      type == GL_UNSIGNED_SHORT_5_5_5_1)
    bpp = 2;

  size_t pix_bytes = (size_t)w * (size_t)h * bpp;
  GLint drawfb = -1, readfb = -1, readbuf = -1;
  GLint sample_buffers = -1, samples = -1;
  GLint impl_fmt = -1, impl_type = -1;
  GLint pack_align = -1, row_len = -1, skip_px = -1, skip_rows = -1;
  GLint pbo = -1, pbo_size = -1, pbo_mapped = -1;

  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawfb);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readfb);
  glGetIntegerv(GL_READ_BUFFER, &readbuf);

  glGetIntegerv(GL_SAMPLE_BUFFERS, &sample_buffers);
  glGetIntegerv(GL_SAMPLES, &samples);

  glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT, &impl_fmt);
  glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE, &impl_type);

  glGetIntegerv(GL_PACK_ALIGNMENT, &pack_align);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &row_len);
  glGetIntegerv(GL_PACK_SKIP_PIXELS, &skip_px);
  glGetIntegerv(GL_PACK_SKIP_ROWS, &skip_rows);

  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pbo);
  if (pbo != 0)
  {
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_SIZE, &pbo_size);
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_MAPPED, &pbo_mapped);
  }

  log_console("[READPIXELS DEBUG]\n"
              "  args: x=%d y=%d w=%d h=%d fmt=0x%04x type=0x%04x\n"
              "  FBO: draw=%d read=%d readbuf=0x%x\n"
              "  MSAA: SAMPLE_BUFFERS=%d SAMPLES=%d\n"
              "  IMPL_READ: fmt=0x%04x type=0x%04x\n"
              "  PACK: align=%d rowlen=%d skip_px=%d skip_rows=%d\n"
              "  PBO: id=%d size=%d mapped=%d data_size=%zu data_offset=%zu\n"
              "  pix_bytes=%zu",
              x, y, w, h, fmt, type, drawfb, readfb, readbuf, sample_buffers,
              samples, impl_fmt, impl_type, pack_align, row_len, skip_px,
              skip_rows, pbo, pbo_size, pbo_mapped, C->data_size,
              C->data_offset, pix_bytes);

  if (pbo_mapped == 1)
  {
#ifdef DEBUG_VERBOSE
    log_console("h_glReadPixels: PBO already mapping so unmapping PBO!");
#endif
    glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
  }

  GLint prev_err = glGetError();
#endif

  if (C->data_size == 0)
  {
#ifdef DEBUG_VERBOSE
    log_console(
        "h_glReadPixels: PBO mode - passing offset %zu directly to driver",
        C->data_offset);
#endif
    /* PBO mode: stub set data_size=0 and data_offset = application's byte
     * offset into the PBO.  Pass it directly to the driver; the result goes
     * into the proxy-side PBO and will be mapped by the application via
     * glMapBufferRange — no shared-memory transfer needed. */
    glReadPixels(x, y, w, h, fmt, type, (void *)(uintptr_t)C->data_offset);
  }
  else
  {
#ifdef DEBUG_VERBOSE
    log_console("h_glReadPixels: shared-memory mode - using data_offset=%zu as "
                "CPU-side buffer pointer",
                C->data_offset);
#endif
    /* Normal path: read into shared-memory buffer; stub will copy it back
     * to application memory via bridge_data_read. */
    glReadPixels(x, y, w, h, fmt, type, dp(C->data_offset));
  }

#ifdef DEBUG_VERBOSE
  GL_LOG_IF_ERR("h_glReadPixels: glReadPixels prev_err=0x%04x err=0x%04x",
                prev_err, after_err);
#endif

  C->result = 0;
  (void)D;
}

/* ── Misc ────────────────────────────────────────────────────────────────── */
void h_glFinish(BridgeCtrl *C, uint8_t *D)
{
  glFinish();
  C->result = 0;
  (void)D;
}
void h_glFlush(BridgeCtrl *C, uint8_t *D)
{
  glFlush();
  (void)C;
  (void)D;
}

void h_eglGetError(BridgeCtrl *C, uint8_t *D)
{
  C->result = (uint64_t)eglGetError();
  (void)D;
}

#define H_IS(fn, glfn)                                                         \
  void h_##fn(BridgeCtrl *C, uint8_t *D)                                       \
  {                                                                            \
    AR(r);                                                                     \
    C->result = glfn(ar_u32(&r));                                              \
    (void)D;                                                                   \
  }
H_IS(glIsBuffer, glIsBuffer)
H_IS(glIsFramebuffer, glIsFramebuffer)
H_IS(glIsProgram, glIsProgram)
H_IS(glIsRenderbuffer, glIsRenderbuffer)
H_IS(glIsShader, glIsShader)
H_IS(glIsTexture, glIsTexture)
