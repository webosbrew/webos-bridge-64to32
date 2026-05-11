#pragma once

#include "../proxy.h"

#define MAX_WL_EGL_WINDOWS 64

extern struct wl_egl_window *proxy_wl_egl_windows[MAX_WL_EGL_WINDOWS];
extern pid_t proxy_wl_egl_window_owner[MAX_WL_EGL_WINDOWS];

extern bool demo_wl_egl_window_consumed;

void h_wl_egl_window_create(BridgeCtrl *C, uint8_t *D);
void h_wl_egl_window_resize(BridgeCtrl *C, uint8_t *D);
void h_wl_egl_window_destroy(BridgeCtrl *C, uint8_t *D);
