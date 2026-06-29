#include "../proxy.h"

#include <stdbool.h>

#define LOG_PREFIX "[proxy/wl_egl]"

struct wl_egl_window *proxy_wl_egl_windows[MAX_WL_EGL_WINDOWS];
pid_t proxy_wl_egl_window_owner[MAX_WL_EGL_WINDOWS];

bool demo_wl_egl_window_consumed = false;

void h_wl_egl_window_create(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  uint32_t surface_id = ar_u32(&r); // from wl_proxy_get_id(surface) in stub
  int width = ar_i32(&r);
  int height = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_wl_egl_window_create: surface_id: %d width: %d height: %d",
              surface_id, width, height);
#endif

  if (proxy_wl_egl_windows[surface_id])
  {
    log_console("h_wl_egl_window_create: WARNING surface_id %u already has a "
                "wl_egl_window",
                surface_id);
  }

  if (surface_id >= PROXY_SURF_MAX)
  {
    log_error("h_wl_egl_window_create: surface_id=%u > PROXY_SURF_MAX=%u",
              surface_id, PROXY_SURF_MAX);
    C->result = 0;
    return;
  }

#ifdef HAVE_OWN_WAYLAND_CLIENT
  if (!g_surfs[surface_id])
  {
    log_console("h_wl_egl_window_create: invalid surface_id=%u", surface_id);
    C->result = 0;
    return;
  }

  // get already stored surface
  struct wl_surface *real_surf = g_surfs[surface_id];
#else
  // in 64-bit libwayland-client - TODO:
  struct wl_surface *real_surf = NULL;
#endif

  struct wl_egl_window *w = wl_egl_window_create(real_surf, width, height);
  if (!w)
  {
    log_console(
        "h_wl_egl_window_create: wl_egl_window_create FAILED surface_id=%u",
        surface_id);
    C->result = 0;
    return;
  }

  if (proxy_wl_egl_windows[surface_id])
  {
    log_console("h_wl_egl_window_create: surf_id=%u already occupied",
                surface_id);

    wl_egl_window_destroy(proxy_wl_egl_windows[surface_id]);
  }

  proxy_wl_egl_windows[surface_id] = w;
  proxy_wl_egl_window_owner[surface_id] = C->client_pid;

  C->result = surface_id;

#ifdef DEBUG_VERBOSE
  log_console("h_wl_egl_window_create: created window into slot(surface_id)=%u "
              "real=%p (%dx%d)",
              surface_id, w, width, height);
#endif
}

void h_wl_egl_window_resize(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  uint32_t slot = ar_u32(&r);
  int width = ar_i32(&r);
  int height = ar_i32(&r);
  int dx = ar_i32(&r);
  int dy = ar_i32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_wl_egl_window_resize: slot=%d width=%d height=%d dx=%d dy=%d",
              slot, width, height, dx, dy);
#endif

  if (slot >= MAX_WL_EGL_WINDOWS)
  {
    log_error("h_wl_egl_window_resize: slot=%u > MAX_WL_EGL_WINDOWS", slot);
    return;
  }

  if (!proxy_wl_egl_windows[slot])
  {
    log_error("h_wl_egl_window_resize: invalid slot=%u egl_window=%p", slot,
              proxy_wl_egl_windows[slot]);
    return;
  }

  struct wl_egl_window *w = proxy_wl_egl_windows[slot];

  wl_egl_window_resize(w, width, height, dx, dy);

#ifdef DEBUG_VERBOSE
  log_console(
      "h_wl_egl_window_resize: slot=%u wl_egl_window=%p -> %dx%d (%d,%d)", slot,
      w, width, height, dx, dy);
#endif
}

void h_wl_egl_window_destroy(BridgeCtrl *C, uint8_t *D)
{
  (void)D;
  AR(r);

  uint32_t slot = ar_u32(&r);

#ifdef DEBUG_VERBOSE
  log_console("h_wl_egl_window_destroy: slot=%d ", slot);
#endif

  if (slot >= MAX_WL_EGL_WINDOWS || !proxy_wl_egl_windows[slot])
  {
    log_console("h_wl_egl_window_destroy: invalid slot=%u", slot);
    return;
  }

  struct wl_egl_window *w = proxy_wl_egl_windows[slot];

  wl_egl_window_destroy(w);
  proxy_wl_egl_windows[slot] = NULL;
  proxy_wl_egl_window_owner[slot] = 0;

#ifdef DEBUG_VERBOSE
  log_console("h_wl_egl_window_destroy: slot=%u destroyed", slot);
#endif
}
