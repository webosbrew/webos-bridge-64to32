/*
 * proxy/protocol/dmabuf_proxy.c
 *
 * Allocates DRM dumb buffers, wraps them as EGLImages backed FBOs,
 * and sends the dma_buf fds to the 64-bit stub via a Unix socket.
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <GLES3/gl32.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#define LOG_PREFIX "[proxy/dmabuf]"
#include "../../bridge/shared_util.h"
#include "../../include/gles_bridge_protocol.h"
#include "dmabuf.h"

struct _dma_heap_alloc
{
  uint64_t len;
  uint32_t fd;
  uint32_t fd_flags;
  uint64_t heap_flags;
};

#define DMA_HEAP_IOCTL_ALLOC ((unsigned long)0xC0184800UL)

/* Device path */
#define DMA_BUF_UNIFIED_PATH "/dev/dma_buf_unified"

/* Stride alignment in bytes */
#define DMABUF_STRIDE_ALIGN 64u

/* ── Module-private fd (replaces g_drm_fd) ─────────────────────────────── */
static int g_heap_fd = -1;

/* ── Raw DRM ioctls ───────────────────────── */
#define _DRM_IOC_BASE 'd'
#define _DRM_IOWR(n, t) _IOWR(_DRM_IOC_BASE, n, t)

#define DRM_IOCTL_MODE_CREATE_DUMB _DRM_IOWR(0xB2, struct _dumb_create)
#define DRM_IOCTL_PRIME_HANDLE_TO_FD _DRM_IOWR(0x2d, struct _prime_handle)
#define DRM_IOCTL_MODE_DESTROY_DUMB _DRM_IOWR(0xB4, struct _dumb_destroy)

struct _dumb_create
{
  uint32_t h, w, bpp, flags, handle, pitch;
  uint64_t size;
};
struct _prime_handle
{
  uint32_t handle, flags;
  int32_t fd;
};
struct _dumb_destroy
{
  uint32_t handle, pad;
};

#define DRM_PRIME_FLAGS (O_RDWR | O_CLOEXEC)

/* ── EGL extension function pointers ────────────────────────────────────── */
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;
typedef void (*PFNGLEGLIMAGETARGETRBO)(GLenum, GLeglImageOES);
static PFNGLEGLIMAGETARGETRBO p_glEGLImageTargetRenderbufferStorageOES;

static void load_ext(void)
{
  p_eglCreateImageKHR =
      (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
  p_eglDestroyImageKHR =
      (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
  p_glEGLImageTargetRenderbufferStorageOES =
      (PFNGLEGLIMAGETARGETRBO)eglGetProcAddress(
          "glEGLImageTargetRenderbufferStorageOES");

  if (!p_eglCreateImageKHR || !p_eglDestroyImageKHR ||
      !p_glEGLImageTargetRenderbufferStorageOES)
    log_error(
        "dmabuf: missing EGL_EXT_image_dma_buf_import or GL_OES_EGL_image");
}

/* ── Module state ────────────────────────────────────────────────────────── */
DmaBufFrame g_dmabuf_frames[DMABUF_NUM_BUFFERS];
int g_dmabuf_current = 0;

static EGLDisplay g_dpy = EGL_NO_DISPLAY;
static EGLConfig g_cfg = NULL;
static int g_fd_sock = -1;
static int g_drm_fd = -1;
static int g_ready = 0;
static uint32_t g_width = 0;
static uint32_t g_height = 0;

/* ── Store EGL handles before dispatch loop ──────────────────────────────── */
void dmabuf_proxy_store_egl(EGLDisplay dpy, EGLConfig cfg, int fd_sock)
{
  g_dpy = dpy;
  g_cfg = cfg;
  g_fd_sock = fd_sock;
}

int dmabuf_proxy_is_ready(void)
{
  return g_ready;
}

/* ── DRM dumb buffer allocation ─────────────────────────────────────────── */
static int open_drm(void)
{
  g_heap_fd = open(DMA_BUF_UNIFIED_PATH, O_RDONLY | O_CLOEXEC);
  if (g_heap_fd < 0)
  {
    log_error("dmabuf: open(%s): %s", DMA_BUF_UNIFIED_PATH, strerror(errno));
    return -1;
  }
  log_console("dmabuf: heap fd=%d (%s)", g_heap_fd, DMA_BUF_UNIFIED_PATH);
  return g_heap_fd;
}

static int alloc_dumb(uint32_t w, uint32_t h, uint32_t *stride_out,
                      uint32_t *handle_out)
{
  /* 4 bytes per pixel (ARGB8888 / XRGB8888), aligned stride */
  uint32_t stride =
      (w * 4u + DMABUF_STRIDE_ALIGN - 1u) & ~(DMABUF_STRIDE_ALIGN - 1u);
  uint64_t size = (uint64_t)stride * h;

  struct _dma_heap_alloc req;
  memset(&req, 0, sizeof(req));
  req.len = size;
  req.fd_flags = O_RDWR | O_CLOEXEC;

  if (ioctl(g_heap_fd, DMA_HEAP_IOCTL_ALLOC, &req) < 0)
  {
    log_error("dmabuf: DMA_HEAP_IOCTL_ALLOC %ux%u (%" PRIu64 " B): %s", w, h,
              size, strerror(errno));
    return -1;
  }

  *stride_out = stride;
  *handle_out = 0; /* no GEM handle on this path */

  log_console("dmabuf: alloc %ux%u  stride=%u  size=%" PRIu64 "  fd=%d", w, h,
              stride, size, req.fd);
  return (int)req.fd;
}

/* ── Build EGLImage + renderbuffer + FBO from one dma_buf fd ─────────────── */
static int make_fbo(int frame_idx)
{
  DmaBufFrame *fr = &g_dmabuf_frames[frame_idx];

  static const uint32_t fmts[] = {
      DMABUF_FORMAT_ARGB8888,
      DMABUF_FORMAT_XRGB8888,
      DMABUF_FORMAT_ABGR8888,
  };

  for (size_t fi = 0; fi < sizeof(fmts) / sizeof(fmts[0]); fi++)
  {
    EGLint attrs[] = {EGL_WIDTH,
                      (EGLint)g_width,
                      EGL_HEIGHT,
                      (EGLint)g_height,
                      EGL_LINUX_DRM_FOURCC_EXT,
                      (EGLint)fmts[fi],
                      EGL_DMA_BUF_PLANE0_FD_EXT,
                      (EGLint)fr->dma_fd,
                      EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                      0,
                      EGL_DMA_BUF_PLANE0_PITCH_EXT,
                      (EGLint)fr->stride,
                      EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                      0,
                      EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
                      0,
                      EGL_NONE};

    fr->egl_image = p_eglCreateImageKHR(g_dpy, EGL_NO_CONTEXT,
                                        EGL_LINUX_DMA_BUF_EXT, NULL, attrs);

    if (fr->egl_image != EGL_NO_IMAGE_KHR)
    {
      fr->format = fmts[fi];
      break;
    }
#ifdef DEBUG
    log_console("dmabuf: frame %d format 0x%x failed (egl 0x%x), trying next",
                frame_idx, fmts[fi], eglGetError());
#endif
    eglGetError(); /* clear */
  }

  if (fr->egl_image == EGL_NO_IMAGE_KHR)
  {
    log_error("dmabuf: eglCreateImageKHR failed for frame %d", frame_idx);
    return -1;
  }

  glGenRenderbuffers(1, &fr->rbo);
  glBindRenderbuffer(GL_RENDERBUFFER, fr->rbo);
  p_glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER,
                                           (GLeglImageOES)fr->egl_image);
  glBindRenderbuffer(GL_RENDERBUFFER, 0);

  glGenFramebuffers(1, &fr->fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fr->fbo);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, fr->rbo);

  GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  if (st != GL_FRAMEBUFFER_COMPLETE)
  {
    log_error("dmabuf: FBO %d incomplete (0x%x)", frame_idx, st);
    return -1;
  }

  log_always("dmabuf: frame %d  fbo=%-3u rbo=%-3u  fmt=0x%x  stride=%u  fd=%d",
             frame_idx, fr->fbo, fr->rbo, fr->format, fr->stride, fr->dma_fd);
  return 0;
}

/* ── Send fds via SCM_RIGHTS ─────────────────────────────────────────────── */
static int send_fds(int sock, int *fds, int n)
{
  char dummy = 0;
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
  struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
  cm->cmsg_level = SOL_SOCKET;
  cm->cmsg_type = SCM_RIGHTS;
  cm->cmsg_len = CMSG_LEN((size_t)n * sizeof(int));
  memcpy(CMSG_DATA(cm), fds, (size_t)n * sizeof(int));

  ssize_t r = sendmsg(sock, &msg, 0);
  if (r < 0)
    log_error("dmabuf: sendmsg fds: %s", strerror(errno));
  return (r < 0) ? -1 : 0;
}

/* ── Public init ─────────────────────────────────────────────────────────── */
int dmabuf_proxy_init(uint32_t width, uint32_t height)
{
  if (g_ready)
  {
    log_console("dmabuf: already initialised, skipping");
    return 0;
  }
  if (g_dpy == EGL_NO_DISPLAY || g_fd_sock < 0)
  {
    log_error("dmabuf: store_egl not called before init");
    return -1;
  }

  g_width = width;
  g_height = height;

  load_ext();

  g_drm_fd = open_drm();
  if (g_drm_fd < 0)
    return -1;

  int send_fds_arr[DMABUF_NUM_BUFFERS];

  for (int i = 0; i < DMABUF_NUM_BUFFERS; i++)
  {
    DmaBufFrame *fr = &g_dmabuf_frames[i];
    memset(fr, 0, sizeof(*fr));
    fr->dma_fd = -1;
    fr->state = DMABUF_FRAME_FREE;
    fr->egl_image = EGL_NO_IMAGE_KHR;

    int fd = alloc_dumb(width, height, &fr->stride, &fr->drm_handle);
    if (fd < 0)
      return -1;
    fr->dma_fd = fd;

    if (make_fbo(i) < 0)
      return -1;

    send_fds_arr[i] = fd;
  }

  /* ── Send metadata then fds to the 64-bit stub ── */
  DmaBufFrameInfo infos[DMABUF_NUM_BUFFERS];
  for (int i = 0; i < DMABUF_NUM_BUFFERS; i++)
  {
    infos[i].width = width;
    infos[i].height = height;
    infos[i].format = g_dmabuf_frames[i].format;
    infos[i].stride = g_dmabuf_frames[i].stride;
    infos[i].offset = 0;
    infos[i].modifier_lo = 0;
    infos[i].modifier_hi = 0;
  }

  ssize_t meta_sz = sizeof(infos);
  if (write(g_fd_sock, infos, (size_t)meta_sz) != meta_sz)
  {
    log_error("dmabuf: write metadata: %s", strerror(errno));
    return -1;
  }
  if (send_fds(g_fd_sock, send_fds_arr, DMABUF_NUM_BUFFERS) < 0)
    return -1;

  /* Frame 0 starts as the active render target */
  g_dmabuf_current = 0;
  g_dmabuf_frames[0].state = DMABUF_FRAME_RENDERING;

  g_ready = 1;
  log_always("dmabuf: proxy init OK  %ux%u  %d frames", width, height,
             DMABUF_NUM_BUFFERS);
  return 0;
}

/* ── Called after eglMakeCurrent ─────────────────────────────────────────── */
void dmabuf_proxy_bind_current_fbo(void)
{
  if (g_ready)
    glBindFramebuffer(GL_FRAMEBUFFER, g_dmabuf_frames[g_dmabuf_current].fbo);
}

/* ── Intercept glBindFramebuffer(target, 0) ──────────────────────────────── */
GLuint dmabuf_proxy_remap_fbo0(void)
{
  return g_ready ? g_dmabuf_frames[g_dmabuf_current].fbo : 0;
}

/* ── Swap: finish current frame, advance to next free ────────────────────── */
int dmabuf_proxy_swap(uint32_t released_mask)
{
  /* Apply releases the stub reported (compositor gave buffer back) */
  for (int i = 0; i < DMABUF_NUM_BUFFERS; i++)
    if ((released_mask >> i) & 1u)
      g_dmabuf_frames[i].state = DMABUF_FRAME_FREE;

  /* Flush all GPU work for the current frame */
  glFlush();
  glFinish();

  int ready = g_dmabuf_current;
  g_dmabuf_frames[ready].state = DMABUF_FRAME_READY;

  /* Pick the next free frame; if none free yet, reuse oldest READY */
  int next = -1;
  for (int i = 0; i < DMABUF_NUM_BUFFERS; i++)
  {
    if (g_dmabuf_frames[i].state == DMABUF_FRAME_FREE)
    {
      next = i;
      break;
    }
  }
  if (next < 0)
  {
    /* All frames in flight — compositor is slow; reuse the frame
     * just finished (tearing possible but avoids stall) */
    log_console("dmabuf: all frames busy, recycling frame %d", ready);
    next = ready;
    g_dmabuf_frames[next].state = DMABUF_FRAME_FREE;
  }

  g_dmabuf_frames[next].state = DMABUF_FRAME_RENDERING;
  g_dmabuf_current = next;

  /* Pre-bind next FBO so the first draw after swap goes to it */
  glBindFramebuffer(GL_FRAMEBUFFER, g_dmabuf_frames[next].fbo);

  return ready;
}

void dmabuf_proxy_destroy()
{
  if (g_heap_fd >= 0)
  {
    close(g_heap_fd);
    g_heap_fd = -1;
  }
}
