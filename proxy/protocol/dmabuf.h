#pragma once
/*
 * proxy/protocol/dmabuf.h
 *
 * 32-bit proxy side: allocate dma_buf-backed FBOs, render into them,
 * signal the 64-bit stub which frame is ready for presentation.
 *
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <stdint.h>

/* Number of dma_buf frames in the ring (triple-buffer). */
#ifndef DMABUF_NUM_BUFFERS
#define DMABUF_NUM_BUFFERS 3
#endif

/* DRM device to open for dumb-buffer allocation */
#ifndef DRM_DEVICE_PATH
#define DRM_DEVICE_PATH "/dev/dma_buf_unified"
#endif
#ifndef DRM_DEVICE_PATH_FALLBACK
#define DRM_DEVICE_PATH_FALLBACK "/dev/dri/card0"
#endif

/* DRM format fourcc — matches EGL_LINUX_DRM_FOURCC_EXT attrib */
#define DMABUF_FORMAT_XRGB8888 0x34325258u /* DRM_FORMAT_XRGB8888 */
#define DMABUF_FORMAT_ARGB8888 0x34325241u /* DRM_FORMAT_ARGB8888 */
#define DMABUF_FORMAT_ABGR8888 0x34324241u /* DRM_FORMAT_ABGR8888 */
#define DMABUF_MOD_LINEAR 0ULL

typedef enum
{
  DMABUF_FRAME_FREE = 0,
  DMABUF_FRAME_RENDERING,
  DMABUF_FRAME_READY,
} DmaBufFrameState;

typedef struct
{
  int dma_fd;          /* exported dma_buf fd                  */
  uint32_t drm_handle; /* DRM GEM handle (for cleanup)         */
  uint32_t stride;     /* bytes per row                        */
  uint32_t format;     /* DRM fourcc actually used             */
  EGLImageKHR egl_image;
  GLuint rbo; /* renderbuffer backed by EGLImage      */
  GLuint fbo; /* framebuffer with rbo as color attach */
  DmaBufFrameState state;
} DmaBufFrame;

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Called from proxy main() before entering dispatch loop. Stores EGL
 * display/config and the fd-socket for sending dma_buf fds to the stub. */
void dmabuf_proxy_store_egl(EGLDisplay dpy, EGLConfig cfg, int fd_sock);

/* Called from h_eglCreateWindowSurface once dimensions are known.
 * Allocates DMABUF_NUM_BUFFERS frames and sends fds to stub. */
int dmabuf_proxy_init(uint32_t width, uint32_t height);

/* True after dmabuf_proxy_init() succeeds. */
int dmabuf_proxy_is_ready(void);

/* Bind the current rendering FBO (call at the end of h_eglMakeCurrent). */
void dmabuf_proxy_bind_current_fbo(void);

/* Intercept glBindFramebuffer(target, 0): remap to current dma_buf FBO. */
GLuint dmabuf_proxy_remap_fbo0(void);

/* Called from h_eglSwapBuffers.
 *   released_mask : bitmask of frames the compositor released since last swap
 *   Returns the frame index the stub should present (0..DMABUF_NUM_BUFFERS-1).
 */
int dmabuf_proxy_swap(uint32_t released_mask);

/* Shared frame array (read-only outside dmabuf_proxy.c). */
extern DmaBufFrame g_dmabuf_frames[DMABUF_NUM_BUFFERS];
extern int g_dmabuf_current;
