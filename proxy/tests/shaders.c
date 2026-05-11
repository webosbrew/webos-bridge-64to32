#include "../proxy.h"

#include <GLES2/gl2.h>

void reg_global(void *data, struct wl_registry *reg, uint32_t name,
                const char *iface, uint32_t ver)
{
  (void)data;

  if (strcmp(iface, "wl_compositor") == 0)
  {
    proxy_wl_compositor = wl_registry_bind(reg, name, &wl_compositor_interface,
                                           ver < 3 ? ver : 3);
  }
  else if (strcmp(iface, "wl_shell") == 0)
  {
    proxy_wl_shell = wl_registry_bind(reg, name, &wl_shell_interface, 1);
  }
  else if (strcmp(iface, "wl_webos_shell") == 0)
  {
    proxy_wl_webos_shell =
        wl_registry_bind(reg, name, &wl_webos_shell_interface, 1);
  }
}

void reg_global_remove(void *data, struct wl_registry *reg, uint32_t name)
{
  (void)data;
  (void)reg;
  (void)name;
}

static const struct wl_registry_listener reg_listener = {
    .global = reg_global,
    .global_remove = reg_global_remove,
};

int demo_init_egl(void)
{
  static const EGLint cfg_attrib_stub_state[] = {EGL_SURFACE_TYPE,
                                                 EGL_WINDOW_BIT,
                                                 EGL_RED_SIZE,
                                                 8,
                                                 EGL_GREEN_SIZE,
                                                 8,
                                                 EGL_BLUE_SIZE,
                                                 8,
                                                 EGL_ALPHA_SIZE,
                                                 8,
                                                 EGL_RENDERABLE_TYPE,
                                                 EGL_OPENGL_ES2_BIT,
                                                 EGL_NONE};

  static const EGLint ctx_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

  EGLint ncfg = 0;
  EGLConfig cfg;

  /* use slot 1 for the demo/primary display */
  egl_displays[DEFAULT_EGL_IDX].handle =
      eglGetDisplay((EGLNativeDisplayType)proxy_wl_display);
  if (egl_displays[DEFAULT_EGL_IDX].handle == EGL_NO_DISPLAY)
  {
    log_error("[demo] eglGetDisplay failed");
    return -1;
  }

  if (!eglInitialize(egl_displays[DEFAULT_EGL_IDX].handle,
                     &egl_displays[DEFAULT_EGL_IDX].major,
                     &egl_displays[DEFAULT_EGL_IDX].minor))
  {
    log_error("[demo] eglInitialize failed");
    return -1;
  }

  egl_displays[DEFAULT_EGL_IDX].initialized = true;

  if (!eglChooseConfig(egl_displays[DEFAULT_EGL_IDX].handle,
                       cfg_attrib_stub_state, &cfg, 1, &ncfg) ||
      ncfg < 1)
  {
    log_error("[demo] eglChooseConfig failed");
    return -1;
  }

  if (!eglBindAPI(EGL_OPENGL_ES_API))
  {
    log_error("[demo] eglBindAPI failed");
    return -1;
  }

  egl_configs[DEFAULT_EGL_IDX].handle = cfg;

  egl_contexts[DEFAULT_EGL_IDX].handle = eglCreateContext(
      egl_displays[DEFAULT_EGL_IDX].handle, egl_configs[DEFAULT_EGL_IDX].handle,
      EGL_NO_CONTEXT, ctx_attribs);
  if (egl_contexts[DEFAULT_EGL_IDX].handle == EGL_NO_CONTEXT)
  {
    log_error("[demo] eglCreateContext failed");
    return -1;
  }

  return 0;
}

void create_window(void)
{
  log_console("[demo] starting demo_window()");

  g_surfs[DEFAULT_WL_IDX] = wl_compositor_create_surface(proxy_wl_compositor);
  if (!g_surfs[DEFAULT_WL_IDX])
  {
    log_error("[demo] wl_compositor_create_surface failed");
    return;
  }

  log_console("[demo] storing wl_surface as DEFAULT_WL_IDX");

  g_shell_surfs[DEFAULT_WL_IDX] =
      wl_shell_get_shell_surface(proxy_wl_shell, g_surfs[DEFAULT_WL_IDX]);
  if (!g_shell_surfs[DEFAULT_WL_IDX])
  {
    log_error("[demo] wl_shell_get_shell_surface failed");
    return;
  }

  wl_shell_surface_set_toplevel(g_shell_surfs[DEFAULT_WL_IDX]);

  g_webos_shell_surfaces[DEFAULT_WL_IDX] = wl_webos_shell_get_shell_surface(
      proxy_wl_webos_shell, g_surfs[DEFAULT_WL_IDX]);
  if (!g_webos_shell_surfaces[DEFAULT_WL_IDX])
  {
    log_error("[demo] wl_webos_shell_get_shell_surface failed");
    return;
  }

  const char *appId = getenv("APPID");
  if (!appId || !*appId)
    appId = "com.example.gles2proxy";
  const char *displayId = getenv("DISPLAY_ID");
  if (!displayId || !*displayId)
    displayId = "0";

  wl_webos_shell_surface_set_property(g_webos_shell_surfaces[DEFAULT_WL_IDX],
                                      "appId", appId);
  wl_webos_shell_surface_set_property(g_webos_shell_surfaces[DEFAULT_WL_IDX],
                                      "title", "gles_proxy");
  wl_webos_shell_surface_set_property(g_webos_shell_surfaces[DEFAULT_WL_IDX],
                                      "displayAffinity", displayId);

  int width = 1920, height = 1080;

  if (demo_init_egl() != 0)
  {
    log_error("[demo] demo_init_egl failed");
    return;
  }

  proxy_wl_egl_windows[DEFAULT_WL_EGL_IDX] =
      wl_egl_window_create(g_surfs[DEFAULT_WL_IDX], width, height);
  if (!proxy_wl_egl_windows[DEFAULT_WL_EGL_IDX])
  {
    log_error("[demo] wl_egl_window_create failed");
    return;
  }

  egl_surfaces[DEFAULT_EGL_IDX].handle = eglCreateWindowSurface(
      egl_displays[DEFAULT_EGL_IDX].handle, egl_configs[DEFAULT_EGL_IDX].handle,
      (EGLNativeWindowType)proxy_wl_egl_windows[DEFAULT_WL_EGL_IDX], NULL);
  if (egl_surfaces[DEFAULT_EGL_IDX].handle == EGL_NO_SURFACE)
  {
    log_error("[demo] eglCreateWindowSurface failed");
    return;
  }

  if (!eglMakeCurrent(egl_displays[DEFAULT_EGL_IDX].handle,
                      egl_surfaces[DEFAULT_EGL_IDX].handle,
                      egl_surfaces[DEFAULT_EGL_IDX].handle,
                      egl_contexts[DEFAULT_EGL_IDX].handle))
  {
    log_error("[demo] eglMakeCurrent failed");
    return;
  }

  log_console("[demo] appId: %s displayId: %s", appId, displayId);

  log_console("demo] demo_window() : wl globals (disp=%p compositor=%p "
              "shell=%p webos shell=%p webOS shell surf=%p)",
              proxy_wl_display, proxy_wl_compositor, proxy_wl_shell,
              proxy_wl_webos_shell, g_webos_shell_surfaces[DEFAULT_WL_IDX]);

  log_console(
      "demo] demo_window() : egl globals (displ=%p egl surf=%p context=%p)",
      egl_displays[DEFAULT_EGL_IDX].handle,
      egl_surfaces[DEFAULT_EGL_IDX].handle,
      egl_contexts[DEFAULT_EGL_IDX].handle);
}

// test shaders as one string
void shaderTestSimple()
{
#ifdef DEBUG
  dump_ctx("shaderTest");
#endif

#ifdef DEBUG_SHADERS
  log_console("shaderTest() BEGIN");
#endif

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

  const char *v =
      "#define VERTEX\n"
      "#define PARAMETER_UNIFORM\n"
      "#define _HAS_ORIGINALASPECT_UNIFORMS\n"
      "#define _HAS_FRAMETIME_UNIFORMS\n"
      "#define _HAS_SENSOR_UNIFORM\n"
      "#extension GL_OES_standard_derivatives : enable\n"
      "#ifdef GL_ES\n"
      "  #ifdef GL_FRAGMENT_PRECISION_HIGH\n"
      "    precision highp float;\n"
      "  #else\n"
      "    precision mediump float;\n"
      "  #endif\n"
      "#else\n"
      "  precision mediump float;\n"
      "#endif\n"
      "attribute vec2 TexCoord; attribute vec2 VertexCoord; attribute vec4 \n"
      "Color; uniform mat4 MVPMatrix; varying vec2 tex_coord;\n"
      "void main() { gl_Position = MVPMatrix * vec4(VertexCoord, 0.0, 1.0); \n"
      "tex_coord = TexCoord; }\n";

  const char *f = "#define FRAGMENT\n"
                  "#define PARAMETER_UNIFORM\n"
                  "#define _HAS_ORIGINALASPECT_UNIFORMS\n"
                  "#define _HAS_FRAMETIME_UNIFORMS\n"
                  "#define _HAS_SENSOR_UNIFORMS\n"
                  "#extension GL_OES_standard_derivatives : enable\n"
                  "#ifdef GL_ES\n"
                  "  #ifdef GL_FRAGMENT_PRECISION_HIGH\n"
                  "    precision highp float;\n"
                  "  #else\n"
                  "    precision mediump float;\n"
                  "  #endif\n"
                  "#else\n"
                  "  precision mediump float;\n"
                  "#endif\n"
                  "uniform sampler2D Texture; varying vec2 tex_coord; \n"
                  "void main() { gl_FragColor = vec4(texture2D(Texture, \n"
                  "tex_coord).rgb, 1.0); }\n";

  glShaderSource(vs, 1, &v, NULL);
  glShaderSource(fs, 1, &f, NULL);

  glCompileShader(vs);

#ifdef DEBUG_SHADERS
  GL_LOG_IF_ERR("after glCompileShader(vs) err=0x%04x", after_err);
#endif

  glCompileShader(fs);

#ifdef DEBUG_SHADERS
  GLuint err = glGetError();
  log_console("after glCompileShader(fs) err=0x%04x", err);
#endif

  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);

#ifdef DEBUG_SHADERS
  log_console("TEST LINK BEGIN");
#endif

  glLinkProgram(p);

#ifdef DEBUG_SHADERS
  err = glGetError();
  log_console("after glLinkProgram err=0x%04x", err);

  GLint linked = 0;
  GLint proglog = 0;
  GLint validated = 0;

  glGetProgramiv(p, GL_LINK_STATUS, &linked);
  glGetProgramiv(p, GL_INFO_LOG_LENGTH, &proglog);
  glGetProgramiv(p, GL_VALIDATE_STATUS, &validated);

  log_console("PROGRAM state:"
              " linked=%d"
              " validated=%d"
              " loglen=%d",
              linked, validated, proglog);

  if (proglog > 1)
  {
    char *log = calloc(1, proglog + 1);

    glGetProgramInfoLog(p, proglog, NULL, log);

    log_console("PROGRAM LOG BEGIN");
    log_console("%s", log);
    log_console("PROGRAM LOG END");

    free(log);
  }

  log_console("TEST LINK END");
#endif
}

// test as per retroarch
void shaderTestComplete(void)
{
#ifdef DEBUG_SHADERS
  log_console("================================================");
  log_console("shaderTest COMPLETE BEGIN");
  log_console("================================================");
#endif

#ifdef DEBUG_SHADERS
  GLenum err = glGetError();
  log_console("initial err = 0x%04x", err);
#endif

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);

#ifdef DEBUG_SHADERS
  log_console("glCreateShader(VERTEX)   -> %u", vs);
  log_console("glCreateShader(FRAGMENT) -> %u", fs);
#endif

  static const char *vs_source[4] = {
      "#version 100\n",

      "#define VERTEX\n"
      "#define PARAMETER_UNIFORM\n"
      "#define _HAS_ORIGINALASPECT_UNIFORMS\n"
      "#define _HAS_FRAMETIME_UNIFORMS\n"
      "#define _HAS_SENSOR_UNIFORMS\n",

      "",

      "#extension GL_OES_standard_derivatives : enable\n"
      "#ifdef GL_ES\n"
      "  #ifdef GL_FRAGMENT_PRECISION_HIGH\n"
      "    precision highp float;\n"
      "  #else\n"
      "    precision mediump float;\n"
      "  #endif\n"
      "#else\n"
      "  precision mediump float;\n"
      "#endif\n"
      "attribute vec2 TexCoord;\n"
      "attribute vec2 VertexCoord;\n"
      "attribute vec4 Color;\n"
      "uniform mat4 MVPMatrix;\n"
      "varying vec2 tex_coord;\n"
      "void main()\n"
      "{\n"
      "    gl_Position = MVPMatrix * vec4(VertexCoord, 0.0, 1.0);\n"
      "    tex_coord = TexCoord;\n"
      "}\n"};

  static const char *fs_source[4] = {
      "#version 100\n",

      "#define FRAGMENT\n"
      "#define PARAMETER_UNIFORM\n"
      "#define _HAS_ORIGINALASPECT_UNIFORMS\n"
      "#define _HAS_FRAMETIME_UNIFORMS\n"
      "#define _HAS_SENSOR_UNIFORMS\n",

      "",

      "#extension GL_OES_standard_derivatives : enable\n"
      "#ifdef GL_ES\n"
      "  #ifdef GL_FRAGMENT_PRECISION_HIGH\n"
      "    precision highp float;\n"
      "  #else\n"
      "    precision mediump float;\n"
      "  #endif\n"
      "#else\n"
      "  precision mediump float;\n"
      "#endif\n"
      "uniform sampler2D Texture;\n"
      "varying vec2 tex_coord;\n"
      "void main()\n"
      "{\n"
      "    gl_FragColor = vec4(texture2D(Texture, tex_coord).rgb, 1.0);\n"
      "}\n"};

#ifdef DEBUG_SHADERS
  log_console("------------------------------------------------");
  log_console("VERTEX SHADER SOURCE STRINGS");
  log_console("------------------------------------------------");

  for (int i = 0; i < 4; i++)
  {
    log_console("vs_source[%d] ptr=%p len=%u", i, vs_source[i],
                vs_source[i] ? (unsigned)strlen(vs_source[i]) : 0);

    if (vs_source[i])
      log_console("%s", vs_source[i]);
  }

  log_console("------------------------------------------------");
  log_console("FRAGMENT SHADER SOURCE STRINGS");
  log_console("------------------------------------------------");

  for (int i = 0; i < 4; i++)
  {
    log_console("fs_source[%d] ptr=%p len=%u", i, fs_source[i],
                fs_source[i] ? (unsigned)strlen(fs_source[i]) : 0);

    if (fs_source[i])
      log_console("%s", fs_source[i]);
  }

  log_console("------------------------------------------------");
  log_console("glShaderSource(VS)");
  log_console("------------------------------------------------");
#endif

  glShaderSource(vs, 4, vs_source, NULL);

#ifdef DEBUG_SHADERS
  GL_LOG_IF_ERR("after glShaderSource(VS) err=0x%04x", after_err);

  log_console("------------------------------------------------");
  log_console("glCompileShader(VS)");
  log_console("------------------------------------------------");
#endif

  glCompileShader(vs);

#ifdef DEBUG_SHADERS
  err = glGetError();
  log_console("after glCompileShader(VS) err=0x%04x", err);
#endif

  GLint status = 0;
  GLint loglen = 0;
  GLint shader_type = 0;
  GLint delete_status = 0;
  GLint source_len = 0;

  glGetShaderiv(vs, GL_SHADER_TYPE, &shader_type);
  glGetShaderiv(vs, GL_DELETE_STATUS, &delete_status);
  glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
  glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &loglen);
  glGetShaderiv(vs, GL_SHADER_SOURCE_LENGTH, &source_len);

#ifdef DEBUG_SHADERS
  log_console("VS state:"
              " type=0x%x"
              " delete=%d"
              " compile=%d"
              " loglen=%d"
              " source_len=%d",
              shader_type, delete_status, status, loglen, source_len);

  if (loglen > 1)
  {
    char *log = calloc(1, loglen + 1);

    glGetShaderInfoLog(vs, loglen, NULL, log);

    log_console("VS LOG BEGIN");
    log_console("%s", log);
    log_console("VS LOG END");

    free(log);
  }

  log_console("------------------------------------------------");
  log_console("glShaderSource(FS)");
  log_console("------------------------------------------------");
#endif

  glShaderSource(fs, 4, fs_source, NULL);

#ifdef DEBUG_SHADERS
  err = glGetError();
  log_console("after glShaderSource(FS) err=0x%04x", err);

  log_console("------------------------------------------------");
  log_console("glCompileShader(FS)");
  log_console("------------------------------------------------");
#endif

  glCompileShader(fs);

#ifdef DEBUG_SHADERS
  err = glGetError();
  log_console("after glCompileShader(FS) err=0x%04x", err);
#endif

  status = 0;
  loglen = 0;
  shader_type = 0;
  delete_status = 0;
  source_len = 0;

  glGetShaderiv(fs, GL_SHADER_TYPE, &shader_type);
  glGetShaderiv(fs, GL_DELETE_STATUS, &delete_status);
  glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
  glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &loglen);
  glGetShaderiv(fs, GL_SHADER_SOURCE_LENGTH, &source_len);

#ifdef DEBUG_SHADERS
  log_console("FS state:"
              " type=0x%x"
              " delete=%d"
              " compile=%d"
              " loglen=%d"
              " source_len=%d",
              shader_type, delete_status, status, loglen, source_len);

  if (loglen > 1)
  {
    char *log = calloc(1, loglen + 1);

    glGetShaderInfoLog(fs, loglen, NULL, log);

    log_console("FS LOG BEGIN");
    log_console("%s", log);
    log_console("FS LOG END");

    free(log);
  }

  log_console("------------------------------------------------");
  log_console("glCreateProgram()");
  log_console("------------------------------------------------");
#endif

  GLuint prog = glCreateProgram();

  log_console("glCreateProgram -> %u", prog);

#ifdef DEBUG_SHADERS
  err = glGetError();
  log_console("after glCreateProgram err=0x%04x", err);
#endif

  GLint attached = 0;

  glGetProgramiv(prog, GL_ATTACHED_SHADERS, &attached);

#ifdef DEBUG_SHADERS
  log_console("before attach: attached shaders=%d", attached);

  log_console("------------------------------------------------");
  log_console("glAttachShader(prog=%u, vs=%u)", prog, vs);
  log_console("------------------------------------------------");
#endif

  glAttachShader(prog, vs);

#ifdef DEBUG_SHADERS
  err = glGetError();
  log_console("after attach VS err=0x%04x", err);
#endif

  glGetProgramiv(prog, GL_ATTACHED_SHADERS, &attached);

#ifdef DEBUG_SHADERS
  log_console("after VS attach: attached shaders=%d", attached);

  log_console("------------------------------------------------");
  log_console("glAttachShader(prog=%u, fs=%u)", prog, fs);
  log_console("------------------------------------------------");
#endif

  glAttachShader(prog, fs);

#ifdef DEBUG_SHADERS
  err = glGetError();
  log_console("after attach FS err=0x%04x", err);
#endif

  glGetProgramiv(prog, GL_ATTACHED_SHADERS, &attached);

#ifdef DEBUG_SHADERS
  log_console("after FS attach: attached shaders=%d", attached);
#endif

  GLint validate = 0;

  glGetProgramiv(prog, GL_DELETE_STATUS, &validate);

#ifdef DEBUG_SHADERS
  log_console("program delete status=%d", validate);

  log_console("================================================");
  log_console("TEST LINK BEGIN");
  log_console("================================================");

  log_console("calling glLinkProgram(%u)...", prog);
#endif

  glLinkProgram(prog);

#ifdef DEBUG_SHADERS
  log_console("returned from glLinkProgram(%u)", prog);

  err = glGetError();
  log_console("after glLinkProgram err=0x%04x", err);

  GLint linked = 0;
  GLint proglog = 0;
  GLint validated = 0;

  glGetProgramiv(prog, GL_LINK_STATUS, &linked);
  glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &proglog);
  glGetProgramiv(prog, GL_VALIDATE_STATUS, &validated);

  log_console("PROGRAM state:"
              " linked=%d"
              " validated=%d"
              " loglen=%d",
              linked, validated, proglog);

  if (proglog > 1)
  {
    char *log = calloc(1, proglog + 1);

    glGetProgramInfoLog(prog, proglog, NULL, log);

    log_console("PROGRAM LOG BEGIN");
    log_console("%s", log);
    log_console("PROGRAM LOG END");

    free(log);
  }

  log_console("================================================");
  log_console("shaderTest COMPLETE END");
  log_console("================================================");
#endif
}

void drawDemo()
{
  int width = 1920, height = 1080;

  glViewport(0, 0, width, height);
  glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  eglSwapBuffers(egl_displays[DEFAULT_EGL_IDX].handle,
                 egl_surfaces[DEFAULT_EGL_IDX].handle);

  wl_surface_commit(g_surfs[DEFAULT_WL_IDX]);
  wl_display_flush(proxy_wl_display);

  log_console("[demo] drew red window");
}

void shaderWarmUp()
{
  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  fprintf(stderr, "err=%x\n", glGetError());

  glCompileShader(vs);
  fprintf(stderr, "err=%x\n", glGetError());

  GLuint p = glCreateProgram();
  glAttachShader(p, vs);

  fprintf(stderr, "err=%x\n", glGetError());

  glLinkProgram(p);
  fprintf(stderr, "err=%x\n", glGetError());

  glDeleteShader(vs);
  fprintf(stderr, "err=%x\n", glGetError());

  glDeleteProgram(p);
  fprintf(stderr, "err=%x\n", glGetError());
}
