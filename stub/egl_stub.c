/*
 * egl_stub.c  —  aarch64 EGL stub
 *
 * Implements every EGL entry point.  All calls are forwarded to the
 * armv7a proxy via the same shared-memory bridge used by gles2_stub.c.
 *
 * Handle mapping
 * ──────────────
 * EGLDisplay / EGLContext / EGLSurface / EGLConfig are opaque pointer-sized
 * values.  On the 64-bit stub side we represent them as 1-based integer
 * indices cast to the appropriate pointer type:
 *
 *   (EGLDisplay)(uintptr_t)1  -  the first display slot in the proxy table
 *   (EGLDisplay)0             -  EGL_NO_DISPLAY
 *
 * The proxy maintains four fixed-size tables (indexed 1..MAX) that map
 * each index to a real 32-bit pointer.
 *
 */

#include <GLES3/gl32.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "../tools/egl_generated.h"
#include "../tools/gles2_generated.h"

#define LOG_PREFIX "[egl_stub]"
#include "../bridge/shared_util.h"
#include "bridge_core.h"
#include "gles_bridge_protocol.h"
#include "gles_util_stub.h"

/* ── Helpers ─────────────────────────────────────────────────────────────── */

/* Convert an opaque EGL handle to the uint32 index we send over the wire. */
#define H2I(h) ((uint32_t)(uintptr_t)(h))
/* Convert an index returned by the proxy back to an opaque handle type. */
#define I2H(T, i) ((T)(uintptr_t)(uint32_t)(i))

/*
 * Size of a null-terminated EGLint attrib list in bytes, INCLUDING the
 * EGL_NONE terminator.  Returns 0 if list == NULL (proxy interprets as NULL).
 */
static uint32_t attrib_list_bytes(const EGLint *list)
{
  if (!list)
    return 0;
  const EGLint *p = list;
  while (*p != EGL_NONE)
    p += 2;
  return (uint32_t)((p - list) + 1) * sizeof(EGLint);
}

static void setup_egl(GLBridgeOpcode op)
{
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = (uint32_t)op;
  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Error
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
  BRIDGE_BEGIN();
  setup_egl(OP_eglGetError);
  BRIDGE_CTRL()->args_len = 0;
  return (EGLint)BRIDGE_SEND_CALL();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Initialisation / termination
 * ═══════════════════════════════════════════════════════════════════════════
 */
EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplayEXT(
    EGLenum platform, void *native, const EGLint *attrib_list)
{
#ifdef DEBUG
  log_console(
      "[eglGetPlatformDisplayEXT] platform=0x%x native=%p (u64=0x%016llx)",
      platform, native, (unsigned long long)(uint64_t)(uintptr_t)native);
#endif

#ifndef HAVE_OWN_WAYLAND_EGL
  // client has passed a 64-bit pointer, which is useless here
  return EGL_NO_DISPLAY;
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_eglGetPlatformDisplayEXT;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, platform);
  aw_u64(&W, (uint64_t)(uintptr_t)native);
  C->args_len = W.pos;

  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;

#ifdef DEBUG
  log_console("[eglGetPlatformDisplayEXT] sending opcode=%u", C->opcode);
#endif

  uint32_t idx = (uint32_t)BRIDGE_SEND_CALL();

#ifdef DEBUG
  log_console("[eglGetPlatformDisplayEXT] received idx=%u handle=%p", idx,
              I2H(EGLDisplay, idx));
#endif
  return I2H(EGLDisplay, idx);
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id)
{
  // fallback for clients that call eglGetDisplay themselves with
  // EGL_DEFAULT_DISPLAY e.g. query extensions
  static struct wl_display *last_wl_display = NULL;
  struct wl_display *wl_dpy = (struct wl_display *)display_id;

  if (!wl_dpy || display_id == EGL_DEFAULT_DISPLAY)
  {
    wl_dpy = last_wl_display;
    if (!last_wl_display)
    {
      log_console("[eglGetDisplay] ERROR: no last wl_display");
      return EGL_NO_DISPLAY;
    }
    if (display_id == EGL_DEFAULT_DISPLAY)
      log_console("[eglGetDisplay] received EGL_DEFAULT_DISPLAY, using last "
                  "wl_display %p",
                  last_wl_display);
  }

  // store last active for the above fallback case
  last_wl_display = wl_dpy;

  int wl_fd = wl_display_get_fd(wl_dpy);

  /* Probe the current ID watermark — highest ID client has allocated */
  struct wl_callback *probe = wl_display_sync(wl_dpy);
  uint32_t watermark =
      wl_proxy_get_id((struct wl_proxy *)probe) + 32; /* +8 safety buffer */
  wl_callback_destroy(probe);

#ifdef DEBUG
  log_console("[eglGetDisplay] wl_fd=%d watermark=%u", wl_fd, watermark);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglGetDisplay);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, (uint32_t)wl_fd);
  aw_u32(&W, watermark);
  C->args_len = W.pos;

  uint64_t fake = BRIDGE_SEND_CALL();
  log_console("[eglGetDisplay] handle=0x%llx", (unsigned long long)fake);
  return I2H(EGLDisplay, fake);
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major,
                                            EGLint *minor)
{
#ifdef DEBUG
  log_console("[eglInitialize] dpy=%p", dpy);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglInitialize);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  C->args_len = W.pos;

#ifdef DEBUG
  log_console("[eglInitialize] sending opcode=%u", C->opcode);
#endif

  EGLBoolean ok = (EGLBoolean)BRIDGE_SEND_CALL();

#ifdef DEBUG
  log_console("[eglInitialize] returned ok=%d", ok);
#endif

  /* Proxy packs [major, minor] into result_buf */
  if (major)
  {
    *major = ((EGLint *)C->result_buf)[0];
#ifdef DEBUG
    log_console("[eglInitialize] major=%d", *major);
#endif
  }
  if (minor)
  {
    *minor = ((EGLint *)C->result_buf)[1];
#ifdef DEBUG
    log_console("[eglInitialize] minor=%d", *minor);
#endif
  }

  return ok;
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglTerminate);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
  BRIDGE_BEGIN();
  setup_egl(OP_eglReleaseThread);
  BRIDGE_CTRL()->args_len = 0;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Query
 * ═══════════════════════════════════════════════════════════════════════════
 */
EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
#ifdef DEBUG_VERBOSE
  log_console("[eglQueryString] dpy=%p name=%d", dpy, name);
#endif

  static char cache[EGL_BRIDGE_MAX_DISPLAYS][8][BRIDGE_RESULT_SIZE];
  static int valid[EGL_BRIDGE_MAX_DISPLAYS][8];

  uint32_t di = H2I(dpy) & (EGL_BRIDGE_MAX_DISPLAYS - 1);
  int ni = name & 0x7;

  /* If cached AND non-empty - return it */
  if (valid[di][ni] && cache[di][ni][0] != '\0')
  {
#ifdef DEBUG_VERBOSE
    log_console("[eglQueryString] cache hit di=%u ni=%d -> \"%s\"", di, ni,
                cache[di][ni]);
#endif
    return cache[di][ni];
  }

  /* Otherwise: force a real query */
#ifdef DEBUG_VERBOSE
  log_console("[eglQueryString] cache miss di=%u ni=%d", di, ni);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglQueryString);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_i32(&W, name);
  C->args_len = W.pos;

#ifdef DEBUG_VERBOSE
  log_console("[eglQueryString] sending opcode=%u", C->opcode);
#endif

  BRIDGE_SEND_CALL();

  const char *src = (const char *)BRIDGE_CTRL()->result_buf;

  /* If proxy returned NULL or empty - DO NOT CACHE */
  if (!src || src[0] == '\0')
  {
    log_console(
        "[eglQueryString] WARNING: proxy returned empty string, NOT caching");
    /* Return empty string but allow future re-query */
    cache[di][ni][0] = '\0';
    valid[di][ni] = 0;
    return cache[di][ni];
  }

  /* Cache valid non-empty result */
  strncpy(cache[di][ni], src, BRIDGE_RESULT_SIZE - 1);
  cache[di][ni][BRIDGE_RESULT_SIZE - 1] = '\0';
  valid[di][ni] = 1;

#ifdef DEBUG_VERBOSE
  log_console("[eglQueryString] received=\"%s\"", cache[di][ni]);
#endif
  return cache[di][ni];
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Config selection
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                            EGLint config_size,
                                            EGLint *num_config)
{
#ifdef DEBUG
  log_console("[eglGetConfigs] dpy=%p idx=%u config_size=%d", (void *)dpy,
              H2I(dpy), config_size);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_eglGetConfigs;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_i32(&W, config_size);
  C->args_len = W.pos;

  /* Proxy writes config_size uint32_t indices into data region */
  uint32_t buf_sz = (uint32_t)config_size * sizeof(uint32_t);
  uint32_t out = configs ? bridge_data_write(configs, buf_sz) : 0;

  C->data_offset = out;
  C->data_size = buf_sz;
  C->data2_offset = 0;
  C->data2_size = 0;

#ifdef DEBUG
  log_console("[eglGetConfigs] sending opcode=%u data_offset=%u data_size=%u",
              C->opcode, C->data_offset, C->data_size);
#endif

  uint64_t r = BRIDGE_SEND_CALL();
  EGLBoolean ok = (EGLBoolean)(r >> 32);      /* high 32 bits */
  EGLint returned = (EGLint)(r & 0xFFFFFFFF); /* low 32 bits  */

#ifdef DEBUG
  log_console("[eglGetConfigs] returned ok=%d num=%d", ok, returned);
#endif

  if (num_config)
    *num_config = returned;

  if (configs && ok)
  {
    uint32_t tmp[EGL_BRIDGE_MAX_CONFIGS];
    int cnt = returned < config_size ? returned : config_size;

    bridge_data_read(tmp, out, (size_t)cnt * sizeof(uint32_t));

    for (int i = 0; i < cnt; i++)
    {
      configs[i] = I2H(EGLConfig, tmp[i]);
#ifdef DEBUG
      log_console("[eglGetConfigs] cfg[%d] index=%u handle=%p", i, tmp[i],
                  (void *)configs[i]);
#endif
    }
  }

  return ok;
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy,
                                              const EGLint *attrib_list,
                                              EGLConfig *configs,
                                              EGLint config_size,
                                              EGLint *num_config)
{
#ifdef DEBUG
  log_console("[eglChooseConfig] BEGIN");
  log_console("  dpy=%p", dpy);
  log_console("  attrib_list=%p", attrib_list);
  log_console("  config_size=%d", config_size);
  log_console("  num_config ptr=%p", num_config);

  if (attrib_list)
  {
    log_console("  attrib_list contents:");
    for (int i = 0; attrib_list[i] != EGL_NONE; i += 2)
      log_console("    0x%04x = %d", attrib_list[i], attrib_list[i + 1]);
    log_console("    EGL_NONE");
  }
  else
  {
    log_console("  attrib_list = (null)");
  }
#endif

  uint32_t attr_sz = attrib_list_bytes(attrib_list);

#ifdef DEBUG
  log_console("  attr_sz=%u", attr_sz);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_eglChooseConfig;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_i32(&W, config_size);
  aw_u32(&W, attr_sz);
  C->args_len = W.pos;

  uint32_t attr_off = attr_sz ? bridge_data_write(attrib_list, attr_sz) : 0;
  uint32_t out_sz = (uint32_t)config_size * sizeof(uint32_t);
  uint32_t out = configs ? bridge_data_write(configs, out_sz) : 0;

  C->data_offset = attr_off;
  C->data_size = attr_sz;
  C->data2_offset = out;
  C->data2_size = out_sz;

#ifdef DEBUG
  log_console("  sending opcode=%u", C->opcode);
  log_console("  data_offset=%u data_size=%u", C->data_offset, C->data_size);
  log_console("  data2_offset=%u data2_size=%u", C->data2_offset,
              C->data2_size);
#endif

  EGLBoolean ok = (EGLBoolean)BRIDGE_SEND_CALL();

#ifdef DEBUG
  log_console("  BRIDGE_SEND_CALL returned ok=%d", ok);
  log_console("  C->result (num_config)=%llu",
              (unsigned long long)BRIDGE_CTRL()->result);
#endif

  if (num_config)
    *num_config = (EGLint)BRIDGE_CTRL()->result;

  if (configs && ok && *num_config > 0)
  {
    int cnt = *num_config < config_size ? *num_config : config_size;
    uint32_t tmp[EGL_BRIDGE_MAX_CONFIGS];

#ifdef DEBUG
    log_console("  reading back %d config indices", cnt);
#endif

    bridge_data_read(tmp, out, (size_t)cnt * sizeof(uint32_t));

    for (int i = 0; i < cnt; i++)
    {
#ifdef DEBUG
      log_console("    tmp[%d] = %u", i, tmp[i]);
#endif
      configs[i] = I2H(EGLConfig, tmp[i]);
#ifdef DEBUG
      log_console("    configs[%d] = %p", i, configs[i]);
#endif
    }
  }

#ifdef DEBUG
  log_console("[eglChooseConfig] END (return=%d)", ok);
#endif
  return ok;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy,
                                                 EGLConfig config,
                                                 EGLint attribute,
                                                 EGLint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglGetConfigAttrib);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(config));
  aw_i32(&W, attribute);
  C->args_len = W.pos;
  EGLBoolean ok = (EGLBoolean)BRIDGE_SEND_CALL();
  if (value)
    *value = (EGLint)BRIDGE_CTRL()->result;
  return ok;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Surface creation / destruction
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(
    EGLDisplay display, EGLConfig config, EGLNativeWindowType native_window,
    const EGLint *attrib_list)
{
  const struct wl_egl_window *ew =
      native_window ? (const struct wl_egl_window *)(uintptr_t)native_window
                    : NULL;

  void *surface = ew ? ew->surface : 0;
  uint32_t win_slot = ew ? ew->slot : 0;
  int win_width = ew ? ew->width : 0;
  int win_height = ew ? ew->height : 0;
  uint32_t attr_sz = attrib_list_bytes(attrib_list);

#ifdef DEBUG
  log_console("2. eglCreateWindowSurface native_window=%p surf=%p width=%d "
              "height=%d win_slot=%d attr_sz=%u",
              native_window, surface, win_width, win_height, win_slot, attr_sz);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_eglCreateWindowSurface;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(display));
  aw_u32(&W, H2I(config));
  // aw_u32(&W, surf_wire_id); /* wire ID, not a pointer */
  aw_u32(&W, win_slot);
  aw_i32(&W, win_width);
  aw_i32(&W, win_height);
  aw_u32(&W, attr_sz);
  C->args_len = W.pos;
  if (attr_sz)
  {
    C->data_offset = bridge_data_write(attrib_list, attr_sz);
    C->data_size = attr_sz;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }
  C->data2_offset = 0;
  C->data2_size = 0;

#ifdef DEBUG
  uint64_t raw = BRIDGE_SEND_CALL();
  log_console(
      "eglCreateWindowSurface: BRIDGE_SEND_CALL returned raw=%llu (0x%llx)",
      (unsigned long long)raw, (unsigned long long)raw);

  EGLSurface surf = I2H(EGLSurface, raw);

  log_console("eglCreateWindowSurface: returning EGLSurface=%p", surf);

  return surf;
#else
  return I2H(EGLSurface, BRIDGE_SEND_CALL());
#endif
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy,
                                                      EGLConfig config,
                                                      const EGLint *attrib_list)
{
  uint32_t attr_sz = attrib_list_bytes(attrib_list);
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_eglCreatePbufferSurface;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(config));
  aw_u32(&W, attr_sz);
  C->args_len = W.pos;
  if (attr_sz)
  {
    C->data_offset = bridge_data_write(attrib_list, attr_sz);
    C->data_size = attr_sz;
  }
  return I2H(EGLSurface, BRIDGE_SEND_CALL());
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy,
                                                     EGLConfig config,
                                                     EGLNativePixmapType pixmap,
                                                     const EGLint *attrib_list)
{
  uint32_t attr_sz = attrib_list_bytes(attrib_list);
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_eglCreatePixmapSurface;
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(config));
  aw_u64(&W, (uint64_t)(uintptr_t)pixmap);
  aw_u32(&W, attr_sz);
  C->args_len = W.pos;
  if (attr_sz)
  {
    C->data_offset = bridge_data_write(attrib_list, attr_sz);
    C->data_size = attr_sz;
  }
  return I2H(EGLSurface, BRIDGE_SEND_CALL());
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy,
                                                EGLSurface surface)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglDestroySurface);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(surface));
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy,
                                              EGLSurface surface,
                                              EGLint attribute, EGLint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglQuerySurface);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(surface));
  aw_i32(&W, attribute);
  C->args_len = W.pos;
  uint64_t r = BRIDGE_SEND_CALL();
  EGLBoolean ok = (EGLBoolean)(r >> 32);
  if (value)
    *value = (EGLint)(r & 0xFFFFFFFF);
  return ok;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy,
                                               EGLSurface surface,
                                               EGLint attribute, EGLint value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglSurfaceAttrib);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(surface));
  aw_i32(&W, attribute);
  aw_i32(&W, value);
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy,
                                              EGLSurface surface, EGLint buffer)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglBindTexImage);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(surface));
  aw_i32(&W, buffer);
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy,
                                                 EGLSurface surface,
                                                 EGLint buffer)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglReleaseTexImage);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(surface));
  aw_i32(&W, buffer);
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                                             EGLNativePixmapType target)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglCopyBuffers);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(surface));
  aw_u64(&W, (uint64_t)(uintptr_t)target);
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Context creation / management
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                               EGLContext share_context,
                                               const EGLint *attrib_list)
{
  uint32_t attr_sz = attrib_list_bytes(attrib_list);

#ifdef DEBUG
  log_console("eglCreateContext:"
              "    real args: dpy=%p cfg=%p share_context=%p attr_list=%p",
              dpy, config, share_context, attrib_list);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  C->opcode = OP_eglCreateContext;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(config));
  aw_u32(&W, H2I(share_context));
  aw_u32(&W, attr_sz);
  C->args_len = W.pos;

  if (attr_sz)
  {
    C->data_offset = bridge_data_write(attrib_list, attr_sz);
    C->data_size = attr_sz;
  }
  else
  {
    C->data_offset = 0;
    C->data_size = 0;
  }

  uint64_t raw = BRIDGE_SEND_CALL();

#ifdef DEBUG
  log_console("eglCreateContext: BRIDGE_SEND_CALL returned raw=%llu (0x%llx)",
              (unsigned long long)raw, (unsigned long long)raw);
#endif

  EGLContext ctx = I2H(EGLContext, raw);

#ifdef CACHE_GL_STATE
  if (raw != 0)
    g_stub_new_ctx = raw;
  // stub_context_state_reset((unsigned int)raw);
#endif

#ifdef DEBUG
  log_console("eglCreateContext: returning EGLContext=%p", ctx);
#endif

  return ctx;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglDestroyContext);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(ctx));
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay display,
                                             EGLSurface draw, EGLSurface read,
                                             EGLContext context)
{
  static uint32_t last_display = 0xFFFFFFFFu;
  static uint32_t last_draw = 0xFFFFFFFFu;
  static uint32_t last_read = 0xFFFFFFFFu;
  static uint32_t last_context = 0xFFFFFFFFu;
  static int have_last = 0;

  uint32_t d = H2I(display);
  uint32_t dr = H2I(draw);
  uint32_t rd = H2I(read);
  uint32_t ctx = H2I(context);

  if (have_last && last_display == d && last_draw == dr && last_read == rd &&
      last_context == ctx)
  {
    g_stub_current_ctx = ctx;
    return EGL_TRUE;
  }

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglMakeCurrent);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, d);
  aw_u32(&W, dr);
  aw_u32(&W, rd);
  aw_u32(&W, ctx);
  C->args_len = W.pos;

  g_stub_current_ctx = ctx;

  EGLBoolean ok = (EGLBoolean)BRIDGE_SEND_CALL();

  if (ok)
  {
    last_display = d;
    last_draw = dr;
    last_read = rd;
    last_context = ctx;
    have_last = 1;
  }
  else
    have_last = 0; /* failed — don't trust cached state */

  return ok;
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
  BRIDGE_BEGIN();
  setup_egl(OP_eglGetCurrentContext);
  BRIDGE_CTRL()->args_len = 0;
  return I2H(EGLContext, BRIDGE_SEND_CALL());
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglGetCurrentSurface);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, readdraw);
  C->args_len = W.pos;
  return I2H(EGLSurface, BRIDGE_SEND_CALL());
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
  BRIDGE_BEGIN();
  setup_egl(OP_eglGetCurrentDisplay);
  BRIDGE_CTRL()->args_len = 0;
  return I2H(EGLDisplay, BRIDGE_SEND_CALL());
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext ctx,
                                              EGLint attribute, EGLint *value)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglQueryContext);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(ctx));
  aw_i32(&W, attribute);
  C->args_len = W.pos;
  EGLBoolean ok = (EGLBoolean)BRIDGE_SEND_CALL();
  if (value)
    *value = (EGLint)BRIDGE_CTRL()->result;
  return ok;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Frame presentation
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglSwapBuffers);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_u32(&W, H2I(surface));
  C->args_len = W.pos;

#ifdef DEBUG_VERBOSE
  log_console("eglSwapBuffers dpy=%p egl surf=%p", dpy, surface);
#endif
#ifdef DEBUG_OPCODES
  {
    static uint64_t swap_count = 0;
    if ((++swap_count % 60) == 0)
      bridge_dump_backpressure_stats();
  }
#endif
  /* Always synchronous — must block until the frame is on screen */
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglSwapInterval);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, H2I(dpy));
  aw_i32(&W, interval);
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Sync / wait
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void)
{
  BRIDGE_BEGIN();
  setup_egl(OP_eglWaitGL);
  BRIDGE_CTRL()->args_len = 0;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine)
{
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglWaitNative);
  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_i32(&W, engine);
  C->args_len = W.pos;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient(void)
{
  BRIDGE_BEGIN();
  setup_egl(OP_eglWaitClient);
  BRIDGE_CTRL()->args_len = 0;
  return (EGLBoolean)BRIDGE_SEND_CALL();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * API binding / proc address
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
#ifdef DEBUG
  log_console("[eglBindAPI] api=0x%x", api);
  log_console("[eglBindAPI] eglGetProcAddress @ %p", eglGetProcAddress);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglBindAPI);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, api);
  C->args_len = W.pos;

#ifdef DEBUG
  log_console("[eglBindAPI] sending opcode=%u", C->opcode);
#endif

  EGLBoolean ok = (EGLBoolean)BRIDGE_SEND_CALL();

#ifdef DEBUG
  log_console("[eglBindAPI] returned ok=%d", ok);
#endif

  return ok;
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
  BRIDGE_BEGIN();
  setup_egl(OP_eglQueryAPI);
  BRIDGE_CTRL()->args_len = 0;
  return (EGLenum)BRIDGE_SEND_CALL();
}

EGLAPI EGLImage EGLAPIENTRY eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx,
                                              EGLenum target,
                                              EGLClientBuffer buffer,
                                              const EGLAttrib *attrib_list);

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImageKHR(EGLDisplay dpy,
                                                 EGLImage image);

void *lookup_core_func(const char *procname)
{
#define CORE_GLES2(name)                                                       \
  if (!strcmp(procname, #name))                                                \
    return (void *)name;

  CORE_GLES2(glActiveTexture)
  CORE_GLES2(glAttachShader)
  CORE_GLES2(glBindAttribLocation)
  CORE_GLES2(glBindBuffer)
  CORE_GLES2(glBindFramebuffer)
  CORE_GLES2(glBindRenderbuffer)
  CORE_GLES2(glBindTexture)
  CORE_GLES2(glBlendColor)
  CORE_GLES2(glBlendEquation)
  CORE_GLES2(glBlendEquationSeparate)
  CORE_GLES2(glBlendFunc)
  CORE_GLES2(glBlendFuncSeparate)
  CORE_GLES2(glBufferData)
  CORE_GLES2(glBufferSubData)
  CORE_GLES2(glCheckFramebufferStatus)
  CORE_GLES2(glClear)
  CORE_GLES2(glClearColor)
  CORE_GLES2(glClearDepthf)
  CORE_GLES2(glClearStencil)
  CORE_GLES2(glColorMask)
  CORE_GLES2(glCompileShader)
  CORE_GLES2(glCompressedTexImage2D)
  CORE_GLES2(glCompressedTexSubImage2D)
  CORE_GLES2(glCopyTexImage2D)
  CORE_GLES2(glCopyTexSubImage2D)
  CORE_GLES2(glCreateProgram)
  CORE_GLES2(glCreateShader)
  CORE_GLES2(glCullFace)
  CORE_GLES2(glDeleteBuffers)
  CORE_GLES2(glDeleteFramebuffers)
  CORE_GLES2(glDeleteProgram)
  CORE_GLES2(glDeleteRenderbuffers)
  CORE_GLES2(glDeleteShader)
  CORE_GLES2(glDeleteTextures)
  CORE_GLES2(glDepthFunc)
  CORE_GLES2(glDepthMask)
  CORE_GLES2(glDepthRangef)
  CORE_GLES2(glDetachShader)
  CORE_GLES2(glDisable)
  CORE_GLES2(glDisableVertexAttribArray)
  CORE_GLES2(glDrawArrays)
  CORE_GLES2(glDrawElements)
  CORE_GLES2(glEnable)
  CORE_GLES2(glEnableVertexAttribArray)
  CORE_GLES2(glFinish)
  CORE_GLES2(glFlush)
  CORE_GLES2(glFramebufferRenderbuffer)
  CORE_GLES2(glFramebufferTexture2D)
  CORE_GLES2(glFrontFace)
  CORE_GLES2(glGenBuffers)
  CORE_GLES2(glGenFramebuffers)
  CORE_GLES2(glGenRenderbuffers)
  CORE_GLES2(glGenTextures)
  CORE_GLES2(glGetActiveAttrib)
  CORE_GLES2(glGetActiveUniform)
  CORE_GLES2(glGetAttachedShaders)
  CORE_GLES2(glGetAttribLocation)
  CORE_GLES2(glGetBooleanv)
  CORE_GLES2(glGetBufferParameteriv)
  CORE_GLES2(glGetError)
  CORE_GLES2(glGetFloatv)
  CORE_GLES2(glGetFramebufferAttachmentParameteriv)
  CORE_GLES2(glGetIntegerv)
  CORE_GLES2(glGetProgramInfoLog)
  CORE_GLES2(glGetProgramiv)
  CORE_GLES2(glGetRenderbufferParameteriv)
  CORE_GLES2(glGetShaderInfoLog)
  CORE_GLES2(glGetShaderiv)
  CORE_GLES2(glGetString)
  CORE_GLES2(glGetTexParameterfv)
  CORE_GLES2(glGetTexParameteriv)
  CORE_GLES2(glGetUniformfv)
  CORE_GLES2(glGetUniformiv)
  CORE_GLES2(glGetUniformLocation)
  CORE_GLES2(glGetVertexAttribfv)
  CORE_GLES2(glGetVertexAttribiv)
  CORE_GLES2(glGetVertexAttribPointerv)
  CORE_GLES2(glHint)
  CORE_GLES2(glIsBuffer)
  CORE_GLES2(glIsEnabled)
  CORE_GLES2(glIsFramebuffer)
  CORE_GLES2(glIsProgram)
  CORE_GLES2(glIsRenderbuffer)
  CORE_GLES2(glIsShader)
  CORE_GLES2(glIsTexture)
  CORE_GLES2(glLineWidth)
  CORE_GLES2(glLinkProgram)
  CORE_GLES2(glPixelStorei)
  CORE_GLES2(glPolygonOffset)
  CORE_GLES2(glReadPixels)
  CORE_GLES2(glRenderbufferStorage)
  CORE_GLES2(glSampleCoverage)
  CORE_GLES2(glScissor)
  CORE_GLES2(glShaderBinary)
  CORE_GLES2(glShaderSource)
  CORE_GLES2(glStencilFunc)
  CORE_GLES2(glStencilFuncSeparate)
  CORE_GLES2(glStencilMask)
  CORE_GLES2(glStencilMaskSeparate)
  CORE_GLES2(glStencilOp)
  CORE_GLES2(glStencilOpSeparate)
  CORE_GLES2(glTexImage2D)
  CORE_GLES2(glTexParameterf)
  CORE_GLES2(glTexParameterfv)
  CORE_GLES2(glTexParameteri)
  CORE_GLES2(glTexParameteriv)
  CORE_GLES2(glTexSubImage2D)
  CORE_GLES2(glUniform1f)
  CORE_GLES2(glUniform1fv)
  CORE_GLES2(glUniform1i)
  CORE_GLES2(glUniform1iv)
  CORE_GLES2(glUniform2f)
  CORE_GLES2(glUniform2fv)
  CORE_GLES2(glUniform2i)
  CORE_GLES2(glUniform2iv)
  CORE_GLES2(glUniform3f)
  CORE_GLES2(glUniform3fv)
  CORE_GLES2(glUniform3i)
  CORE_GLES2(glUniform3iv)
  CORE_GLES2(glUniform4f)
  CORE_GLES2(glUniform4fv)
  CORE_GLES2(glUniform4i)
  CORE_GLES2(glUniform4iv)
  CORE_GLES2(glUniformMatrix2fv)
  CORE_GLES2(glUniformMatrix3fv)
  CORE_GLES2(glUniformMatrix4fv)
  CORE_GLES2(glUseProgram)
  CORE_GLES2(glValidateProgram)
  CORE_GLES2(glVertexAttrib1f)
  CORE_GLES2(glVertexAttrib1fv)
  CORE_GLES2(glVertexAttrib2f)
  CORE_GLES2(glVertexAttrib2fv)
  CORE_GLES2(glVertexAttrib3f)
  CORE_GLES2(glVertexAttrib3fv)
  CORE_GLES2(glVertexAttrib4f)
  CORE_GLES2(glVertexAttrib4fv)
  CORE_GLES2(glVertexAttribPointer)
  CORE_GLES2(glViewport)

#undef CORE_GLES2

#define CORE_GLES3(name)                                                       \
  if (!strcmp(procname, #name))                                                \
    return (void *)name;

  /* ================================================================ */
  /* GLES 3.0                                                         */
  /* ================================================================ */

  /* ---- Vertex Arrays ---- */
  CORE_GLES3(glGenVertexArrays)
  CORE_GLES3(glDeleteVertexArrays)
  CORE_GLES3(glBindVertexArray)
  CORE_GLES3(glIsVertexArray)

  /* ---- Integer Attributes ---- */
  CORE_GLES3(glVertexAttribI4i)
  CORE_GLES3(glVertexAttribI4iv)
  CORE_GLES3(glVertexAttribI4ui)
  CORE_GLES3(glVertexAttribI4uiv)
  CORE_GLES3(glVertexAttribIPointer)
  CORE_GLES3(glGetVertexAttribIiv)
  CORE_GLES3(glGetVertexAttribIuiv)

  /* ---- Instancing ---- */
  CORE_GLES3(glDrawArraysInstanced)
  CORE_GLES3(glDrawElementsInstanced)
  CORE_GLES3(glVertexAttribDivisor)

  /* ---- Buffer Mapping ---- */
  CORE_GLES3(glMapBufferRange)
  CORE_GLES3(glFlushMappedBufferRange)
  CORE_GLES3(glUnmapBuffer)
  CORE_GLES3(glCopyBufferSubData)
  CORE_GLES3(glGetBufferPointerv)

  /* ---- Query Objects ---- */
  CORE_GLES3(glGenQueries)
  CORE_GLES3(glDeleteQueries)
  CORE_GLES3(glBeginQuery)
  CORE_GLES3(glEndQuery)
  CORE_GLES3(glGetQueryiv)
  CORE_GLES3(glGetQueryObjectuiv)
  CORE_GLES3(glIsQuery)

  /* ---- Samplers ---- */
  CORE_GLES3(glGenSamplers)
  CORE_GLES3(glDeleteSamplers)
  CORE_GLES3(glBindSampler)
  CORE_GLES3(glIsSampler)
  CORE_GLES3(glSamplerParameteri)
  CORE_GLES3(glSamplerParameteriv)
  CORE_GLES3(glSamplerParameterf)
  CORE_GLES3(glSamplerParameterfv)
  CORE_GLES3(glGetSamplerParameteriv)
  CORE_GLES3(glGetSamplerParameterfv)

  /* ---- Transform Feedback ---- */
  CORE_GLES3(glBeginTransformFeedback)
  CORE_GLES3(glEndTransformFeedback)
  CORE_GLES3(glTransformFeedbackVaryings)
  CORE_GLES3(glGetTransformFeedbackVarying)
  CORE_GLES3(glBindBufferBase)
  CORE_GLES3(glBindBufferRange)
  CORE_GLES3(glGenTransformFeedbacks)
  CORE_GLES3(glDeleteTransformFeedbacks)
  CORE_GLES3(glBindTransformFeedback)
  CORE_GLES3(glPauseTransformFeedback)
  CORE_GLES3(glResumeTransformFeedback)
  CORE_GLES3(glIsTransformFeedback)

  /* ---- Uniform Integer ---- */
  CORE_GLES3(glGetUniformuiv)
  CORE_GLES3(glUniform1ui)
  CORE_GLES3(glUniform1uiv)
  CORE_GLES3(glUniform2ui)
  CORE_GLES3(glUniform2uiv)
  CORE_GLES3(glUniform3ui)
  CORE_GLES3(glUniform3uiv)
  CORE_GLES3(glUniform4ui)
  CORE_GLES3(glUniform4uiv)

  /* ---- Uniform Blocks ---- */
  CORE_GLES3(glGetUniformIndices)
  CORE_GLES3(glGetActiveUniformsiv)
  CORE_GLES3(glGetUniformBlockIndex)
  CORE_GLES3(glGetActiveUniformBlockiv)
  CORE_GLES3(glGetActiveUniformBlockName)
  CORE_GLES3(glUniformBlockBinding)

  /* ---- Texture / 3D Texture ---- */
  CORE_GLES3(glTexImage3D)
  CORE_GLES3(glTexSubImage3D)
  CORE_GLES3(glCopyTexSubImage3D)
  CORE_GLES3(glCompressedTexImage3D)
  CORE_GLES3(glCompressedTexSubImage3D)
  CORE_GLES3(glTexStorage2D)
  CORE_GLES3(glTexStorage3D)

  /* ---- Indexed State ---- */

  CORE_GLES3(glColorMaski)
  CORE_GLES3(glEnablei)
  CORE_GLES3(glDisablei)
  CORE_GLES3(glIsEnabledi)
  CORE_GLES3(glGetBooleani_v)
  CORE_GLES3(glGetTexParameterIiv)
  CORE_GLES3(glGetTexParameterIuiv)
  CORE_GLES3(glTexParameterIiv)
  CORE_GLES3(glTexParameterIuiv)

  /* ---- Sync ---- */
  CORE_GLES3(glFenceSync)
  CORE_GLES3(glClientWaitSync)
  CORE_GLES3(glWaitSync)
  CORE_GLES3(glDeleteSync)
  CORE_GLES3(glGetSynciv)
  CORE_GLES3(glIsSync)

  /* ---- Misc ---- */
  CORE_GLES3(glGetStringi)
  CORE_GLES3(glGetInteger64v)
  CORE_GLES3(glGetFragDataLocation)

  /* ---- Multisample/FBO ---- */
  CORE_GLES3(glRenderbufferStorageMultisample)
  CORE_GLES3(glBlitFramebuffer)
  CORE_GLES3(glFramebufferTextureLayer)

  /* ================================================================ */
  /* GLES 3.2                                                         */
  /* ================================================================ */

  /* ---- Debug ---- */
  CORE_GLES3(glDebugMessageCallback)
  CORE_GLES3(glDebugMessageControl)

  /* ============================================================== */
  /* GLES 3.x / extras                                                */
  /* ================================================================ */

  /* Framebuffer / readback / ranges */
  CORE_GLES3(glReadBuffer)
  CORE_GLES3(glDrawRangeElements)

  /* Shader / program info */
  CORE_GLES3(glGetShaderSource)
  CORE_GLES3(glGetProgramBinary)
  CORE_GLES3(glProgramBinary)
  CORE_GLES3(glProgramParameteri)
  CORE_GLES3(glGetShaderPrecisionFormat)
  CORE_GLES3(glReleaseShaderCompiler)

  /* Draw buffers / clears */
  CORE_GLES3(glDrawBuffers)
  CORE_GLES3(glClearBufferfi)
  CORE_GLES3(glClearBufferfv)
  CORE_GLES3(glClearBufferiv)
  CORE_GLES3(glClearBufferuiv)

  /* Buffers / integer queries */
  CORE_GLES3(glTexBuffer)
  CORE_GLES3(glGetBufferParameteri64v)
  CORE_GLES3(glGetInteger64i_v)
  CORE_GLES3(glGetIntegeri_v)

  /* Samplers (integer variants) */
  CORE_GLES3(glGetSamplerParameterIiv)
  CORE_GLES3(glGetSamplerParameterIuiv)
  CORE_GLES3(glSamplerParameterIiv)
  CORE_GLES3(glSamplerParameterIuiv)

  /* Mipmap / storage */
  CORE_GLES3(glGenerateMipmap)
  CORE_GLES3(glTexStorage2DMultisample)
  CORE_GLES3(glTexStorage3DMultisample)

  /* FBO / internal formats */
  CORE_GLES3(glFramebufferTexture)
  CORE_GLES3(glGetInternalformativ)

  /* Base‑vertex / instanced draws */
  CORE_GLES3(glDrawElementsBaseVertex)
  CORE_GLES3(glDrawElementsInstancedBaseVertex)
  CORE_GLES3(glDrawRangeElementsBaseVertex)

  /* Sample shading */
  CORE_GLES3(glMinSampleShading)

  /* Pointer query */
  CORE_GLES3(glGetPointerv)

  /* ARB/EXT‑style aliases some cores probe */
  CORE_GLES3(glDebugMessageInsert)
  CORE_GLES3(glGetDebugMessageLog)
  CORE_GLES3(glGetObjectLabel)
  CORE_GLES3(glGetObjectPtrLabel)
  CORE_GLES3(glObjectLabel)
  CORE_GLES3(glObjectPtrLabel)
  CORE_GLES3(glPopDebugGroup)
  CORE_GLES3(glPushDebugGroup)

  /* Image / compute */
  CORE_GLES3(glCopyImageSubData)
  CORE_GLES3(glBindImageTexture)
  CORE_GLES3(glMemoryBarrier)
  CORE_GLES3(glDispatchCompute)
  CORE_GLES3(glDispatchComputeIndirect)

  CORE_GLES3(eglCreateImageKHR)
  CORE_GLES3(eglDestroyImageKHR)
#undef CORE_GLES3

  return NULL;
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
eglGetProcAddress(const char *procname)
{
  // this is a workaround for dolphin etc.
  void *core = lookup_core_func(procname);
  if (core)
  {
#ifdef DEBUG_EGL_GETPROC
    log_console("eglGetProcAddress: procname: %s found as core func at %p",
                procname, core);
#endif
    return core;
  }

  /* 1. GLES table */
  ProcEntry *p = find_proc(procname);

  /* 2. EGL extension table */
  EGLProcEntry *ep = NULL;
  if (!p)
    ep = egl_find_proc(procname);

#ifdef DEBUG_EGL_GETPROC
  log_console("eglGetProcAddress: procname: %s", procname);
#endif

  /* If neither table has it - return NULL */
  if (!p && !ep)
  {
#ifdef DEBUG_EGL_GETPROC
    log_console("eglGetProcAddress: %s NOT FOUND in either table", procname);
#endif
    return NULL;
  }

  // already resolve, so don't resolve again
  if (p && p->resolved)
    return p->dispatch;
  if (ep && ep->resolved)
    return ep->dispatch;

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglGetProcAddress);

  size_t len = strlen(procname) + 1;
  if (len > BRIDGE_ARGS_SIZE)
    len = BRIDGE_ARGS_SIZE;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, p ? p->idx : ep->idx);
  aw_u32(&W, ep ? 1 : 0);
  memcpy(C->args + W.pos, procname, len);
  C->args_len = W.pos;

  uint32_t idx = BRIDGE_SEND_CALL();

  if (!idx)
  {
#ifdef DEBUG_EGL_GETPROC
    log_console(
        "eglGetProcAddress - returned from proxy with no idx! - procname %s",
        procname);
#endif
    return NULL;
  }

#ifdef DEBUG_EGL_GETPROC
  log_console(
      "eglGetProcAddress - returned from proxy - procname: %s %s idx: %d "
      "dispatch: %s: %p",
      procname, p ? "gl" : "egl", idx, p ? "gl" : "egl",
      p ? p->dispatch : ep->dispatch);
#endif

  if (p)
  {
    p->idx = idx;
    p->resolved = 1;
  }
  else if (ep)
  {
    ep->idx = idx;
    ep->resolved = 1;
  }

  // return a 64-bit adress to a dispatch_xyz function
  // (gles2_generated/egl_generated) which from there, is used to communicate
  // with the proxy and call real function
  return p ? p->dispatch : ep->dispatch;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Pbuffer from client buffer (EGL 1.2 — stub returns EGL_NO_SURFACE)
 * ═══════════════════════════════════════════════════════════════════════════
 */

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer(
    EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config,
    const EGLint *attrib_list)
{
#ifdef DEBUG
  log_console(
      "eglCreatePbufferFromClientBuffer - STUB called! This should not be "
      "called by client code, and will always fail. dpy=%p buftype=0x%x "
      "buffer=%p config=%p attrib_list=%p",
      dpy, buftype, buffer, config, attrib_list);
#endif
  /* Not used by client; forward as a no-op that returns failure */
  (void)dpy;
  (void)buftype;
  (void)buffer;
  (void)config;
  (void)attrib_list;
  return EGL_NO_SURFACE;
}

EGLAPI EGLImage EGLAPIENTRY eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx,
                                              EGLenum target,
                                              EGLClientBuffer buffer,
                                              const EGLAttrib *attrib_list)
{
#ifdef DEBUG_VERBOSE
  log_console(
      "eglCreateImageKHR: dpy=%p ctx=%p target=0x%x buffer=%p attrib_list=%p",
      dpy, ctx, target, buffer, attrib_list);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglCreateImageKHR);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u64(&W, (uint64_t)(uintptr_t)dpy);
  aw_u64(&W, (uint64_t)(uintptr_t)ctx);
  aw_u32(&W, target);
  aw_u64(&W, (uint64_t)(uintptr_t)buffer);

  if (attrib_list)
  {
    const EGLAttrib *p = attrib_list;
    while (*p != EGL_NONE)
    {
      aw_u64(&W, p[0]);
      aw_u64(&W, p[1]);
      p += 2;
    }
  }
  aw_u64(&W, EGL_NONE);

  C->args_len = W.pos;

  uint64_t real = BRIDGE_SEND_CALL();
  return (EGLImage)(uintptr_t)real;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImageKHR(EGLDisplay dpy, EGLImage image)
{
#ifdef DEBUG_VERBOSE
  log_console("eglDestroyImageKHR: dpy=%p image=%p", dpy, image);
#endif
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_egl(OP_eglDestroyImageKHR);

#ifdef DEBUG_EGL_GETPROC
  log_console("stub eglDestroyImageKHR: dpy=%p image=%p", dpy, image);
#endif

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);

  aw_u64(&W, (uint64_t)(uintptr_t)dpy);
  aw_u64(&W, (uint64_t)(uintptr_t)image);

  C->args_len = W.pos;

  uint32_t ok = BRIDGE_SEND_CALL();
  return ok ? EGL_TRUE : EGL_FALSE;
}
