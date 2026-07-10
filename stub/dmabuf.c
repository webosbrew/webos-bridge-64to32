/*
 * stub/dmabuf_present.c  —  aarch64
 *
 * Hand-rolled minimal zwp_linux_dmabuf_v1 binding + frame presentation.
 * No dependency on wayland-scanner or protocol XML files.
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <wayland-client.h>

#define LOG_PREFIX "[stub/dmabuf]"
#include "../bridge/shared_util.h"
#include "../include/gles_bridge_protocol.h"
#include "dmabuf.h"

/* ════════════════════════════════════════════════════════════════════════════
 * Minimal zwp_linux_dmabuf_v1 / zwp_linux_buffer_params_v1 interfaces
 * ════════════════════════════════════════════════════════════════════════════
 */

/* Forward declaration needed for the types arrays */
static const struct wl_interface zwp_linux_buffer_params_v1_interface;

/* zwp_linux_buffer_params_v1 ─────────────────────────────────────────────── */
/* opcode 3 (create_immed) returns a new wl_buffer */
static const struct wl_interface *_params_create_immed_types[] = {
    &wl_buffer_interface,
};

static const struct wl_message _params_requests[] = {
    /* 0 */ {"destroy", "", NULL},
    /* 1 */ {"add", "huuuuu", NULL}, /* h = fd */
    /* 2 */ {"create", "iiuu", NULL},
    /* 3 */ {"create_immed", "niiuu", _params_create_immed_types},
};

static const struct wl_interface zwp_linux_buffer_params_v1_interface = {
    "zwp_linux_buffer_params_v1", 1, 4, _params_requests, 0, NULL};

/* zwp_linux_dmabuf_v1 ────────────────────────────────────────────────────── */
static const struct wl_interface *_dmabuf_create_params_types[] = {
    &zwp_linux_buffer_params_v1_interface,
};

static const struct wl_message _dmabuf_requests[] = {
    /* 0 */ {"destroy", "", NULL},
    /* 1 */ {"create_params", "n", _dmabuf_create_params_types},
};

static const struct wl_interface zwp_linux_dmabuf_v1_interface = {
    "zwp_linux_dmabuf_v1", 3, 2, _dmabuf_requests, 0, NULL};

/* ── Inline helpers ──────────────────────────────────────────────────────── */
static inline struct wl_proxy *_create_params(struct wl_proxy *dmabuf)
{
  /* create_params returns a new zwp_linux_buffer_params_v1 */
  return wl_proxy_marshal_constructor(
      dmabuf, 1, &zwp_linux_buffer_params_v1_interface, NULL);
}

static inline void _params_add(struct wl_proxy *params, int32_t fd,
                               uint32_t plane, uint32_t offset, uint32_t stride,
                               uint32_t mod_hi, uint32_t mod_lo)
{
  /* opcode 1: "huuuuu" — 'h' marshalled as ancillary fd by wayland-client */
  wl_proxy_marshal(params, 1, fd, plane, offset, stride, mod_hi, mod_lo);
}

static inline struct wl_proxy *_params_create_immed(struct wl_proxy *params,
                                                    int32_t w, int32_t h,
                                                    uint32_t format,
                                                    uint32_t flags)
{
  /* opcode 3: "niiuu" — returns new wl_buffer */
  return wl_proxy_marshal_constructor(params, 3, &wl_buffer_interface, NULL, w,
                                      h, format, flags);
}

/* ════════════════════════════════════════════════════════════════════════════
 * Module state
 * ════════════════════════════════════════════════════════════════════════════
 */
int g_dmabuf_present_ready = 0;
uint32_t g_dmabuf_release_mask = 0;

static struct wl_display *g_display = NULL;
static struct wl_surface *g_surface = NULL;
static struct wl_proxy *g_linux_dmabuf = NULL;

static int32_t g_surf_w = 0;
static int32_t g_surf_h = 0;

/* Per-frame wl_buffer handles */
static struct wl_proxy *g_wl_buffers[DMABUF_NUM_BUFFERS];
static DmaBufFrameInfo g_frame_info[DMABUF_NUM_BUFFERS];
static int g_frame_fds[DMABUF_NUM_BUFFERS];

/* ── wl_buffer::release ──────────────────────────────────────────────────── */
typedef struct
{
  int idx;
} BufData;
static BufData g_buf_data[DMABUF_NUM_BUFFERS];

static void _on_buffer_release(void *data, struct wl_buffer *buf)
{
  (void)buf;
  BufData *d = (BufData *)data;
  g_dmabuf_release_mask |= (1u << d->idx);
#ifdef DEBUG_WAYLAND
  log_console("dmabuf: wl_buffer[%d] released", d->idx);
#endif
}

static const struct wl_buffer_listener _buf_listener = {
    .release = _on_buffer_release,
};

/* ── Registry: find zwp_linux_dmabuf_v1 ─────────────────────────────────── */
static void _on_global(void *data, struct wl_registry *reg, uint32_t name,
                       const char *iface, uint32_t ver)
{
  (void)data;
  if (strcmp(iface, "zwp_linux_dmabuf_v1") == 0)
  {
    uint32_t use = ver < 3u ? ver : 3u;
    g_linux_dmabuf = (struct wl_proxy *)wl_registry_bind(
        reg, name, &zwp_linux_dmabuf_v1_interface, use);
#ifdef DEBUG
    log_console("dmabuf: bound zwp_linux_dmabuf_v1 ver=%u", use);
#endif
  }
}
static void _on_global_remove(void *d, struct wl_registry *r, uint32_t n)
{
  (void)d;
  (void)r;
  (void)n;
}

static const struct wl_registry_listener _reg_listener = {
    .global = _on_global,
    .global_remove = _on_global_remove,
};

/* ── Receive fds via SCM_RIGHTS ─────────────────────────────────────────── */
static int recv_fds(int sock, int *fds, int n)
{
  char dummy;
  struct iovec iov = {&dummy, 1};

  size_t cmsg_sz = CMSG_SPACE((size_t)n * sizeof(int));
  char *cbuf = alloca(cmsg_sz);
  memset(cbuf, 0, cmsg_sz);

  struct msghdr msg = {
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_control = cbuf,
      .msg_controllen = cmsg_sz,
  };

  ssize_t r = recvmsg(sock, &msg, 0);
  if (r < 0)
  {
    log_error("dmabuf: recvmsg fds: %s", strerror(errno));
    return -1;
  }
  struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
  if (!cm || cm->cmsg_type != SCM_RIGHTS)
  {
    log_error("dmabuf: recvmsg: no SCM_RIGHTS");
    return -1;
  }
  memcpy(fds, CMSG_DATA(cm), (size_t)n * sizeof(int));
  return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Public API
 * ════════════════════════════════════════════════════════════════════════════
 */

void dmabuf_present_set_surface(struct wl_surface *surf, int32_t width,
                                int32_t height)
{
  g_surface = surf;
  g_surf_w = width;
  g_surf_h = height;
}

int dmabuf_present_init(int fd_sock, struct wl_display *display)
{
  if (g_dmabuf_present_ready)
    return 0;

  g_display = display;

  /* ── Discover zwp_linux_dmabuf_v1 ── */
  struct wl_registry *reg = wl_display_get_registry(display);
  wl_registry_add_listener(reg, &_reg_listener, NULL);
  wl_display_roundtrip(display);
  wl_registry_destroy(reg);

  if (!g_linux_dmabuf)
  {
    log_error("dmabuf: compositor has no zwp_linux_dmabuf_v1");
    return -1;
  }

  /* ── Receive buffer metadata from proxy ── */
  ssize_t meta_sz = (ssize_t)sizeof(g_frame_info);
  if (read(fd_sock, g_frame_info, (size_t)meta_sz) != meta_sz)
  {
    log_error("dmabuf: read metadata: %s", strerror(errno));
    return -1;
  }

  /* ── Receive dma_buf file descriptors ── */
  if (recv_fds(fd_sock, g_frame_fds, DMABUF_NUM_BUFFERS) < 0)
    return -1;

  /* ── Create zwp_linux_buffer_params + wl_buffer for each frame ── */
  for (int i = 0; i < DMABUF_NUM_BUFFERS; i++)
  {
    DmaBufFrameInfo *fi = &g_frame_info[i];

    struct wl_proxy *params = _create_params(g_linux_dmabuf);
    if (!params)
    {
      log_error("dmabuf: create_params failed for frame %d", i);
      return -1;
    }

    _params_add(params, g_frame_fds[i], 0, /* plane index */
                fi->offset, fi->stride, fi->modifier_hi, fi->modifier_lo);

    g_wl_buffers[i] =
        _params_create_immed(params, (int32_t)fi->width, (int32_t)fi->height,
                             fi->format, 0 /* flags */);

    /* params object not needed after create_immed */
    wl_proxy_marshal(params, 0); /* destroy */
    wl_proxy_destroy(params);

    if (!g_wl_buffers[i])
    {
      log_error("dmabuf: create_immed failed for frame %d (err 0x%x)", i,
                eglGetError());
      return -1;
    }

    g_buf_data[i].idx = i;
    wl_buffer_add_listener((struct wl_buffer *)g_wl_buffers[i], &_buf_listener,
                           &g_buf_data[i]);

    /* Initially all buffers are free for the proxy to render into */
    g_dmabuf_release_mask |= (1u << i);

    log_always("dmabuf: frame %-2d  fd=%-3d  %ux%u  stride=%u  fmt=0x%x", i,
               g_frame_fds[i], fi->width, fi->height, fi->stride, fi->format);
  }

  /* flush buffer-creation requests to compositor */
  wl_display_roundtrip(display);

  /* Frame 0 is the first one the proxy will render into; clear its
   * release bit so we don't hand it back to the proxy before it's ready */
  g_dmabuf_release_mask &= ~1u;

  g_dmabuf_present_ready = 1;
  log_always("dmabuf: present init OK — %d frames  %dx%d", DMABUF_NUM_BUFFERS,
             g_frame_info[0].width, g_frame_info[0].height);
  return 0;
}

void dmabuf_present_frame(int frame_index, uint32_t *released_mask_out)
{
  if (!g_surface || !g_wl_buffers[frame_index])
  {
    log_error("dmabuf: present_frame %d: no surface or buffer", frame_index);
    if (released_mask_out)
      *released_mask_out = 0;
    return;
  }

  /* Dispatch any pending Wayland events — this may fire wl_buffer::release
   * callbacks from the compositor for previously presented frames.        */
  wl_display_dispatch_pending(g_display);

  /* Snapshot and clear the accumulated release bitmask */
  uint32_t released = g_dmabuf_release_mask;
  g_dmabuf_release_mask = 0;

  /* The frame we are about to present is no longer "released" */
  released &= ~(1u << frame_index);

  /* Attach, damage, commit */
  wl_surface_attach(g_surface, (struct wl_buffer *)g_wl_buffers[frame_index], 0,
                    0);
  wl_surface_damage_buffer(g_surface, 0, 0, g_surf_w, g_surf_h);
  wl_surface_commit(g_surface);
  wl_display_flush(g_display);

  if (released_mask_out)
    *released_mask_out = released;
}

void dmabuf_present_destroy(void)
{
  for (int i = 0; i < DMABUF_NUM_BUFFERS; i++)
  {
    if (g_wl_buffers[i])
    {
      wl_proxy_marshal(g_wl_buffers[i], 0); /* wl_buffer::destroy */
      wl_proxy_destroy(g_wl_buffers[i]);
      g_wl_buffers[i] = NULL;
    }
    if (g_frame_fds[i] >= 0)
    {
      close(g_frame_fds[i]);
      g_frame_fds[i] = -1;
    }
  }
  if (g_linux_dmabuf)
  {
    wl_proxy_marshal(g_linux_dmabuf, 0); /* destroy */
    wl_proxy_destroy(g_linux_dmabuf);
    g_linux_dmabuf = NULL;
  }
  g_dmabuf_present_ready = 0;
  g_dmabuf_release_mask = 0;
}
