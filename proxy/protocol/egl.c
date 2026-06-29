#include "../proxy.h"

#include <syscall.h>
#include <unistd.h>

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>

#include "../tools/egl_generated.h"
#include "../tools/gles2_generated.h"

#define LOG_PREFIX "[proxy/egl]"

#if defined(DEBUG_VERBOSE) || defined(HAVE_OWN_WAYLAND_CLIENT)
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* ══════════════════════════════════════════════════════════════════════════
 * EGL handle tables
 *
 * The 64-bit stub sends 1-based uint32 indices; we map them to real
 * 32-bit EGL pointers here.  Index 0 == EGL_NO_* for all table types.
 * ══════════════════════════════════════════════════════════════════════════ */
EGLHandleEntry egl_displays[EGL_BRIDGE_MAX_DISPLAYS + 1];
EGLHandleEntry egl_configs[EGL_BRIDGE_MAX_CONFIGS + 1];
EGLHandleEntry egl_contexts[EGL_BRIDGE_MAX_CONTEXTS + 1];
EGLHandleEntry egl_surfaces[EGL_BRIDGE_MAX_SURFACES + 1];

uint32_t g_current_ctx = 0;

// EGL extensions dynamic lookup
typedef struct
{
  void *real;
  char name[128];
} DynProc;

// EGL extensions
#define MAX_DYNAMIC_FUNCS 2048

static DynProc gl_dyn_table[MAX_DYNAMIC_FUNCS];
static uint32_t gl_dyn_count = 1;

static DynProc egl_dyn_table[MAX_DYNAMIC_FUNCS];
static uint32_t egl_dyn_count = 1;

/* Call once from main() to preset index-0 as EGL_NO_* */
void egl_tables_init(void)
{
  egl_displays[0].handle = EGL_NO_DISPLAY;
  egl_contexts[0].handle = EGL_NO_CONTEXT;
  egl_surfaces[0].handle = EGL_NO_SURFACE;
}

void dump_ctx(const char *where)
{
  EGLDisplay d = eglGetCurrentDisplay();
  EGLContext c = eglGetCurrentContext();
  EGLSurface draw = eglGetCurrentSurface(EGL_DRAW);
  EGLSurface read = eglGetCurrentSurface(EGL_READ);

  log_console("[CTX %s] display=%p context=%p draw=%p read=%p", where, d, c,
              draw, read);

  EGLint cfgid = 0;

  EGLConfig cfg;
  EGLint n = 0;

  eglQueryContext(d, c, EGL_CONFIG_ID, &cfgid);

  log_console("ctx config id=%d", cfgid);

  GL_LOG_IF_ERR("glerr=0x%x", after_err);
}

/* Allocate a new slot in a table; returns the 1-based index or 0 on overflow */
#define TABLE_ALLOC(table, max, real, pid)                                     \
  ({                                                                           \
    uint32_t _idx = 0;                                                         \
    for (uint32_t _i = 1; _i <= (max); _i++)                                   \
    {                                                                          \
      if (!(table)[_i].handle)                                                 \
      {                                                                        \
        (table)[_i].handle = (void *)(real);                                   \
        (table)[_i].owner_pid = (pid);                                         \
        _idx = _i;                                                             \
        break;                                                                 \
      }                                                                        \
    }                                                                          \
    _idx;                                                                      \
  })

#define TABLE_FREE(table, idx)                                                 \
  do                                                                           \
  {                                                                            \
    (table)[idx].handle = NULL;                                                \
    (table)[idx].owner_pid = 0;                                                \
  } while (0)

#define DISP(i) egl_displays[(i) <= EGL_BRIDGE_MAX_DISPLAYS ? (i) : 0].handle
#define CFG(i) egl_configs[(i) <= EGL_BRIDGE_MAX_CONFIGS ? (i) : 0].handle
#define CTX(i) egl_contexts[(i) <= EGL_BRIDGE_MAX_CONTEXTS ? (i) : 0].handle
#define SURF(i) egl_surfaces[(i) <= EGL_BRIDGE_MAX_SURFACES ? (i) : 0].handle

/* ── EGL handlers ────────────────────────────────────────────────────────── */

void h_eglGetPlatformDisplayEXT(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  EGLenum platform = ar_u32(&r);
  uint64_t native_u64 = ar_u64(&r);
  void *native = (void *)(uintptr_t)(uint32_t)native_u64;

#ifdef DEBUG_VERBOSE
  log_console("h_eglGetPlatformDisplayEXT: BEGIN");
  log_console("  platform=0x%x", platform);
  log_console("  native_u64=0x%016llx", (unsigned long long)native_u64);
  log_console("  native(truncated)=%p", native);
#endif

  PFNEGLGETPLATFORMDISPLAYEXTPROC p_eglGetPlatformDisplayEXT = NULL;
  int resolved = 0;

  if (!resolved)
  {
#ifdef DEBUG_VERBOSE
    log_console(
        "  resolving eglGetPlatformDisplayEXT via eglGetProcAddress...");
#endif
    p_eglGetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
            "eglGetPlatformDisplayEXT");
#ifdef DEBUG_VERBOSE
    log_console("  eglGetProcAddress result = %p", p_eglGetPlatformDisplayEXT);
#endif
    resolved = 1;
  }

  if (!p_eglGetPlatformDisplayEXT)
  {
    log_error("  ERROR: eglGetPlatformDisplayEXT NOT AVAILABLE in 32-bit EGL");
    C->result = 0;
#ifdef DEBUG_VERBOSE
    log_console("h_eglGetPlatformDisplayEXT: END (result=0)");
#endif
    return;
  }

#ifdef DEBUG_VERBOSE
  log_console("  calling eglGetPlatformDisplayEXT(platform=0x%x, native=%p, "
              "attribs=NULL)",
              platform, native);
#endif

  EGLDisplay real = p_eglGetPlatformDisplayEXT(platform, native, NULL);

#ifdef DEBUG_VERBOSE
  log_console("  eglGetPlatformDisplayEXT returned real=%p", (void *)real);
#endif

  if (real == EGL_NO_DISPLAY)
  {
    log_error("  ERROR: eglGetPlatformDisplayEXT returned EGL_NO_DISPLAY");
    C->result = 0;
#ifdef DEBUG_VERBOSE
    log_console("h_eglGetPlatformDisplayEXT: END (result=0)");
#endif
    return;
  }

  uint32_t idx =
      TABLE_ALLOC(egl_displays, EGL_BRIDGE_MAX_DISPLAYS, real, C->client_pid);
  if (!idx)
  {
    log_error("  WARNING: display table full, reusing slot 1");
    egl_displays[DEFAULT_EGL_IDX].handle = real;
    idx = 1;
  }

#ifdef DEBUG_VERBOSE
  log_console("  assigned index=%u for egl_displays real=%p", idx,
              (void *)real);
#endif
  C->result = idx;

#ifdef DEBUG_VERBOSE
  log_console("h_eglGetPlatformDisplayEXT: END (result=%u)", idx);
#endif

  (void)D;
}

void h_eglGetDisplay(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t wl_fd = ar_u32(&r); /* stub's eventfd */
  uint32_t watermark = ar_u32(&r);
  (void)wl_fd;

#ifdef HAVE_OWN_WAYLAND_CLIENT
  log_console(
      "h_eglGetDisplay (tid=%ld): watermark=%u wl_fd=%d proxy_wl_display: %p "
      "proxy_wl_display fd: %d ",
      syscall(SYS_gettid), watermark, wl_fd, proxy_wl_display,
      wl_display_get_fd(proxy_wl_display));
  /*
   * proxy_wl_display was already opened by proxy_wayland_init() inside
   * h_wl_display_connect (if using libwayland-client-stub)
   */
  if (!proxy_wl_display)
  {
    log_error("h_eglGetDisplay: ERROR no proxy_wl_display");
    C->result = 0;
    return;
  }
  EGLDisplay real = eglGetDisplay((EGLNativeDisplayType)proxy_wl_display);
#else
  log_console("h_eglGetDisplay: watermark=%u wl_fd: %d", watermark, wl_fd);

  // TODO: 64-bit?
  EGLDisplay real = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif

  uint32_t idx =
      TABLE_ALLOC(egl_displays, EGL_BRIDGE_MAX_DISPLAYS, real, C->client_pid);
  if (!idx && real != EGL_NO_DISPLAY)
  {
    egl_displays[DEFAULT_EGL_IDX].handle = real;
    egl_displays[DEFAULT_EGL_IDX].owner_pid = C->client_pid;
    idx = 1;
  }

#ifdef DEBUG_VERBOSE
  log_console("h_eglGetDisplay: eglGetDisplay -> %p idx -> %u", real, idx);
#endif

  C->result = idx;
  (void)D;
}

void h_eglInitialize(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);

  EGLDisplay real = DISP(di);
  EGLHandleEntry *e = &egl_displays[di];

#ifdef DEBUG
  log_console("h_eglInitialize (tid=%ld): di=%u real=%p initialized=%d",
              syscall(SYS_gettid), di, (void *)real, e->initialized);
#endif

  EGLint major = 0, minor = 0;
  EGLBoolean ok = EGL_FALSE;

  if (real != EGL_NO_DISPLAY)
  {
    if (!e->initialized)
    {
#ifdef DEBUG
      log_console("h_eglInitialize: calling eglInitialize");
#endif
      ok = eglInitialize(real, &major, &minor);
      e->initialized = true;
      e->major = major;
      e->minor = minor;
    }
    else
      log_console("h_eglInitialize: already eglInitialized");
  }
  else
  {
    log_error("h_eglInitialize: ERROR — DISP(%u) returned NULL", di);
  }

  if (!e->initialized)
  {
    EGLint err = eglGetError();
    log_error("h_eglInitialize: FAILED ok=0 eglGetError=0x%04x", err);
#ifdef DEBUG_ABORT_ON_GL_ERROR
    abort();
#endif
  }
#ifdef DEBUG
  else
  {
    log_console("h_eglInitialize: OK major=%d minor=%d", major, minor);
  }
#endif

  ((EGLint *)C->result_buf)[0] = e->major;
  ((EGLint *)C->result_buf)[1] = e->minor;

  C->result = ok;

  (void)D;
}

void h_eglTerminate(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  EGLHandleEntry *e = &egl_displays[di];

  C->result = eglTerminate(DISP(di));
  if (C->result)
  {
    e->initialized = 0;
    e->major = 0;
    e->minor = 0;
  }

  egl_displays[di].owner_pid = 0;
  (void)D;
}

void h_eglReleaseThread(BridgeCtrl *C, uint8_t *D)
{
  C->result = eglReleaseThread();
  (void)C;
  (void)D;
}

void h_eglQueryString(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  EGLint name = ar_i32(&r);

  if (di == 0 || DISP(di) == EGL_NO_DISPLAY)
  {
    C->result = 0;
#ifdef DEBUG_VERBOSE
    log_console("h_eglQueryString: di=%u DISP(di)=%d name=0x%04x -> no display",
                di, DISP(di), name);
#endif
    if (DISP(di) == EGL_NO_DISPLAY && name == EGL_EXTENSIONS)
    {
      const char *s = eglQueryString(EGL_NO_DISPLAY, name);
      if (s)
      {
        strncpy((char *)C->result_buf, s, BRIDGE_RESULT_SIZE - 1);
        return;
      }
    }
    eglGetError(); // clear global EGL error flag
    C->result_buf[0] = '\0';
    (void)D;
    return;
  }

  const char *s = eglQueryString(DISP(di), name);

  if (s)
    strncpy((char *)C->result_buf, s, BRIDGE_RESULT_SIZE - 1);
  else
    C->result_buf[0] = '\0';
#ifdef DEBUG_EGL_GETPROC
  log_console("h_eglQueryString: di=%u name=0x%04x -> \"%s\"", di, name,
              s ? s : "(null)");
#endif
  C->result = 0;
  (void)D;
}

void h_eglGetConfigs(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  EGLint cfg_sz = ar_i32(&r);

#ifdef DEBUG
  log_console("h_eglGetConfigs: di: %d cfg_sz: %d", di, cfg_sz);
#endif

  EGLDisplay real = DISP(di);
  EGLConfig tmp[EGL_BRIDGE_MAX_CONFIGS];
  EGLint num = 0;
  EGLBoolean ok = EGL_FALSE;

  if (real != EGL_NO_DISPLAY)
  {
    EGLint limit =
        cfg_sz < EGL_BRIDGE_MAX_CONFIGS ? cfg_sz : EGL_BRIDGE_MAX_CONFIGS;
    /* Pass NULL array when cfg_sz==0 so Mali returns the total count */
    ok = eglGetConfigs(real, limit > 0 ? tmp : NULL, limit, &num);
  }

  /* Map configs into table and write indices (only when configs were requested)
   */
  uint32_t *out = (uint32_t *)dp(C->data_offset);
  if (cfg_sz > 0 && ok)
  {
    for (EGLint i = 0; i < num; i++)
    {
      uint32_t ci = TABLE_ALLOC(egl_configs, EGL_BRIDGE_MAX_CONFIGS, tmp[i],
                                C->client_pid);
      if (!ci)
      {
        for (uint32_t j = 1; j <= EGL_BRIDGE_MAX_CONFIGS; j++)
          if (egl_configs[j].handle == tmp[i])
          {
            ci = j;
            break;
          }
      }
      out[i] = ci;
    }
  }

  /* Pack: low 32 = num, high 32 = ok */
  C->result = (uint64_t)(uint32_t)num | ((uint64_t)ok << 32);
  (void)D;
}

void h_eglChooseConfig(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t di = ar_u32(&r);
  EGLint cfg_sz = ar_i32(&r);
  uint32_t attr_sz = ar_u32(&r);

#ifdef DEBUG
  log_console("h_eglChooseConfig: BEGIN");
  log_console("  di      = %u", di);
  log_console("  cfg_sz  = %d", cfg_sz);
  log_console("  attr_sz = %u", attr_sz);
#endif

  const EGLint *attr = attr_sz ? (const EGLint *)dp(C->data_offset) : NULL;

#ifdef DEBUG
  if (attr)
  {
    log_console("  attributes:");
    for (uint32_t i = 0; attr[i] != EGL_NONE; i += 2)
      log_console("    0x%04x = %d", attr[i], attr[i + 1]);
    log_console("    EGL_NONE");
  }
  else
  {
    log_console("  attributes = (null)");
  }
#endif

  EGLConfig tmp[EGL_BRIDGE_MAX_CONFIGS];
  EGLint num = 0;

#ifdef DEBUG
  log_console("  calling eglChooseConfig(dpy=%p, attr=%p, max=%d)", DISP(di),
              attr, cfg_sz);
#endif

  EGLBoolean ok = eglChooseConfig(
      DISP(di), attr, tmp,
      (cfg_sz < EGL_BRIDGE_MAX_CONFIGS ? cfg_sz : EGL_BRIDGE_MAX_CONFIGS),
      &num);

#ifdef DEBUG
  log_console("  eglChooseConfig returned ok=%d num=%d", ok, num);
#endif

  uint32_t *out = (uint32_t *)dp(C->data2_offset);

  for (EGLint i = 0; i < num; i++)
  {
    uint32_t ci = 0;

    for (uint32_t j = 1; j <= EGL_BRIDGE_MAX_CONFIGS; j++)
    {
      if (egl_configs[j].handle == tmp[i])
      {
        ci = j;
        break;
      }
    }

    if (!ci)
    {
      ci = TABLE_ALLOC(egl_configs, EGL_BRIDGE_MAX_CONFIGS, tmp[i],
                       C->client_pid);
#ifdef DEBUG
      log_console("  new config index allocated: %u for %p", ci, tmp[i]);
#endif
    }
    else
    {
#ifdef DEBUG
      log_console("  existing config index %u for %p", ci, tmp[i]);
#endif
    }

    out[i] = ci;
  }

#ifdef DEBUG
  log_console("  setting C->result = %u", (uint32_t)num);
#endif

  C->result = (uint64_t)(uint32_t)num;

#ifdef DEBUG
  log_console("h_eglChooseConfig: END");
#endif
  (void)D;
  (void)ok;
}

void h_eglGetConfigAttrib(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t ci = ar_u32(&r);
  EGLint attr = ar_i32(&r);
  EGLint val = 0;
  EGLBoolean ok = eglGetConfigAttrib(DISP(di), CFG(ci), attr, &val);
  C->result = (uint64_t)(int64_t)val;
  (void)ok;
  (void)D;
}

/* ── h_eglCreateWindowSurface ───────────────────────────────────────────── */
void h_eglCreateWindowSurface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t display = ar_u32(&r);
  uint32_t config = ar_u32(&r);
  // uint32_t surf_slot = ar_u32(&r); /* proxy surface slot from stub */
  uint32_t win_slot = ar_u32(&r);
  int width = ar_i32(&r);
  int height = ar_i32(&r);
  uint32_t attr_sz = ar_u32(&r);

#ifdef DEBUG
  log_console("h_eglCreateWindowSurface: display=%u config=%u win_slot=%u "
              "width/height:%dx%d",
              display, config, win_slot, width, height);
#endif

  if (win_slot >= MAX_WL_EGL_WINDOWS)
  {
    log_error(
        "h_eglCreateWindowSurface: ERROR win_slot=%u > MAX_WL_EGL_WINDOWS",
        win_slot);
    C->result = 0;
    return;
  }

  struct wl_egl_window *win = proxy_wl_egl_windows[win_slot];

  if (!win)
  {
    log_error("h_eglCreateWindowSurface: no wl_egl_window win_slot=%u",
              win_slot);

    C->result = 0;
    return;
  }

#ifdef DEBUG
  log_console("wl_egl_window=%p surf=%p w=%d h=%d",
              proxy_wl_egl_windows[win_slot],
              proxy_wl_egl_windows[win_slot]->surface,
              proxy_wl_egl_windows[win_slot]->width,
              proxy_wl_egl_windows[win_slot]->height);

  log_console("h_eglCreateWindowSurface (wl_egl_window_create): "
              "proxy_wl_egl_windows[win_slot]=%p", // real_surf=%p (%dx%d)",
              proxy_wl_egl_windows[win_slot]); //, real_surf, width, height);
#endif

  const EGLint *attr = attr_sz ? (const EGLint *)dp(C->data_offset) : NULL;

  bool needs_create_new_egl_surface = true;
  EGLSurface real = EGL_NO_SURFACE;

  if (needs_create_new_egl_surface)
    real = eglCreateWindowSurface(DISP(display), CFG(config),
                                  (EGLNativeWindowType)win, attr);

  uint32_t si = (real != EGL_NO_SURFACE)
                    ? TABLE_ALLOC(egl_surfaces, EGL_BRIDGE_MAX_SURFACES, real,
                                  C->client_pid)
                    : 0;
  egl_surfaces[si].owner_pid = C->client_pid;
  C->result = si;

  if (real == EGL_NO_SURFACE)
  {
    log_error("h_eglCreateWindowSurface: FAILED err=0x%04x", eglGetError());
#ifdef DEBUG_ABORT_ON_GL_ERROR
    abort();
#endif
  }
#ifdef DEBUG
  else
    log_console("h_eglCreateWindowSurface: OK EGLSurface=%p si: %d", real, si);
#endif

  (void)D;
}

void h_eglCreatePbufferSurface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t ci = ar_u32(&r);
  uint32_t attr_sz = ar_u32(&r);
  const EGLint *attr = attr_sz ? (const EGLint *)dp(C->data_offset) : NULL;
  EGLSurface real = eglCreatePbufferSurface(DISP(di), CFG(ci), attr);
  uint32_t si = real != EGL_NO_SURFACE
                    ? TABLE_ALLOC(egl_surfaces, EGL_BRIDGE_MAX_SURFACES, real,
                                  C->client_pid)
                    : 0;
  egl_surfaces[si].owner_pid = C->client_pid;
  C->result = si;
  (void)D;
}

void h_eglCreatePixmapSurface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t ci = ar_u32(&r);
  EGLNativePixmapType pix =
      (EGLNativePixmapType)(uintptr_t)(uint32_t)ar_u64(&r);
  uint32_t attr_sz = ar_u32(&r);
  const EGLint *attr = attr_sz ? (const EGLint *)dp(C->data_offset) : NULL;
  EGLSurface real = eglCreatePixmapSurface(DISP(di), CFG(ci), pix, attr);
  uint32_t si = real != EGL_NO_SURFACE
                    ? TABLE_ALLOC(egl_surfaces, EGL_BRIDGE_MAX_SURFACES, real,
                                  C->client_pid)
                    : 0;
  egl_surfaces[si].owner_pid = C->client_pid;
  C->result = si;
  (void)D;
}

void h_eglDestroySurface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t si = ar_u32(&r);

  EGLBoolean ok = eglDestroySurface(DISP(di), SURF(si));
  if (ok)
    TABLE_FREE(egl_surfaces, si);
  egl_surfaces[si].owner_pid = 0;
  C->result = ok;

  (void)D;
}

void h_eglQuerySurface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t si = ar_u32(&r);
  EGLint attr = ar_i32(&r);
  EGLint val = 0;
  EGLBoolean ok = eglQuerySurface(DISP(di), SURF(si), attr, &val);
  C->result = ((uint64_t)(uint32_t)val) | ((uint64_t)ok << 32);
  (void)D;
}

void h_eglSurfaceAttrib(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t si = ar_u32(&r);
  EGLint attr = ar_i32(&r);
  EGLint val = ar_i32(&r);
  C->result = eglSurfaceAttrib(DISP(di), SURF(si), attr, val);
  (void)D;
}

void h_eglBindTexImage(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t si = ar_u32(&r);
  EGLint buf = ar_i32(&r);
  C->result = eglBindTexImage(DISP(di), SURF(si), buf);
  (void)D;
}

void h_eglReleaseTexImage(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t si = ar_u32(&r);
  EGLint buf = ar_i32(&r);
  C->result = eglReleaseTexImage(DISP(di), SURF(si), buf);
  (void)D;
}

void h_eglCopyBuffers(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t si = ar_u32(&r);
  EGLNativePixmapType tgt =
      (EGLNativePixmapType)(uintptr_t)(uint32_t)ar_u64(&r);
  C->result = eglCopyBuffers(DISP(di), SURF(si), tgt);
  (void)D;
}

void h_eglCreateContext(BridgeCtrl *C, uint8_t *D)
{
  AR(r);

  uint32_t di = ar_u32(&r);
  uint32_t ci = ar_u32(&r);
  uint32_t share_idx = ar_u32(&r);
  uint32_t attr_sz = ar_u32(&r);

  const EGLint *attr = attr_sz ? (const EGLint *)dp(C->data_offset) : NULL;

  EGLContext share_real = share_idx ? CTX(share_idx) : EGL_NO_CONTEXT;

  EGLDisplay dpy_real = DISP(di);
  EGLConfig cfg_real = CFG(ci);

#ifdef DEBUG
  log_console("h_eglCreateContext (tid=%ld):"
              "    handles: dpy=%u cfg=%u share=%u"
              "    real:    dpy=%p cfg=%p share=%p",
              (long)syscall(SYS_gettid), di, ci, share_idx, dpy_real, cfg_real,
              share_real);
#endif

  EGLContext real;

  real = eglCreateContext(dpy_real, cfg_real, share_real, attr);

#ifdef DEBUG
  log_console("h_eglCreateContext: eglCreateContext returned real=%p", real);
#endif

  uint32_t xi = (real != EGL_NO_CONTEXT)
                    ? TABLE_ALLOC(egl_contexts, EGL_BRIDGE_MAX_CONTEXTS, real,
                                  C->client_pid)
                    : 0;

#ifdef DEBUG
  log_console("h_eglCreateContext: allocated ctx index=%u", xi);

  if (attr)
  {
    for (int i = 0; attr[i] != EGL_NONE; i += 2)
    {
      log_console("h_eglCreateContext: ctx attr[%d]: 0x%x = %d", i / 2, attr[i],
                  attr[i + 1]);
    }
  }

  for (int i = 1; i < EGL_BRIDGE_MAX_CONTEXTS; i++)
  {
    if (egl_contexts[i].handle)
    {
      log_console("h_eglCreateContext: ctx[%d]: handle=%p owner=%d", i,
                  egl_contexts[i].handle, egl_contexts[i].owner_pid);
    }
  }
#endif

  C->result = xi;
  (void)D;
}

void h_eglDestroyContext(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t xi = ar_u32(&r);
#ifdef DEBUG
  log_console("h_eglDestroyContext (tid=%ld) di: %d xi: %d", di, xi,
              syscall(SYS_gettid));
#endif

  EGLBoolean ok = eglDestroyContext(DISP(di), CTX(xi));
  if (ok)
    TABLE_FREE(egl_contexts, xi);
  egl_contexts[xi].owner_pid = 0;
  C->result = ok;

  (void)D;
}

void h_eglMakeCurrent(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t display = ar_u32(&r);
  uint32_t draw = ar_u32(&r);
  uint32_t read = ar_u32(&r);
  uint32_t context = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  EGLDisplay dpy_real = DISP(display);
  EGLSurface draw_real = SURF(draw);
  EGLSurface read_real = SURF(read);
  EGLContext ctx_real = CTX(context);

  log_console("h_eglMakeCurrent (tid=%ld):\n"
              "    handles: display=%u draw=%u read=%u ctx=%u\n"
              "    real:    display=%p draw=%p read=%p ctx=%p",
              (long)syscall(SYS_gettid), display, draw, read, context, dpy_real,
              draw_real, read_real, ctx_real);
#endif

  C->result =
      eglMakeCurrent(DISP(display), SURF(draw), SURF(read), CTX(context));
  (void)D;

  if (C->result)
  {
    g_current_ctx = context;
#ifdef DEBUG_VERBOSE
    log_console("h_eglMakeCurrent: g_current_ctx now set to:%d", g_current_ctx);
#endif
  }
  else
  {
    log_error("h_eglMakeCurrent: C->result", C->result);
  }

  if (g_current_ctx >= MAX_CONTEXTS)
    log_error("h_eglMakeCurrent: ctx:%u > MAX_CONTEXTS", g_current_ctx);

#ifdef DEBUG_VERBOSE
  log_console("h_eglMakeCurrent result:%lld (err=0x%x)", C->result,
              eglGetError()); // EGL_SUCCESS = 0x3000

  EGLContext newCtx = eglGetCurrentContext();
  EGLSurface newDraw = eglGetCurrentSurface(EGL_DRAW);
  EGLSurface newRead = eglGetCurrentSurface(EGL_READ);

  log_console("h_eglMakeCurrent AFTER: ok=%lld ctx=%p draw=%p read=%p",
              C->result, newCtx, newDraw, newRead);

  log_console("[h_eglMakeCurrent ctx table:");
  for (int i = 1; i < EGL_BRIDGE_MAX_CONTEXTS; i++)
  {
    if (egl_contexts[i].handle)
    {
      log_console("    ctx[%d] = %p owner=%d", i, egl_contexts[i].handle,
                  egl_contexts[i].owner_pid);
    }
  }
#endif
}

void h_eglGetCurrentContext(BridgeCtrl *C, uint8_t *D)
{
  EGLContext real = eglGetCurrentContext();
  uint32_t xi = 0;
  if (real != EGL_NO_CONTEXT)
    for (uint32_t i = 1; i <= EGL_BRIDGE_MAX_CONTEXTS; i++)
      if (egl_contexts[i].handle == real)
      {
        xi = i;
        break;
      }
  C->result = xi;
#ifdef DEBUG_VERBOSE
  log_console("h_eglGetCurrentContext (tid=%ld) real:%p returned:%p",
              (long)syscall(SYS_gettid), real, xi);
#endif
  (void)D;
}

void h_eglGetCurrentSurface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  EGLint readdraw = ar_i32(&r);
  EGLSurface real = eglGetCurrentSurface(readdraw);
  uint32_t si = 0;
  if (real != EGL_NO_SURFACE)
    for (uint32_t i = 1; i <= EGL_BRIDGE_MAX_SURFACES; i++)
      if (egl_surfaces[i].handle == real)
      {
        si = i;
        break;
      }
  C->result = si;
  (void)D;
}

void h_eglGetCurrentDisplay(BridgeCtrl *C, uint8_t *D)
{
  EGLDisplay real = eglGetCurrentDisplay();
  uint32_t di = 0;
  if (real != EGL_NO_DISPLAY)
    for (uint32_t i = 1; i <= EGL_BRIDGE_MAX_DISPLAYS; i++)
      if (egl_displays[i].handle == real)
      {
        di = i;
        break;
      }
  C->result = di;
  (void)D;
}

void h_eglQueryContext(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t xi = ar_u32(&r);
  EGLint attr = ar_i32(&r);
  EGLint val = 0;
  eglQueryContext(DISP(di), CTX(xi), attr, &val);
  C->result = (uint64_t)(int64_t)val;
  (void)D;
}

void h_eglSwapBuffers(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  uint32_t si = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_eglSwapBuffers (tid=%ld) di=%d si=%d display=%p egl surf=%p "
              "before swap: "
              "current ctx=%p display=%p draw=%p read=%p wl_display fd=%d",
              syscall(SYS_gettid), di, si, DISP(di), SURF(si),
              eglGetCurrentContext(), eglGetCurrentDisplay(),
              eglGetCurrentSurface(EGL_DRAW), eglGetCurrentSurface(EGL_READ),
              wl_display_get_fd(proxy_wl_display));

  EGLBoolean ok = eglSwapBuffers(DISP(di), SURF(si));
  C->result = ok;

  EGLint err = eglGetError();
  log_console("eglSwapBuffers -> result=%d err=0x%04x", (EGLBoolean)ok, err);

#ifdef DEBUG_ABORT_ON_GL_ERROR
  if (err != EGL_SUCCESS)
    abort();
#endif

  EGLint surf_w = 0, surf_h = 0;
  eglQuerySurface(DISP(di), SURF(si), EGL_WIDTH, &surf_w);
  eglQuerySurface(DISP(di), SURF(si), EGL_HEIGHT, &surf_h);
  log_console("eglSwapBuffers: EGL surface size = %dx%d", surf_w, surf_h);
  GLint vp[4];
  glGetIntegerv(GL_VIEWPORT, vp);
  log_console("eglSwapBuffers: GL viewport = %d,%d %dx%d", vp[0], vp[1], vp[2],
              vp[3]);
  GLint fb = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
  log_console("eglSwapBuffers: FB = %d", fb);
#else
  C->result = eglSwapBuffers(DISP(di), SURF(si));
#endif
  (void)D;
}

void h_eglSwapInterval(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t di = ar_u32(&r);
  EGLint iv = ar_i32(&r);
  C->result = eglSwapInterval(DISP(di), iv);
  (void)D;
}

void h_eglWaitGL(BridgeCtrl *C, uint8_t *D)
{
  C->result = eglWaitGL();
  (void)D;
}
void h_eglWaitNative(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  C->result = eglWaitNative(ar_i32(&r));
  (void)D;
}
void h_eglWaitClient(BridgeCtrl *C, uint8_t *D)
{
  C->result = eglWaitClient();
  (void)D;
}

void h_eglBindAPI(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  C->result = eglBindAPI(ar_u32(&r));
  (void)D;
}
void h_eglQueryAPI(BridgeCtrl *C, uint8_t *D)
{
  C->result = eglQueryAPI();
  (void)C;
  (void)D;
}

static inline void *resolve_proc(const char *name)
{
  /* 1. GLES table */
  ProcEntry *pe = find_proc(name);
  if (pe)
    return pe->dispatch;

  /* 2. EGL extension table */
  EGLProcEntry *ee = egl_find_proc(name);
  if (ee)
    return ee->dispatch;

  /* 3. Driver fallback */
  return eglGetProcAddress(name);
}

void h_eglGetProcAddress(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  uint32_t idx = ar_u32(&r);
  uint32_t egl = ar_u32(&r);
  const char *name = (const char *)(r.buf + r.pos);

#ifdef DEBUG_EGL_GETPROC
  log_console("h_eglGetProcAddress: name=%s idx=%u egl=%u", name, idx, egl);
#endif

  if (egl == 0)
  {

    /* 1. Check if already loaded */
    for (uint32_t i = 0; i < gl_dyn_count; i++)
    {
      if (strcmp(gl_dyn_table[i].name, name) == 0)
      {
#ifdef DEBUG_EGL_GETPROC
        log_console("h_eglGetProcAddress: (gl) already loaded at idx=%u", i);
#endif
        C->result = i;
        return;
      }
    }
  }
  else
  {
    for (uint32_t i = 0; i < egl_dyn_count; i++)
    {
      if (strcmp(egl_dyn_table[i].name, name) == 0)
      {
#ifdef DEBUG_EGL_GETPROC
        log_console("h_eglGetProcAddress: (egl) already loaded at idx=%u", i);
#endif
        C->result = i;
        return;
      }
    }
  }

  /* 2. Load new function */
  void *p = eglGetProcAddress(name);

#ifdef DEBUG_EGL_GETPROC
  log_console("h_eglGetProcAddress: driver returned p=%p", p);
#endif

  if (!p)
  {
#ifdef DEBUG_EGL_GETPROC
    log_console("h_eglGetProcAddress: NOT FOUND - returning 0");
#endif
    C->result = 0;
    return;
  }

  if (idx >= MAX_DYNAMIC_FUNCS)
  {
    log_error("h_eglGetProcAddress: egl dyn table full idx: %u >= "
              "MAX_DYNAMIC_FUNCS: %u",
              idx, MAX_DYNAMIC_FUNCS);
    C->result = 0;
    return;
  }

  if (egl == 0)
  {
    gl_dyn_table[idx].real = p;
    strncpy(gl_dyn_table[idx].name, name, sizeof(gl_dyn_table[idx].name) - 1);
    gl_dyn_table[idx].name[sizeof(gl_dyn_table[idx].name) - 1] = '\0';

#ifdef DEBUG_EGL_GETPROC
    log_console("h_eglGetProcAddress: stored name=\"%s\" real=%p idx=%u",
                gl_dyn_table[idx].name, gl_dyn_table[idx].real, idx);
#endif
  }
  else
  {
    egl_dyn_table[idx].real = p;
    strncpy(egl_dyn_table[idx].name, name, sizeof(egl_dyn_table[idx].name) - 1);
    egl_dyn_table[idx].name[sizeof(egl_dyn_table[idx].name) - 1] = '\0';

#ifdef DEBUG_EGL_GETPROC
    log_console("h_eglGetProcAddress: stored name=\"%s\" real=%p e-idx=%u",
                egl_dyn_table[idx].name, egl_dyn_table[idx].real, idx);
#endif
  }

  C->result = idx;
}

void h_eglCreateImageKHR(BridgeCtrl *C, uint8_t *D)
{
  ArgReader r = ar_init(C->args, C->args_len);

  EGLDisplay dpy = (EGLDisplay)(uintptr_t)ar_u64(&r);
  EGLContext ctx = (EGLContext)(uintptr_t)ar_u64(&r);
  EGLenum target = ar_u32(&r);
  EGLClientBuffer buffer = (EGLClientBuffer)(uintptr_t)ar_u64(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_eglCreateImageKHR: dpy=%p ctx=%p target=0x%04x buffer=%p", dpy,
              ctx, target, buffer);
#endif

  EGLAttrib attribs[64];
  int ai = 0;
  while (1)
  {
    EGLAttrib k = ar_u64(&r);
    attribs[ai++] = k;
    if (k == EGL_NONE)
      break;
    attribs[ai++] = ar_u64(&r);
  }

  EGLImage real = eglCreateImageKHR(dpy, ctx, target, buffer, attribs);
  C->result = (uint64_t)(uintptr_t)real;
}

void h_eglDestroyImageKHR(BridgeCtrl *C, uint8_t *D)
{
  ArgReader r = ar_init(C->args, C->args_len);

  EGLDisplay dpy = (EGLDisplay)(uintptr_t)ar_u64(&r);
  EGLImage image = (EGLImage)(uintptr_t)ar_u64(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_eglDestroyImageKHR: dpy=%p image=%p", dpy, image);
#endif

  EGLBoolean ok = eglDestroyImageKHR(dpy, image);

  C->result = ok ? 1 : 0;
}
