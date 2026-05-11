#pragma once

#include <stdbool.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "wayland-util.h"
#include "webos-shell-32.h"

#define PROXY_SURF_MAX 16
#define PROXY_SURF_NEXT 1

extern struct wl_surface *g_surfs[PROXY_SURF_MAX];
extern struct wl_shell_surface *g_shell_surfs[PROXY_SURF_MAX];
extern struct wl_webos_shell_surface *g_webos_shell_surfaces[PROXY_SURF_MAX];

extern pid_t g_surfs_owner[PROXY_SURF_MAX];
extern pid_t g_shell_surfs_owner[PROXY_SURF_MAX];
extern pid_t g_webos_shell_surfaces_owner[PROXY_SURF_MAX];

extern struct wl_display *proxy_wl_display;
extern struct wl_compositor *proxy_wl_compositor;
extern struct wl_shell *proxy_wl_shell;
extern struct wl_webos_shell *proxy_wl_webos_shell;
extern struct wl_registry *proxy_wl_registry;

extern bool demo_surface_consumed;

/* ── Connect to Wayland and discover globals  ── */
void proxy_wayland_init(void);

/* ── h_wl_display_connect ──────────────────────────────────────────────── */
void h_wl_display_connect(BridgeCtrl *C, uint8_t *D);
void h_wl_display_disconnect(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_compositor_create_surface ────────────────────────────────────── */
void h_wl_compositor_create_surface(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_shell_get_shell_surface ──────────────────────────────────────── */
void h_wl_shell_get_shell_surface(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_shell_surface_set_toplevel ────────────────────────────────────── */
void h_wl_shell_surface_set_toplevel(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_webos_shell_get_shell_surface ─────────────────────────────────── */
void h_wl_webos_shell_get_shell_surface(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_webos_set_property ────────────────────────────────────────────── */
void h_wl_webos_set_property(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_surface_commit ────────────────────────────────────────────────── */
void h_wl_surface_commit(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_surface_destroy ───────────────────────────────────────────────── */
void h_wl_surface_destroy(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_proxy_destroy ─────────────────────────────────────────────────── */
void h_wl_proxy_destroy(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_roundtrip ─────────────────────────────────────────────────────── */
void h_wl_roundtrip(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_flush ─────────────────────────────────────────────────────────── */
void h_wl_flush(BridgeCtrl *C, uint8_t *D);

/* ── h_wl_webos_set_state ────────────────────────────────────── */
void h_wl_webos_shell_surface_set_state(BridgeCtrl *C, uint8_t *D);
