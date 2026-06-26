#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#define LOG_PREFIX "[wl_egl_stub]"
#include "../bridge/shared_util.h"
#include "bridge_core.h"

#define MAX_WL_EGL_WINDOWS 64

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

WL_EXPORT struct wl_egl_window *wl_egl_window_create(struct wl_surface *surface,
                                                     int width, int height)
{
  if (!surface || width <= 0 || height <= 0)
    return NULL;

  struct wl_egl_window *w = calloc(1, sizeof(*w));
  if (!w)
    return NULL;

  uint32_t proxy_id = wl_proxy_get_id((struct wl_proxy *)surface);

#ifdef DEBUG
  log_console("1. wl_egl_window_create - surface %p, width %d, height %d proxy id: %d",
              surface, width, height, proxy_id);
#endif

  w->surface = surface;
  w->width = width;
  w->height = height;
  w->attached_width = width;
  w->attached_height = height;

  /* bridge call: OP_wl_egl_window_create(surf_id, width, height) */
  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_wl_egl_window_create);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, proxy_id); /* surf_id */
  aw_i32(&W, width);
  aw_i32(&W, height);
  C->args_len = W.pos;

  uint32_t proxy_slot = BRIDGE_SEND_CALL();

  if (!proxy_slot)
  {
    free(w);
    return NULL;
  }

  w->slot = proxy_slot;

  return (struct wl_egl_window *)w;
}

WL_EXPORT void wl_egl_window_destroy(struct wl_egl_window *egl_window)
{
#ifdef DEBUG_WAYLAND
  log_console("wl_egl_window_destroy");
#endif

  struct wl_egl_window *w = (void *)egl_window;
  if (!w)
    return;

#ifdef DEBUG_WAYLAND
  log_console("wl_egl_window_destroy: egl_window: %p w->slot=%d", egl_window,
              w->slot);
#endif

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_wl_egl_window_destroy);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, w->slot);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();

  free(w);
}

WL_EXPORT void wl_egl_window_resize(struct wl_egl_window *egl_window, int width,
                                    int height, int dx, int dy)
{
#ifdef DEBUG_VERBOSE
  log_console("wl_egl_window_resize");
#endif

  struct wl_egl_window *w = (void *)egl_window;
  if (!w)
    return;

#ifdef DEBUG_VERBOSE
  log_console("wl_egl_window_resize: egl_window: %p w->surface: %p width: %d "
              "height: %d dy: %d",
              egl_window, w->surface, width, height, dy);
#endif

  w->width = width;
  w->height = height;
  w->dx = dx;
  w->dy = dy;

  BRIDGE_BEGIN();
  BridgeCtrl *C = BRIDGE_CTRL();
  setup_scalar(OP_wl_egl_window_resize);

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, w->slot);
  aw_i32(&W, width);
  aw_i32(&W, height);
  aw_i32(&W, dx);
  aw_i32(&W, dy);
  C->args_len = W.pos;

  BRIDGE_SEND_VOID();
}

WL_EXPORT void wl_egl_window_get_attached_size(struct wl_egl_window *egl_window,
                                               int *width, int *height)
{
  struct wl_egl_window *w = (void *)egl_window;
  if (!w)
    return;

  if (width)
    *width = w->attached_width;
  if (height)
    *height = w->attached_height;
}
