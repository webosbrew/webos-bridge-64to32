#define _GNU_SOURCE

#include <GLES3/gl32.h>
#include <EGL/egl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#if defined(__aarch64__)
#include "../include/webos-shell.h"
#else
#include "../include/webos-shell-32.h"
#endif

#define KEY_WAYLAND_WEBOS_BACK 412

static struct wl_display *g_display;
static struct wl_compositor *g_compositor;
static struct wl_shell *g_wl_shell;
static struct wl_webos_shell *g_webos_shell;
static struct wl_seat *g_seat;
static struct wl_keyboard *g_keyboard;
static struct wl_surface *g_surface;
static struct wl_egl_window *g_egl_window;
static struct wl_shell_surface *g_shell_surface;
static struct wl_webos_shell_surface *g_webos_ss;
static volatile int g_running = 1;
static volatile int g_fullscreen = 0;

static EGLDisplay g_egl_dpy = EGL_NO_DISPLAY;
static EGLConfig g_egl_cfg = 0;
static EGLSurface g_egl_surf = EGL_NO_SURFACE;
static EGLContext g_ctx1 = EGL_NO_CONTEXT;
static EGLContext g_ctx2 = EGL_NO_CONTEXT;

#define UBO_SIZE (64u * 1024u * 1024u)
static GLuint g_ubo = 0;
static void *g_ubo_ptr = NULL;
static GLuint g_ubo_offset = 0;
static GLint g_ubo_align = 256;

static GLuint g_efb_tex = 0;
static GLuint g_copy_tex = 0;
static GLuint g_fbo_efb = 0;
static GLuint g_fbo_copy = 0;
static GLuint g_quad_vbo = 0;
static GLuint g_quad_vao = 0;
static GLuint g_geom_vbo = 0;
static GLuint g_geom_ebo = 0;
static GLuint g_geom_vao = 0;
static GLuint g_prog_efb = 0;
static GLuint g_prog_copy = 0;
static GLuint g_prog_blit = 0;
static GLuint g_sampler = 0;

/* ctx1-local objects — VAOs and programs are NOT shared between contexts */
static GLuint g_ctx1_quad_vao = 0;
static GLuint g_ctx1_prog_blit = 0;
static GLuint g_ctx1_sampler = 0;

#define EFB_W 640
#define EFB_H 528
#define COPY_W 640
#define COPY_H 456

static long render_frame_count = 0;

/* ── timing ─────────────────────────────────────────────────────────────── */
static float now_sec(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (float)ts.tv_sec + (float)ts.tv_nsec * 1e-9f;
}

/* ── GL error ────────────────────────────────────────────────────────────── */
static void check_gl(const char *where)
{
  GLenum e = glGetError();
  if (e != GL_NO_ERROR)
  {
    fprintf(stderr, "[GL ERROR] %s: 0x%04x\n", where, e);
    abort();
  }
}

/* ── shaders ─────────────────────────────────────────────────────────────── */
static GLuint compile_shader(GLenum type, const char *src)
{
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char buf[4096];
    glGetShaderInfoLog(s, sizeof(buf), NULL, buf);
    fprintf(stderr, "[SHADER compile error]\n%s\n", buf);
  }
  return s;
}

static GLuint link_prog(const char *vs_src, const char *fs_src)
{
  GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
  GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    char buf[4096];
    glGetProgramInfoLog(p, sizeof(buf), NULL, buf);
    fprintf(stderr, "[SHADER link error]\n%s\n", buf);
  }
  glDeleteShader(vs);
  glDeleteShader(fs);
  return p;
}

/*
 * EFB geometry shader:
 *   attrib 0 = vec2 pos  (stride 16, offset 0)
 *   attrib 8 = vec2 uv   (stride 16, offset 8)
 *   UBO binding=1: projection_matrix (mat4) + model_matrix (mat4)
 */
static const char *VS_EFB = "#version 310 es\n"
                            "layout(location=0) in vec2 aPos;\n"
                            "layout(location=8) in vec2 aUV;\n"
                            "layout(std140, binding=1) uniform VSBlock {\n"
                            "    mat4 projection_matrix;\n"
                            "    mat4 model_matrix;\n"
                            "};\n"
                            "out vec2 vUV;\n"
                            "void main() {\n"
                            "    vUV = aUV;\n"
                            "    gl_Position = projection_matrix * "
                            "model_matrix * vec4(aPos, 0.0, 1.0);\n"
                            "}\n";

static const char *FS_EFB =
    "#version 310 es\n"
    "precision mediump float;\n"
    "precision mediump sampler2DArray;\n"
    "uniform sampler2DArray uTex;\n"
    "in vec2 vUV;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vec4(1.0, 0.0, 0.5, 1.0);\n" /* solid magenta — ignore
                                                     UBO/tex for now */
    "}\n";

/* fullscreen-triangle blit: attrib 0 = vec2 NDC pos */
static const char *VS_BLIT = "#version 310 es\n"
                             "layout(location=0) in vec2 aPos;\n"
                             "out vec2 vUV;\n"
                             "void main() {\n"
                             "    vUV = aPos * 0.5 + 0.5;\n"
                             "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
                             "}\n";

static const char *FS_COPY = "#version 310 es\n"
                             "precision mediump float;\n"
                             "precision mediump sampler2DArray;\n"
                             "uniform sampler2DArray uSrc;\n"
                             "in vec2 vUV;\n"
                             "out vec4 fragColor;\n"
                             "void main() {\n"
                             "    fragColor = texture(uSrc, vec3(vUV, 0.0));\n"
                             "}\n";

static const char *FS_SCREEN =
    "#version 310 es\n"
    "precision mediump float;\n"
    "precision mediump sampler2DArray;\n"
    "uniform sampler2DArray uSrc;\n"
    "in vec2 vUV;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(uSrc, vec3(vUV, 0.0));\n"
    "}\n";

/* ── Wayland ─────────────────────────────────────────────────────────────── */
static void kbd_key(void *d, struct wl_keyboard *k, uint32_t ser, uint32_t time,
                    uint32_t key, uint32_t state)
{
  (void)d;
  (void)k;
  (void)ser;
  (void)time;
  if (state && key == KEY_WAYLAND_WEBOS_BACK)
    g_running = 0;
}
static void kbd_keymap(void *d, struct wl_keyboard *k, uint32_t f, int fd,
                       uint32_t s)
{
  (void)d;
  (void)k;
  (void)f;
  (void)s;
  close(fd);
}
static void kbd_enter(void *d, struct wl_keyboard *k, uint32_t s,
                      struct wl_surface *su, struct wl_array *a)
{
  (void)d;
  (void)k;
  (void)s;
  (void)su;
  (void)a;
}
static void kbd_leave(void *d, struct wl_keyboard *k, uint32_t s,
                      struct wl_surface *su)
{
  (void)d;
  (void)k;
  (void)s;
  (void)su;
}
static void kbd_mods(void *d, struct wl_keyboard *k, uint32_t a, uint32_t b,
                     uint32_t c, uint32_t e, uint32_t f)
{
  (void)d;
  (void)k;
  (void)a;
  (void)b;
  (void)c;
  (void)e;
  (void)f;
}
static void kbd_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t dl)
{
  (void)d;
  (void)k;
  (void)r;
  (void)dl;
}
static const struct wl_keyboard_listener g_kbd_listener = {
    kbd_keymap, kbd_enter, kbd_leave, kbd_key, kbd_mods, kbd_repeat};

static void seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
  (void)d;
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g_keyboard)
  {
    g_keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(g_keyboard, &g_kbd_listener, NULL);
  }
}
static void seat_name(void *d, struct wl_seat *s, const char *n)
{
  (void)d;
  (void)s;
  (void)n;
}
static const struct wl_seat_listener g_seat_listener = {seat_caps, seat_name};

static void wss_state(void *d, struct wl_webos_shell_surface *s, uint32_t v)
{
  (void)d;
  (void)s;
  fprintf(stderr, "wss state=%u\n", v);
  if (v == 3)
    g_fullscreen = 1;
}
static void wss_pos(void *d, struct wl_webos_shell_surface *s, int32_t x,
                    int32_t y)
{
  (void)d;
  (void)s;
  (void)x;
  (void)y;
}
static void wss_close(void *d, struct wl_webos_shell_surface *s)
{
  (void)d;
  (void)s;
  g_running = 0;
}
static void wss_exposed(void *d, struct wl_webos_shell_surface *s,
                        struct wl_array *a)
{
  (void)d;
  (void)s;
  (void)a;
}
static void wss_state_about(void *d, struct wl_webos_shell_surface *s,
                            uint32_t v)
{
  (void)d;
  (void)s;
  (void)v;
}
static const struct wl_webos_shell_surface_listener g_wss_listener = {
    wss_state, wss_pos, wss_close, wss_exposed, wss_state_about};

static void reg_global(void *d, struct wl_registry *reg, uint32_t id,
                       const char *iface, uint32_t ver)
{
  (void)d;
  if (!strcmp(iface, "wl_compositor"))
    g_compositor = wl_registry_bind(reg, id, &wl_compositor_interface, 1);
  else if (!strcmp(iface, "wl_shell"))
    g_wl_shell = wl_registry_bind(reg, id, &wl_shell_interface, 1);
  else if (!strcmp(iface, "wl_webos_shell"))
    g_webos_shell = wl_registry_bind(reg, id, &wl_webos_shell_interface, 1);
  else if (!strcmp(iface, "wl_seat"))
  {
    g_seat = wl_registry_bind(reg, id, &wl_seat_interface, ver < 4 ? ver : 4);
    wl_seat_add_listener(g_seat, &g_seat_listener, NULL);
  }
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t n)
{
  (void)d;
  (void)r;
  (void)n;
}
static const struct wl_registry_listener g_reg_listener = {reg_global,
                                                           reg_remove};

/* ── EGL ─────────────────────────────────────────────────────────────────── */
static void init_egl(void)
{
  g_egl_dpy = eglGetDisplay((EGLNativeDisplayType)g_display);
  eglInitialize(g_egl_dpy, NULL, NULL);
  eglBindAPI(EGL_OPENGL_ES_API);

  EGLint cfg_attr[] = {EGL_SURFACE_TYPE,
                       EGL_WINDOW_BIT,
                       EGL_RENDERABLE_TYPE,
                       EGL_OPENGL_ES3_BIT,
                       EGL_RED_SIZE,
                       8,
                       EGL_GREEN_SIZE,
                       8,
                       EGL_BLUE_SIZE,
                       8,
                       EGL_ALPHA_SIZE,
                       8,
                       EGL_DEPTH_SIZE,
                       24,
                       EGL_NONE};
  EGLint n = 0;
  eglChooseConfig(g_egl_dpy, cfg_attr, &g_egl_cfg, 1, &n);
  if (!n)
  {
    fprintf(stderr, "eglChooseConfig failed\n");
    exit(1);
  }

  g_egl_surf = eglCreateWindowSurface(g_egl_dpy, g_egl_cfg,
                                      (EGLNativeWindowType)g_egl_window, NULL);

  EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  g_ctx1 = eglCreateContext(g_egl_dpy, g_egl_cfg, EGL_NO_CONTEXT, ctx_attr);
  g_ctx2 = eglCreateContext(g_egl_dpy, g_egl_cfg, g_ctx1, ctx_attr);

  if (g_ctx1 == EGL_NO_CONTEXT || g_ctx2 == EGL_NO_CONTEXT)
  {
    fprintf(stderr, "eglCreateContext failed err=0x%x\n", eglGetError());
    exit(1);
  }
  fprintf(stderr, "EGL: ctx1=%p ctx2=%p surf=%p\n", (void *)g_ctx1,
          (void *)g_ctx2, (void *)g_egl_surf);
}

/* ── UBO streaming allocator ─────────────────────────────────────────────── */
static GLintptr ubo_alloc(GLsizeiptr size)
{
  GLintptr off =
      ((GLintptr)g_ubo_offset + g_ubo_align - 1) & ~(GLintptr)(g_ubo_align - 1);
  if (off + size > (GLintptr)UBO_SIZE)
    off = 0;
  g_ubo_offset = (GLuint)(off + size);
  return off;
}

/* ── mat4 helpers ────────────────────────────────────────────────────────── */
static void mat4_identity(float *m)
{
  memset(m, 0, 64);
  m[0] = m[5] = m[10] = m[15] = 1.f;
}

static void mat4_ortho(float *m, float l, float r, float b, float t, float n,
                       float f)
{
  memset(m, 0, 64);
  m[0] = 2.f / (r - l);
  m[5] = 2.f / (t - b);
  m[10] = -2.f / (f - n);
  m[12] = -(r + l) / (r - l);
  m[13] = -(t + b) / (t - b);
  m[14] = -(f + n) / (f - n);
  m[15] = 1.f;
}

/* ── GL resources ────────────────────────────────────────────────────────── */
static void init_gl_resources(void)
{
  fprintf(stderr, "GL_VERSION:  %s\n", glGetString(GL_VERSION));
  fprintf(stderr, "GL_RENDERER: %s\n", glGetString(GL_RENDERER));

  /* query alignment before use */
  glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &g_ubo_align);
  fprintf(stderr, "UBO align=%d\n", g_ubo_align);

  /* ── programs ── */
  g_prog_efb = link_prog(VS_EFB, FS_EFB);
  g_prog_copy = link_prog(VS_BLIT, FS_COPY);
  g_prog_blit = link_prog(VS_BLIT, FS_SCREEN);
  check_gl("link programs");

  glUseProgram(g_prog_copy);
  glUniform1i(glGetUniformLocation(g_prog_copy, "uSrc"), 0);
  glUseProgram(g_prog_blit);
  glUniform1i(glGetUniformLocation(g_prog_blit, "uSrc"), 0);
  glUseProgram(g_prog_efb);
  glUniform1i(glGetUniformLocation(g_prog_efb, "uTex"), 0);
  glUseProgram(0);
  check_gl("uniform setup");

  /* ── persistent UBO (64 MB, write-only, no flush-explicit) ── */
  glGenBuffers(1, &g_ubo);
  glBindBuffer(GL_UNIFORM_BUFFER, g_ubo);
  glBufferData(GL_UNIFORM_BUFFER, UBO_SIZE, NULL, GL_DYNAMIC_DRAW);
  g_ubo_ptr = glMapBufferRange(GL_UNIFORM_BUFFER, 0, UBO_SIZE,
                               GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT |
                                   GL_MAP_UNSYNCHRONIZED_BIT);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  if (!g_ubo_ptr)
  {
    fprintf(stderr, "glMapBufferRange failed 0x%x\n", glGetError());
    exit(1);
  }
  fprintf(stderr, "UBO=%u mapped=%p\n", g_ubo, g_ubo_ptr);
  check_gl("UBO map");

  /* ── EFB colour texture — GL_TEXTURE_2D_ARRAY, 1 layer ── */
  glGenTextures(1, &g_efb_tex);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_efb_tex);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, EFB_W, EFB_H, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  check_gl("efb_tex");

  /* ── EFB copy texture ── */
  glGenTextures(1, &g_copy_tex);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_copy_tex);
  glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, COPY_W, COPY_H, 1);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  check_gl("copy_tex");

  /* ── FBO EFB (render target) ── */
  glGenFramebuffers(1, &g_fbo_efb);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo_efb);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, g_efb_tex, 0,
                            0);
  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  fprintf(stderr, "fbo_efb=%u status=0x%x\n", g_fbo_efb, st);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  check_gl("fbo_efb");

  /* ── FBO copy (EFB copy output) ── */
  glGenFramebuffers(1, &g_fbo_copy);
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo_copy);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, g_copy_tex, 0,
                            0);
  st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  fprintf(stderr, "fbo_copy=%u status=0x%x\n", g_fbo_copy, st);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  check_gl("fbo_copy");

  /* ── fullscreen-triangle VAO (VAO 1) — attrib 0, stride=0 ── */
  static const float tri[] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
  glGenBuffers(1, &g_quad_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
  check_gl("quad vbo");

  glGenVertexArrays(1, &g_quad_vao);
  glBindVertexArray(g_quad_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  check_gl("quad vao");

  /* ── geometry VAO (VAO 4 equivalent)
   *    attrib 0 = vec2 pos  stride=16 offset=0
   *    attrib 8 = vec2 uv   stride=16 offset=8
   *    Uses glDrawElementsBaseVertex
   * ── */
  /* layout: x, y, u, v  (all float32, 16 bytes per vertex) */
  static const float geom[] = {
      -0.5f, -0.5f, 0.f, 0.f, 0.5f,  -0.5f, 1.f, 0.f,
      0.5f,  0.5f,  1.f, 1.f, -0.5f, 0.5f,  0.f, 1.f,
  };
  static const uint16_t geom_idx[] = {0, 1, 2, 2, 3, 0};

  glGenBuffers(1, &g_geom_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, g_geom_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(geom), geom, GL_STATIC_DRAW);
  check_gl("geom vbo");

  glGenBuffers(1, &g_geom_ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_geom_ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(geom_idx), geom_idx,
               GL_STATIC_DRAW);
  check_gl("geom ebo");

  glGenVertexArrays(1, &g_geom_vao);
  glBindVertexArray(g_geom_vao);
  /* EBO must be bound WHILE the VAO is bound so it is captured */
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_geom_ebo);
  glBindBuffer(GL_ARRAY_BUFFER, g_geom_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void *)0);
  glEnableVertexAttribArray(8);
  glVertexAttribPointer(8, 2, GL_FLOAT, GL_FALSE, 16, (void *)8);
  /* verify EBO capture */
  GLint captured_ebo = 0;
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &captured_ebo);
  fprintf(stderr, "geom_vao=%u  captured EBO=%d (expect %u)\n", g_geom_vao,
          captured_ebo, g_geom_ebo);
  glBindVertexArray(0);
  /* unbind EBO AFTER VAO so we don't accidentally detach it */
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  check_gl("geom vao");

  /* ── sampler ── */
  glGenSamplers(1, &g_sampler);
  glSamplerParameteri(g_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glSamplerParameteri(g_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glSamplerParameteri(g_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glSamplerParameteri(g_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  check_gl("sampler");

  fprintf(stderr,
          "resources: UBO=%u efb_tex=%u copy_tex=%u "
          "fbo_efb=%u fbo_copy=%u "
          "prog_efb=%u prog_copy=%u prog_blit=%u "
          "quad_vao=%u geom_vao=%u\n",
          g_ubo, g_efb_tex, g_copy_tex, g_fbo_efb, g_fbo_copy, g_prog_efb,
          g_prog_copy, g_prog_blit, g_quad_vao, g_geom_vao);
}

/* ── render ──────────────────────────────────────────────────────────────── */
static void render_frame(float t)
{
  /* ── Phase 1: EFB render — ctx2 ── */
  eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_ctx2);

  /* Allocate and write UBO sub-range via persistent map */
  GLintptr ubo_off = ubo_alloc(128); /* proj(64) + model(64) */

  float proj[16], model[16];
  mat4_ortho(proj, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f);
  mat4_identity(model);
  float angle = t * 1.5f;
  model[0] = cosf(angle);
  model[1] = sinf(angle);
  model[4] = -sinf(angle);
  model[5] = cosf(angle);
  model[12] = sinf(t * 0.7f) * 0.5f;
  model[13] = cosf(t * 0.5f) * 0.3f;

  memcpy((uint8_t *)g_ubo_ptr + ubo_off, proj, 64);
  memcpy((uint8_t *)g_ubo_ptr + ubo_off + 64, model, 64);

  /* Must unmap before the GPU can read from it as a UBO */
  glBindBuffer(GL_UNIFORM_BUFFER, g_ubo);
  glUnmapBuffer(GL_UNIFORM_BUFFER);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  g_ubo_ptr = NULL;
  check_gl("UBO unmap before draw");

  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo_efb);
  check_gl("bind fbo_efb");
  glViewport(0, 0, EFB_W, EFB_H);
  glScissor(0, 0, EFB_W, EFB_H);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glClearColor(0.1f, 0.1f, 0.3f, 1.f);
  glClear(GL_COLOR_BUFFER_BIT);
  check_gl("EFB clear");

  glUseProgram(g_prog_efb);
  glBindBufferRange(GL_UNIFORM_BUFFER, 1, g_ubo, ubo_off, 128);
  check_gl("BindBufferRange");

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_efb_tex);
  glBindSampler(0, g_sampler);

  glBindVertexArray(g_geom_vao);
  check_gl("pre-draw");

  /* Verify EBO is captured */
  GLint ebo_chk = 0;
  glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ebo_chk);
  glDrawElementsBaseVertex(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (void *)0, 0);
  check_gl("EFB draw");

  /* Remap for next frame */
  glBindBuffer(GL_UNIFORM_BUFFER, g_ubo);
  g_ubo_ptr = glMapBufferRange(GL_UNIFORM_BUFFER, 0, UBO_SIZE,
                               GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  check_gl("UBO remap");

  {
    GLubyte px[4] = {0};
    glReadPixels(EFB_W / 2, EFB_H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    fprintf(stderr, "[EFB]    %u %u %u %u\n", px[0], px[1], px[2], px[3]);
  }

  /* ── Phase 2: EFB copy — ctx2, blit efb_tex → copy_tex ── */
  glBindFramebuffer(GL_FRAMEBUFFER, g_fbo_copy);
  glViewport(0, 0, COPY_W, COPY_H);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(g_prog_copy);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_efb_tex);
  glBindSampler(0, g_sampler);
  glBindVertexArray(g_quad_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  check_gl("EFB copy");

  {
    GLubyte px[4] = {0};
    glReadPixels(COPY_W / 2, COPY_H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    fprintf(stderr, "[COPY]   %u %u %u %u\n", px[0], px[1], px[2], px[3]);
  }

  /* ── Phase 3: screen blit — ctx1 ── */
  eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_ctx1);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, 1920, 1080);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  glUseProgram(g_ctx1_prog_blit);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D_ARRAY, g_copy_tex);
  glBindSampler(0, g_ctx1_sampler);
  glBindVertexArray(g_ctx1_quad_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  check_gl("screen blit");

  {
    GLubyte px[4] = {0};
    glReadPixels(960, 540, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    fprintf(stderr, "[SCREEN] %u %u %u %u\n", px[0], px[1], px[2], px[3]);
  }

  eglSwapBuffers(g_egl_dpy, g_egl_surf);

  render_frame_count++;

  if (render_frame_count > 25)
    exit(0);
  // sleep(5);
  // abort();
}

/* ── main ────────────────────────────────────────────────────────────────── */
int main(void)
{
  if (!getenv("XDG_RUNTIME_DIR"))
    setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
  if (!getenv("WAYLAND_DISPLAY"))
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
  if (!getenv("EGL_PLATFORM"))
    setenv("EGL_PLATFORM", "wayland", 0);

  g_display = wl_display_connect(NULL);
  if (!g_display)
  {
    fprintf(stderr, "wl_display_connect failed\n");
    return 1;
  }

  struct wl_registry *reg = wl_display_get_registry(g_display);
  wl_registry_add_listener(reg, &g_reg_listener, NULL);
  wl_display_roundtrip(g_display);

  if (!g_compositor || !g_webos_shell)
  {
    fprintf(stderr, "missing compositor/webos_shell\n");
    return 1;
  }

  g_surface = wl_compositor_create_surface(g_compositor);
  g_shell_surface = wl_shell_get_shell_surface(g_wl_shell, g_surface);
  wl_shell_surface_set_toplevel(g_shell_surface);
  g_webos_ss = wl_webos_shell_get_shell_surface(g_webos_shell, g_surface);
  wl_webos_shell_surface_add_listener(g_webos_ss, &g_wss_listener, NULL);

  const char *appId = getenv("APPID");
  if (!appId || !*appId)
    appId = "org.webosbrew.bridge-64to32";
  const char *dispId = getenv("DISPLAY_ID");
  if (!dispId || !*dispId)
    dispId = "0";
  wl_webos_shell_surface_set_property(g_webos_ss, "appId", appId);
  wl_webos_shell_surface_set_property(g_webos_ss, "title", "ubo_test");
  wl_webos_shell_surface_set_property(g_webos_ss, "displayAffinity", dispId);

  g_egl_window = wl_egl_window_create(g_surface, 1920, 1080);

  init_egl();
  eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_ctx2);
  init_gl_resources();

  /* init ctx1-local objects (VAOs/programs not shared) */
  eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_ctx1);
  {
    g_ctx1_prog_blit = link_prog(VS_BLIT, FS_SCREEN);
    glUseProgram(g_ctx1_prog_blit);
    glUniform1i(glGetUniformLocation(g_ctx1_prog_blit, "uSrc"), 0);
    glUseProgram(0);

    glGenSamplers(1, &g_ctx1_sampler);
    glSamplerParameteri(g_ctx1_sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(g_ctx1_sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glSamplerParameteri(g_ctx1_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(g_ctx1_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    /* quad VBO is shared (buffer object), just need a new VAO */
    glGenVertexArrays(1, &g_ctx1_quad_vao);
    glBindVertexArray(g_ctx1_quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_quad_vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void *)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    check_gl("ctx1 resource init");
    fprintf(stderr, "ctx1: prog_blit=%u quad_vao=%u sampler=%u\n",
            g_ctx1_prog_blit, g_ctx1_quad_vao, g_ctx1_sampler);
  }
  eglMakeCurrent(g_egl_dpy, g_egl_surf, g_egl_surf, g_ctx2);

  /* wait for fullscreen */
  for (int i = 0; i < 100 && !g_fullscreen; i++)
  {
    wl_display_dispatch_pending(g_display);
    wl_display_roundtrip(g_display);
    struct timespec ts = {0, 50000000};
    nanosleep(&ts, NULL);
  }
  fprintf(stderr, "starting render loop (fullscreen=%d)\n", g_fullscreen);

  float start = now_sec();
  int frame = 0;
  while (g_running)
  {
    wl_display_dispatch_pending(g_display);
    render_frame(now_sec() - start);
    wl_display_flush(g_display);
    ++frame;
    if (frame % 120 == 0)
      fprintf(stderr, "frame %d t=%.1f\n", frame, now_sec() - start);
  }

  eglMakeCurrent(g_egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(g_egl_dpy, g_ctx2);
  eglDestroyContext(g_egl_dpy, g_ctx1);
  eglDestroySurface(g_egl_dpy, g_egl_surf);
  eglTerminate(g_egl_dpy);
  wl_display_disconnect(g_display);
  return 0;
}
