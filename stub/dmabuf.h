#pragma once

/*
 * stub/dmabuf_present.h  —  aarch64
 *
 * Receives dma_buf fds from the 32-bit proxy, creates zwp_linux_dmabuf_v1
 * wl_buffers from them, and presents completed frames to the real wl_surface
 * the 64-bit application created via the real libwayland-client.
 *
 */
#include "../include/gles_bridge_protocol.h"

#include <stdint.h>
#include <wayland-client.h>

/* Discovers zwp_linux_dmabuf_v1 on display and creates DMABUF_NUM_BUFFERS
 * wl_buffers from the received dma_buf fds.
 *
 * fd_sock  : stub end of the Unix-socket pair (from bridge_dmabuf_fd_sock())
 * display  : the application's real wl_display
 *
 * Returns 0 on success, -1 on failure. */
int dmabuf_present_init(int fd_sock, struct wl_display *display);

/* Record the wl_surface + dimensions the app handed to eglCreateWindowSurface
 */
void dmabuf_present_set_surface(struct wl_surface *surf, int32_t width,
                                int32_t height);

/* ── Per-frame presentation ──────────────────────────────────────────────── */

/* Called from eglSwapBuffers (stub side) after the proxy reports which frame
 * index is ready.
 *
 * frame_index       : 0..DMABUF_NUM_BUFFERS-1, as returned by the proxy
 * released_mask_out : receives a bitmask of frames the compositor has
 *                     released since the last call; pass to proxy on the
 *                     next eglSwapBuffers so it can recycle those frames.
 */
void dmabuf_present_frame(int frame_index, uint32_t *released_mask_out);

/* ── Teardown ────────────────────────────────────────────────────────────── */
void dmabuf_present_destroy(void);

/* ── State accessors used by egl_stub.c ──────────────────────────────────── */
extern int g_dmabuf_present_ready;     /* non-zero after successful init */
extern uint32_t g_dmabuf_release_mask; /* releases to piggyback on next swap */
