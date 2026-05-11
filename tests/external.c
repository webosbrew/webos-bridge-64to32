#define _GNU_SOURCE

#ifdef USE_DESKTOP_GL
#include <GL/glew.h>
#else
#include <GLES3/gl32.h>
#endif

#include <EGL/egl.h>
#include <math.h>
#include <pthread.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#define MINIMP3_IMPLEMENTATION
#include "deps/minimp3_ex.h"

#include "deps/font8x8_basic.h"

#if defined(__aarch64__)
#include "../include/webos-shell.h"
#else
#include "../include/webos-shell-32.h"
#endif

#define AUDIO_BUF_SAMPLES 8192
int16_t buffer[AUDIO_BUF_SAMPLES];

#define KEY_WAYLAND_WEBOS_BACK 412
#define KEY_WAYLAND_WEBOS_RED 398
#define KEY_WAYLAND_WEBOS_GREEN 399
#define KEY_WAYLAND_WEBOS_YELLOW 400
#define KEY_WAYLAND_WEBOS_BLUE 401

// ---------------- Wayland / webOS ----------------
struct wl_display *display;
struct wl_registry *registry;
struct wl_compositor *compositor;
struct wl_seat *seat;
struct wl_keyboard *keyboard;
struct wl_webos_shell *webos_shell;
struct wl_surface *surface;
struct wl_egl_window *egl_window;

struct wl_shell *demo_wl_shell;
struct wl_webos_shell_surface *webos_shell_surface;
struct wl_shell_surface *demo_shell_surface;

// ---------------- EGL ----------------
EGLDisplay eglD;
EGLContext eglC;
EGLSurface eglS;
EGLConfig eglCfg;

// ---------------- GL ----------------
GLuint prog;
GLuint vbo;
GLuint ebo;
GLuint vao;
GLuint tex;
GLint mvp_loc;

// Text
GLuint text_tex;
GLuint text_vbo;
GLuint text_vao;
float start_time;
int text_w;
int text_h;

pthread_t music_thread_id;
static volatile int running = 1;

// ---------------- geometry ----------------
static const float cube[] = {
    // front
    -0.5f, -0.5f, 0.5f, 0, 0, 0.5f, -0.5f, 0.5f, 1, 0, 0.5f, 0.5f, 0.5f, 1, 1,
    -0.5f, 0.5f, 0.5f, 0, 1,

    // back
    -0.5f, -0.5f, -0.5f, 1, 0, -0.5f, 0.5f, -0.5f, 1, 1, 0.5f, 0.5f, -0.5f, 0,
    1, 0.5f, -0.5f, -0.5f, 0, 0,

    // left
    -0.5f, -0.5f, -0.5f, 0, 0, -0.5f, -0.5f, 0.5f, 1, 0, -0.5f, 0.5f, 0.5f, 1,
    1, -0.5f, 0.5f, -0.5f, 0, 1,

    // right
    0.5f, -0.5f, -0.5f, 1, 0, 0.5f, 0.5f, -0.5f, 1, 1, 0.5f, 0.5f, 0.5f, 0, 1,
    0.5f, -0.5f, 0.5f, 0, 0,

    // top
    -0.5f, 0.5f, -0.5f, 0, 0, -0.5f, 0.5f, 0.5f, 0, 1, 0.5f, 0.5f, 0.5f, 1, 1,
    0.5f, 0.5f, -0.5f, 1, 0,

    // bottom
    -0.5f, -0.5f, -0.5f, 1, 1, 0.5f, -0.5f, -0.5f, 0, 1, 0.5f, -0.5f, 0.5f, 0,
    0, -0.5f, -0.5f, 0.5f, 1, 0};

static const uint16_t idx[] = {0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,
                               8,  9,  10, 10, 11, 8,  12, 13, 14, 14, 15, 12,
                               16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20};

// ---------------- shaders ----------------
#ifdef USE_DESKTOP_GL
static const char *vs = "#version 330 core\n"
#else
static const char *vs = "#version 300 es\n"
#endif
                        "layout(location=0) in vec3 aPos;\n"
                        "layout(location=1) in vec2 aUV;\n"
                        "uniform mat4 mvp;\n"
                        "out vec2 uv;\n"
                        "void main() {\n"
                        "    uv = aUV;\n"
                        "    gl_Position = mvp * vec4(aPos, 1.0);\n"
                        "}\n";

#ifdef USE_DESKTOP_GL
static const char *fs = "#version 330 core\n"
#else
static const char *fs = "#version 300 es\n"
                        "precision mediump float;\n"
#endif
                        "in vec2 uv;\n"
                        "uniform sampler2D tex;\n"
                        "out vec4 c;\n"
                        "void main() {\n"
                        "    c = texture(tex, uv);\n"
                        "}\n";

// ---------------- time ----------------
static float now_sec()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9f;
}

// ---------------- loop playback ----------------
static void *music_thread(void *arg)
{
  mp3dec_ex_t dec;
  int err;

  if (mp3dec_ex_open(&dec, "loop.mp3", MP3D_SEEK_TO_SAMPLE))
  {
    fprintf(stderr, "failed to open mp3\n");
    return NULL;
  }

  pa_sample_spec ss = {.format = PA_SAMPLE_S16LE,
                       .rate = dec.info.hz,
                       .channels = dec.info.channels};

  pa_simple *s = pa_simple_new(NULL, "gles_demo", PA_STREAM_PLAYBACK, NULL,
                               "music", &ss, NULL, NULL, &err);

  if (!s)
  {
    fprintf(stderr, "pulse error: %s\n", pa_strerror(err));
    mp3dec_ex_close(&dec);
    return NULL;
  }

  int16_t buffer[8192];

  while (running)
  {
    size_t samples = mp3dec_ex_read(&dec, buffer, 8192);

    if (samples == 0)
    {
      pa_simple_drain(s, &err);
      mp3dec_ex_seek(&dec, 0);
      continue;
    }

    if (pa_simple_write(s, buffer, samples * sizeof(int16_t), &err) < 0)
    {
      fprintf(stderr, "pulse write error: %s\n", pa_strerror(err));
      break;
    }
  }

  pa_simple_free(s);
  mp3dec_ex_close(&dec);
  return NULL;
}

void stop_music(void)
{
  running = 0;
  pthread_join(music_thread_id, NULL);
}

void wl_keyboard_handle_key_webos(void *data, struct wl_keyboard *keyboard,
                                  uint32_t serial, uint32_t time, uint32_t key,
                                  uint32_t state)
{
  fprintf(stderr, "key event: code=%u state=%u\n", key, state);

  if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
  {
    if (key == KEY_WAYLAND_WEBOS_BACK)
    {
      fprintf(stderr, "BACK key pressed — exiting\n");
      stop_music();
      exit(0);
    }
  }
}

static void wl_keyboard_handle_keymap(void *data, struct wl_keyboard *kbd,
                                      uint32_t format, int fd, uint32_t size)
{
}

static void wl_keyboard_handle_enter(void *data, struct wl_keyboard *kbd,
                                     uint32_t serial,
                                     struct wl_surface *surface,
                                     struct wl_array *keys)
{
}

static void wl_keyboard_handle_leave(void *data, struct wl_keyboard *kbd,
                                     uint32_t serial,
                                     struct wl_surface *surface)
{
}

static void wl_keyboard_handle_modifiers(void *data, struct wl_keyboard *kbd,
                                         uint32_t serial,
                                         uint32_t mods_depressed,
                                         uint32_t mods_latched,
                                         uint32_t mods_locked, uint32_t group)
{
}

static void wl_keyboard_handle_repeat_info(void *data, struct wl_keyboard *kbd,
                                           int32_t rate, int32_t delay)
{
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = wl_keyboard_handle_keymap,
    .enter = wl_keyboard_handle_enter,
    .leave = wl_keyboard_handle_leave,
    .key = wl_keyboard_handle_key_webos,
    .modifiers = wl_keyboard_handle_modifiers,
    .repeat_info = wl_keyboard_handle_repeat_info,
};

static void wl_seat_handle_capabilities_webos(void *data, struct wl_seat *seat,
                                              unsigned caps)
{
  if (caps & WL_SEAT_CAPABILITY_KEYBOARD)
  {
    keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
  }
}

const struct wl_seat_listener webos_seat_listener = {
    wl_seat_handle_capabilities_webos,
    NULL,
};

// ---------------- Wayland registry ----------------
static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t id, const char *interface,
                            uint32_t version)
{
  if (strcmp(interface, "wl_compositor") == 0)
    compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);

  if (strcmp(interface, "wl_webos_shell") == 0)
    webos_shell = wl_registry_bind(registry, id, &wl_webos_shell_interface, 1);

  if (strcmp(interface, "wl_shell") == 0)
    demo_wl_shell = wl_registry_bind(registry, id, &wl_shell_interface, 1);

  if (strcmp(interface, "wl_seat") == 0)
    seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
}

static const struct wl_registry_listener registry_listener = {registry_global,
                                                              NULL};

// ---------------- GL helpers ----------------
static GLuint compile(GLenum type, const char *src)
{
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, 0);
  glCompileShader(s);
  return s;
}

static GLuint link_program(GLuint v, GLuint f)
{
  GLuint p = glCreateProgram();
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  return p;
}

// ---------------- webOS surface ----------------
static void create_surface(int w, int h)
{
  const char *appId = getenv("APPID");
  if (!appId || !*appId)
    appId = "org.webosbrew.bridge-64to32";

  const char *displayId = getenv("DISPLAY_ID");
  if (!displayId || !*displayId)
    displayId = "0";

  surface = wl_compositor_create_surface(compositor);

  demo_shell_surface = wl_shell_get_shell_surface(demo_wl_shell, surface);
  wl_shell_surface_set_toplevel(demo_shell_surface);

  webos_shell_surface = wl_webos_shell_get_shell_surface(webos_shell, surface);

  wl_webos_shell_surface_set_property(webos_shell_surface, "appId", appId);
  wl_webos_shell_surface_set_property(webos_shell_surface, "title",
                                      "gles_proxy");
  wl_webos_shell_surface_set_property(webos_shell_surface, "displayAffinity",
                                      displayId);
  wl_webos_shell_surface_set_property(webos_shell_surface,
                                      "_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");

  egl_window = wl_egl_window_create(surface, w, h);
}

// ---------------- EGL ----------------
static void init_egl()
{
  eglD = eglGetDisplay((EGLNativeDisplayType)display);
  eglInitialize(eglD, NULL, NULL);

  EGLint cfg[] = {EGL_RENDERABLE_TYPE,
#ifdef USE_DESKTOP_GL
                  EGL_OPENGL_BIT,
#else
                  EGL_OPENGL_ES3_BIT,
#endif
                  EGL_SURFACE_TYPE,
                  EGL_WINDOW_BIT,
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

  EGLint n;
  eglChooseConfig(eglD, cfg, &eglCfg, 1, &n);

#ifdef USE_DESKTOP_GL
  eglBindAPI(EGL_OPENGL_API);
  eglC = eglCreateContext(eglD, eglCfg, EGL_NO_CONTEXT, NULL);
#else
  eglBindAPI(EGL_OPENGL_ES_API);
  EGLint ctx[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  eglC = eglCreateContext(eglD, eglCfg, EGL_NO_CONTEXT, ctx);
#endif

  eglS = eglCreateWindowSurface(eglD, eglCfg, (EGLNativeWindowType)egl_window,
                                NULL);

  eglMakeCurrent(eglD, eglS, eglS, eglC);

#ifdef USE_DESKTOP_GL
  glewInit();
#endif
}

static void make_text_texture()
{
  const char *msg = "If you can see this cube and hear sound then "
                    "webOS aarch64 test was successful!";

  const int char_w = 8;
  const int char_h = 8;
  const int scale = 2;

  int len = strlen(msg);
  text_w = len * char_w * scale;
  text_h = char_h * scale;

  int W = text_w;
  int H = text_h;

  uint32_t *pixels = calloc(W * H, sizeof(uint32_t));

  for (int i = 0; i < len; i++)
  {
    unsigned char c = (unsigned char)msg[i];

    for (int row = 0; row < 8; row++)
    {
      uint8_t bits = font8x8_basic[c][row];

      for (int col = 0; col < 8; col++)
      {
        if (bits & (1 << col))
        {
          for (int sy = 0; sy < scale; sy++)
          {
            for (int sx = 0; sx < scale; sx++)
            {
              int px = i * char_w * scale + col * scale + sx;
              int py = (H - 1) - (row * scale + sy);

              pixels[py * W + px] = 0xffffffff;
            }
          }
        }
      }
    }
  }

  glGenTextures(1, &text_tex);
  glBindTexture(GL_TEXTURE_2D, text_tex);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  free(pixels);
}

// ---------------- GL init ----------------
static void init_gl()
{
  GLuint v = compile(GL_VERTEX_SHADER, vs);
  GLuint f = compile(GL_FRAGMENT_SHADER, fs);
  prog = link_program(v, f);

  glUseProgram(prog);
  glEnable(GL_DEPTH_TEST);

  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);

  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);

  glGenBuffers(1, &ebo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  mvp_loc = glGetUniformLocation(prog, "mvp");

  uint32_t pixels[4] = {0xffffffff, 0xff0000ff, 0xff00ff00, 0xffff0000};

  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               pixels);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

#ifdef __aarch64__
  make_text_texture();
#endif
  start_time = now_sec();
}

// ---------------- render loop ----------------
static void loop()
{
  float px = 0.0f;
  float py = 0.0f;
  float vx = 0.45f;
  float vy = 0.38f;

  float last = now_sec();

  while (running)
  {
    wl_display_dispatch_pending(display);

    float t = now_sec();
    float dt = t - last;
    last = t;

    // movement
    px += vx * dt;
    py += vy * dt;

    // visible world extents at z=4
    float z = 4.0f;
    float aspect = 1920.0f / 1080.0f;
    float fov = 65.0f * (3.14159265f / 180.0f);

    float half_h = tanf(fov * 0.5f) * z;
    float half_w = half_h * aspect;

    // cube half-size
    float r = 0.5f;

    // bounce against actual screen edges
    if (px > half_w - r)
    {
      px = half_w - r;
      vx = -vx;
    }
    if (px < -half_w + r)
    {
      px = -half_w + r;
      vx = -vx;
    }

    if (py > half_h - r)
    {
      py = half_h - r;
      vy = -vy;
    }
    if (py < -half_h + r)
    {
      py = -half_h + r;
      vy = -vy;
    }

    float cx = cosf(t * 0.9f);
    float sx = sinf(t * 0.9f);

    float cy = cosf(t * 1.2f);
    float sy = sinf(t * 1.2f);

    float near = 0.1f;
    float far = 100.0f;
    float f = 1.0f / tanf(fov * 0.5f);

    // projection (column-major)
    float proj[16] = {f / aspect,
                      0,
                      0,
                      0,
                      0,
                      f,
                      0,
                      0,
                      0,
                      0,
                      (far + near) / (near - far),
                      -1,
                      0,
                      0,
                      (2.0f * far * near) / (near - far),
                      0};

    // model (rotation + translation)
    float model[16] = {cy,  sx * sy, cx * sy, 0, 0,  cx, -sx,   0,
                       -sy, sx * cy, cx * cy, 0, px, py, -4.0f, 1};

    float mvp[16] = {0};

    // mvp = proj * model
    for (int col = 0; col < 4; col++)
    {
      for (int row = 0; row < 4; row++)
      {
        for (int k = 0; k < 4; k++)
        {
          mvp[col * 4 + row] += proj[k * 4 + row] * model[col * 4 + k];
        }
      }
    }

    glViewport(0, 0, 1920, 1080);

    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(prog);
    glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, mvp);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, 0);

    if ((t - start_time) < 15.0f)
    {
      glDisable(GL_DEPTH_TEST);

      float screen_w = 1920.0f;
      float screen_h = 1080.0f;

      float pixel_w = 1400.0f; // desired banner width in pixels
      float pixel_h = pixel_w * ((float)text_h / (float)text_w);

      float ndc_w = (pixel_w / screen_w) * 2.0f;
      float ndc_h = (pixel_h / screen_h) * 2.0f;

      float left = -ndc_w * 0.5f;
      float right = ndc_w * 0.5f;
      float top = 0.9f;
      float bottom = top - ndc_h;

      float quad[] = {left,  top,    0, 0, 1, right, top,    0, 1, 1,
                      right, bottom, 0, 1, 0, left,  bottom, 0, 0, 0};

      static const uint16_t qidx[] = {0, 1, 2, 2, 3, 0};

      GLuint qvbo, qebo, qvao;
      glGenVertexArrays(1, &qvao);
      glBindVertexArray(qvao);

      glGenBuffers(1, &qvbo);
      glBindBuffer(GL_ARRAY_BUFFER, qvbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

      glGenBuffers(1, &qebo);
      glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, qebo);
      glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(qidx), qidx, GL_STATIC_DRAW);

      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                            (void *)0);
      glEnableVertexAttribArray(0);

      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                            (void *)(3 * sizeof(float)));
      glEnableVertexAttribArray(1);

      float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

      glUniformMatrix4fv(mvp_loc, 1, GL_FALSE, ident);

      glBindTexture(GL_TEXTURE_2D, text_tex);
      glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

      glDeleteBuffers(1, &qvbo);
      glDeleteBuffers(1, &qebo);
      glDeleteVertexArrays(1, &qvao);

      glEnable(GL_DEPTH_TEST);
    }

    eglSwapBuffers(eglD, eglS);
  }
}

// ---------------- main ----------------
int main()
{
  srand(time(NULL));

  if (getenv("XDG_RUNTIME_DIR") == NULL)
    setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
  if (getenv("EGL_PLATFORM") == NULL)
    setenv("EGL_PLATFORM", "wayland", 0);
  if (getenv("XDG_RUNTIME_DIR") == NULL)
    setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
  if (getenv("XKB_CONFIG_ROOT") == NULL)
    setenv("XKB_CONFIG_ROOT", "/usr/share/X11/xkb", 0);
  if (getenv("WAYLAND_DISPLAY") == NULL)
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);

  display = wl_display_connect(NULL);
  if (!display)
  {
    printf("failed wl_display_connect\n");
    return 1;
  }

  registry = wl_display_get_registry(display);
  wl_registry_add_listener(registry, &registry_listener, NULL);
  wl_display_roundtrip(display);

  if (!compositor || !webos_shell)
  {
    printf("missing compositor or webos shell\n");
    return 1;
  }

  if (seat)
    wl_seat_add_listener(seat, &webos_seat_listener, NULL);
  wl_display_roundtrip(display);

  create_surface(1920, 1080);
  init_egl();
  init_gl();
  glUniform1i(glGetUniformLocation(prog, "tex"), 0);

  pthread_create(&music_thread_id, NULL, music_thread, NULL);

  printf("[webOS GLES bridge test running]\n");

  loop();

  return 0;
}
