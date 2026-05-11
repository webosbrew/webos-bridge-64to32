/*
 * bridge_core.c  —  aarch64 stub side
 *
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include <stdbool.h>

#ifdef DEBUG
#include <GLES3/gl32.h>
#include <EGL/egl.h>
#endif

#include "../deps/cJSON.h"

#define LOG_PREFIX "[bridge_core]"
#include "../bridge/bridge_shm.h"
#include "../bridge/shared_util.h"
#include "bridge_core.h"
#include "gles_bridge_protocol.h"

#ifndef GLES_PROXY_DEFAULT_PATH
#define GLES_PROXY_DEFAULT_PATH "/media/developer/temp/gles_proxy"
#endif

static BridgeShm g_ctrl_shm;
static BridgeShm g_data_shm;
static BridgeShm g_ctrl_wl_shm;
static BridgeShm g_data_wl_shm;

/* ── Internal state ──────────────────────────────────────────────────────── */
static uint8_t *g_data = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pid_t g_proxy_pid = -1;

static int req_efd = -1;
static int resp_efd = -1;

static int req_wl_efd = -1;
static int resp_wl_efd = -1;
static WLBridge g_wl;

unsigned int g_stub_current_ctx;

/* ── Ring state (stub/producer side) ─────────────────────────────────── */
static BridgeRing *g_ring = NULL;
static BridgeCtrl *g_cur_slot = NULL;
static uint64_t g_cur_seq = 0;

/* Process-local — there is only ever one producer (serialized by g_lock),
 * so these don't need to live in shared memory or be atomic. */
static uint64_t g_local_seq = 0;
static uint64_t g_local_data_head = 0;
static pid_t g_client_pid = 0;

static void print_environment_version(void)
{
  static char pretty[128];

  FILE *f = fopen("/var/run/nyx/os_info.json", "r");

  if (f)
  {
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root)
      return;

    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "webos_name");
    const cJSON *release =
        cJSON_GetObjectItemCaseSensitive(root, "webos_release");

    const char *name_str =
        (cJSON_IsString(name) && name->valuestring) ? name->valuestring : NULL;
    const char *release_str = (cJSON_IsString(release) && release->valuestring)
                                  ? release->valuestring
                                  : NULL;

    if (name_str && release_str)
      snprintf(pretty, sizeof(pretty), "%s %s", name_str, release_str);
    else if (name_str)
      snprintf(pretty, sizeof(pretty), "%s", name_str);
    else
      snprintf(pretty, sizeof(pretty), "webOS (unknown)");

    cJSON_Delete(root);
  }
  else
  {
    snprintf(pretty, sizeof(pretty), "(unknown)");
  }

  struct utsname u;
  uname(&u);

  const char *kernel = u.release;
  const char *arch = u.machine;

  log_always("Running on: %s (kernel=%s, arch=%s)", pretty, kernel, arch);

#ifdef DEBUG
  // log_always("GL_VERSION: %s", glGetString(GL_VERSION));
  // log_always("GL_RENDERER: %s", glGetString(GL_RENDERER));
#endif
}

/* ── eventfd helpers ────────────────────────────────────────────────────── */
static void efd_post(int fd)
{
  uint64_t one = 1;
  if (write(fd, &one, sizeof(one)) != sizeof(one))
  {
    log_console("efd_post(%d) FAILED: %s", fd, strerror(errno));
    abort();
  }
}

static void efd_wait(int fd)
{
  uint64_t val;
  ssize_t n;
  do
  {
    n = read(fd, &val, sizeof(val));
  } while (n < 0 && errno == EINTR);
  if (n != (ssize_t)sizeof(val))
  {
    log_console("efd_wait(%d) FAILED: %s", fd, strerror(errno));
    abort();
  }
}

/* ── Cleanup — called via atexit ────────────────────────────────────────── */
static void bridge_cleanup(void)
{
  /* Kill the proxy so it doesn't linger after client exits */
  if (g_proxy_pid > 0)
  {
    kill(g_proxy_pid, SIGTERM);
    /* Brief wait so the proxy can flush its logs */
    int status;
    waitpid(g_proxy_pid, &status, 0);
    g_proxy_pid = -1;
  }

  /* Remove shared memory so the next run always starts clean */
  shm_destroy(GLES_BRIDGE_CTRL_SHM, &g_ctrl_shm);
  shm_destroy(GLES_BRIDGE_DATA_SHM, &g_data_shm);

  shm_destroy(GLES_BRIDGE_WL_CTRL_SHM, &g_ctrl_wl_shm);
  shm_destroy(GLES_BRIDGE_WL_DATA_SHM, &g_data_wl_shm);

  log_console("cleanup done");
}

/* ── SIGCHLD handler ─────────────────────────────────────────────────────── */
static void proxy_sigchld(int sig)
{
  (void)sig;
  int status;
  pid_t pid = waitpid(-1, &status, WNOHANG);
  if (pid == g_proxy_pid)
  {
    log_console("proxy (pid %d) exited with status %d", (int)pid,
                WEXITSTATUS(status));
    g_proxy_pid = -1;
  }
}

/* ── Start the armv7a proxy ─────────────────────────────────────────────── */
static int start_proxy(void)
{
  log_console("start_proxy()");

  const char *bin = getenv("GLES_PROXY_BIN");
  if (!bin)
    bin = GLES_PROXY_DEFAULT_PATH;

  signal(SIGCHLD, proxy_sigchld);

  char req_str[32], resp_str[32];
  snprintf(req_str, sizeof(req_str), "%d", req_efd);
  snprintf(resp_str, sizeof(resp_str), "%d", resp_efd);

  char req_wl_str[32], resp_wl_str[32];
  snprintf(req_wl_str, sizeof(req_wl_str), "%d", req_wl_efd);
  snprintf(resp_wl_str, sizeof(resp_wl_str), "%d", resp_wl_efd);

  const char *appId = getenv("APPID") ? getenv("APPID") : "";

  /* Dummy placeholders kept so argv indices match proxy's main() */
  pid_t pid = fork();
  if (pid < 0)
  {
    log_console("fork: %s", strerror(errno));
    return -1;
  }

  if (pid == 0)
  {
    /* Child — will become the proxy process */
    execl(bin, bin, req_str, resp_str, req_wl_str, resp_wl_str, appId, NULL);
    log_console("execl(%s) FAILED: %s", bin, strerror(errno));
    _exit(1);
  }

  g_proxy_pid = pid;
  g_ring->proxy_pid = (int32_t)pid;
  log_console("proxy started, pid=%d", (int)pid);
  return 0;
}

/* ── Initialisation (lazy, called on first GL/EGL stub call) ───────────── */
static void bridge_init(void)
{
  // reset log file
  delete_log_file();

  log_console("initialising bridge on first GL/EGL stub call");

  print_environment_version();

  // dump opcodes for comparison
  /*for (int i = 0; i < OP_MAX; i++) {
    log_console("%d -> %s", i, opcode_to_string(i));
  }*/

  g_ctrl_shm = shm_create(GLES_BRIDGE_CTRL_SHM, GLES_BRIDGE_CTRL_SIZE);

  g_data_shm = shm_create(GLES_BRIDGE_DATA_SHM, GLES_BRIDGE_DATA_SIZE);

  g_ring = (BridgeRing *)g_ctrl_shm.ptr;
  g_data = (uint8_t *)g_data_shm.ptr;

  if (!g_ring)
  {
    log_console("g_ring is NULL!!");
    exit(1);
  }

  if (!g_data)
  {
    log_console("g_data is NULL!!");
    exit(1);
  }

#ifdef DEBUG
  log_console("g_ring=%p g_data=%p ctrl_shm.shmid=%d "
              "data_shm.shmid=%d",
              g_ring, g_data, g_ctrl_shm.shmid, g_data_shm.shmid);

  log_console("sizeof(BridgeCtrl)=%zu "
              "client_pid offset=%zu "
              "ctrlsize=%u",
              sizeof(BridgeRing), offsetof(BridgeCtrl, client_pid),
              GLES_BRIDGE_CTRL_SIZE);
#endif

  /* Plain (non-semaphore) eventfds — one write per command, one read per
   * command */
  req_efd = eventfd(0, 0);
  resp_efd = eventfd(0, 0);
  if (req_efd < 0 || resp_efd < 0)
  {
    log_console("eventfd: %s", strerror(errno));
    abort();
  }
  log_console("req_efd=%d resp_efd=%d", req_efd, resp_efd);

  req_wl_efd = eventfd(0, 0);
  resp_wl_efd = eventfd(0, 0);
  if (req_wl_efd < 0 || resp_wl_efd < 0)
  {
    log_console("wl eventfd: %s", strerror(errno));
    abort();
  }
  log_console("req_wl_efd=%d resp_wl_efd=%d", req_wl_efd, resp_wl_efd);

  wl_bridge_init();

  log_console("bridge_init() - setting up bridge_cleanup");

  /* Register cleanup so SHM and proxy are torn down on normal exit */
  if (atexit(bridge_cleanup) != 0)
    log_console("atexit returned a non zero value");

#ifdef DEBUG_VERBOSE
  log_console("before getpid");
#endif

  pid_t p = getpid();

#ifdef DEBUG_VERBOSE
  log_console("after getpid pid=%d", p);
  log_console("before write");
#endif

  g_client_pid = p;

#ifdef DEBUG_VERBOSE
  log_console("after write");
#endif

  log_console("bridge_init() - storing client pid: %d", g_client_pid);

  if (start_proxy() < 0)
    abort();
  /* Give proxy time to map the SHM and enter its dispatch loop */
  usleep(150000);

  log_console("ready");
}

/* ══════════════════════════════════════════════════════════════════════════
 * Public API
 * ══════════════════════════════════════════════════════════════════════════ */

/* ── Slot reservation ─────────────────────────────────────────────────── */
static BridgeCtrl *ring_reserve_slot(uint64_t *out_seq)
{
  uint64_t seq = g_local_seq;

  for (;;)
  {
    uint64_t completed =
        atomic_load_explicit(&g_ring->completed_seq, memory_order_acquire);

    if (seq - completed < BRIDGE_RING_SLOTS)
      break;

    /* Ring full */
    struct timespec ts = {0, 20000}; /* 20us */
    nanosleep(&ts, NULL);
  }

  g_local_seq = seq + 1;
  *out_seq = seq;
  return &g_ring->slots[seq & BRIDGE_RING_MASK];
}

/* ── Data-arena ring allocator ────────────────────────────────────────── */
static uint32_t ring_data_alloc(size_t size)
{
  if (size > GLES_BRIDGE_DATA_SIZE)
  {
    log_error("bridge_data_write OVERFLOW: %zu > %u", size,
              GLES_BRIDGE_DATA_SIZE);
    abort();
  }

  size = (size + 3u) & ~3u;

  for (;;)
  {
    uint32_t pos = (uint32_t)(g_local_data_head % GLES_BRIDGE_DATA_SIZE);
    uint32_t space_to_end = GLES_BRIDGE_DATA_SIZE - pos;
    uint64_t new_head;
    uint32_t offset;

    if (size <= space_to_end)
    {
      offset = pos;
      new_head = g_local_data_head + size;
    }
    else
    {
      // Doesn't fit before the wrap point
      offset = 0;
      new_head = g_local_data_head + space_to_end + size;
    }

    uint64_t tail =
        atomic_load_explicit(&g_ring->data_tail, memory_order_acquire);

    if (new_head - tail > GLES_BRIDGE_DATA_SIZE)
    {
      // full
      struct timespec ts = {0, 20000};
      nanosleep(&ts, NULL);
      continue;
    }

    g_local_data_head = new_head;
    return offset;
  }
}

void bridge_begin(void)
{
  pthread_mutex_lock(&g_lock);
  if (!g_ring)
    bridge_init();

  g_cur_slot = ring_reserve_slot(&g_cur_seq);
  g_cur_slot->client_pid = g_client_pid;
  g_cur_slot->data_watermark = g_local_data_head;
}

uint32_t bridge_data_write(const void *src, size_t size)
{
  uint32_t offset = ring_data_alloc(size);

  if (size && src)
    memcpy(g_data + offset, src, size);

  g_cur_slot->data_watermark = g_local_data_head;
  return offset;
}

void bridge_data_read(void *dst, uint32_t offset, size_t size)
{
  if ((uint64_t)offset + size > GLES_BRIDGE_DATA_SIZE)
  {
    log_error("bridge_data_read OVERFLOW: %zu > %u, offset: %d\n", size,
              GLES_BRIDGE_DATA_SIZE, offset);
    abort();
  }

  memcpy(dst, g_data + offset, size);
}

uint8_t *bridge_data_ptr(uint32_t offset)
{
  return g_data + offset;
}

void bridge_send_void(void)
{
  g_cur_slot->needs_response = 0;

  atomic_store_explicit(&g_ring->published_seq, g_cur_seq + 1,
                        memory_order_release);
  efd_post(req_efd);

  pthread_mutex_unlock(&g_lock);
}

uint64_t bridge_send_call(void)
{
  g_cur_slot->needs_response = 1;

  uint64_t seq = g_cur_seq;
  atomic_store_explicit(&g_ring->published_seq, seq + 1, memory_order_release);
  efd_post(req_efd);

  // wait until dispatched
  for (;;)
  {
    uint64_t completed =
        atomic_load_explicit(&g_ring->completed_seq, memory_order_acquire);
    if (completed > seq)
      break;
    efd_wait(resp_efd);
  }

  uint64_t r = g_cur_slot->result;
  pthread_mutex_unlock(&g_lock);
  return r;
}

BridgeCtrl *bridge_ctrl(void)
{
  return g_cur_slot;
}

/* ── WL state ───────────────────────────────────── */
static WLBridge g_wl;

/* ── WL allocator ───────────────────────────────── */

static uint32_t wl_data_alloc(size_t size)
{
  uint32_t off = g_wl.data_pos;

  g_wl.data_pos = (g_wl.data_pos + (uint32_t)size + 3u) & ~3u;

  if (g_wl.data_pos > GLES_BRIDGE_DATA_SIZE)
  {
    log_console("FATAL wl shm overflow (%u bytes)", g_wl.data_pos);
    abort();
  }

  return off;
}

/* ── Init ────────────────────────────────────────── */
void wl_bridge_init(void)
{
  log_console("initialising wl bridge");

  g_ctrl_wl_shm = shm_create(GLES_BRIDGE_WL_CTRL_SHM, GLES_BRIDGE_CTRL_SIZE);

  g_data_wl_shm = shm_create(GLES_BRIDGE_WL_DATA_SHM, GLES_BRIDGE_DATA_SIZE);

  g_wl.ctrl = (BridgeCtrl *)g_ctrl_wl_shm.ptr;

  g_wl.data = (uint8_t *)g_data_wl_shm.ptr;

  if (!g_wl.ctrl || !g_wl.data)
    abort();

  pthread_mutex_init(&g_wl.lock, NULL);

  g_wl.data_pos = 0;
}

/* ── Begin/end ───────────────────────────────────── */

void wl_bridge_begin(void)
{
  pthread_mutex_lock(&g_wl.lock);

  if (!g_ring)
  {
    bridge_init();
    g_wl.ctrl->client_pid = g_client_pid;
  }

  g_wl.data_pos = 0;
}

void wl_bridge_end(void)
{
  pthread_mutex_unlock(&g_wl.lock);
}

/* ── Data access ─────────────────────────────────── */

uint32_t bridge_data_write_wl(const void *src, size_t size)
{
  uint32_t off = wl_data_alloc(size);

  memcpy(g_wl.data + off, src, size);

  return off;
}

uint8_t *bridge_data_ptr_wl(uint32_t offset)
{
  return g_wl.data + offset;
}

/* ── Send ────────────────────────────────────────── */

void bridge_send_void_wl(void)
{
#ifdef DEBUG_BRIDGE_VERBOSE
  log_console("bridge_send_void_wl opcode=%u", g_wl.ctrl->opcode);
#endif

  g_wl.ctrl->needs_response = 1;

  efd_post(req_wl_efd);
  efd_wait(resp_wl_efd);

  wl_bridge_end();
}

uint64_t bridge_send_call_wl(void)
{
#ifdef DEBUG_BRIDGE_VERBOSE
  log_console("bridge_send_call_wl opcode=%u", g_wl.ctrl->opcode);
#endif

  g_wl.ctrl->needs_response = 1;

  efd_post(req_wl_efd);
  efd_wait(resp_wl_efd);

  uint64_t r = g_wl.ctrl->result;

  wl_bridge_end();

  return r;
}

BridgeCtrl *bridge_ctrl_wl(void)
{
  return g_wl.ctrl;
}
