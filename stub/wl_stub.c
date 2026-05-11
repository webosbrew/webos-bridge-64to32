/*
 * wl_stub.c — Wayland bridge stub for aarch64.
 *
 * Compiled as libwayland-client.so.0.  All wl_* calls from the client are
 * intercepted here; bridge calls are forwarded to the 32-bit proxy via the
 * shared-memory bridge, and non-bridge calls are forwarded to the real
 * Wayland library which has been renamed to libwayland-client-real.so.0
 * (use: patchelf --set-soname libwayland-client-real.so.0
 * libwayland-client.so.0)
 *
 */

#define _GNU_SOURCE
#include "bridge_core.h"
#include "gles_bridge_protocol.h"
#define LOG_PREFIX "[wl_stub]"
#include "../bridge/shared_util.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

// #include "webos-input-manager.h"

#define REAL(name) real_##name

/* ── Handle to the real Wayland library ─────────────────────────────────── */
#ifndef WL_REAL_LIB
#define WL_REAL_LIB "libwayland-client-real.so.0"
#endif

#ifndef WL_CURSOR_LIB
#define WL_CURSOR_LIB "libwayland-cursor.so.0"
#endif

static void *g_real_wl_lib = NULL;
static void *g_real_wl_cursor_lib = NULL;

static void *real_wl_lib(void)
{
  if (!g_real_wl_lib)
  {
    g_real_wl_lib = dlopen(WL_REAL_LIB, RTLD_NOW | RTLD_GLOBAL);
    if (!g_real_wl_lib)
      log_error("[wl_stub] FATAL: cannot open %s: %s", WL_REAL_LIB, dlerror());
  }
  return g_real_wl_lib;
}

static void *real_wl_cursor_lib(void)
{
  if (!g_real_wl_cursor_lib)
  {
    g_real_wl_cursor_lib = dlopen(WL_CURSOR_LIB, RTLD_NOW | RTLD_GLOBAL);
    if (!g_real_wl_cursor_lib)
      log_error("[wl_stub] FATAL: cannot open %s: %s", WL_CURSOR_LIB,
                dlerror());
  }
  return g_real_wl_cursor_lib;
}

/* ── Real-function resolver ─────────────────────────────────────────────── */
/*
 * Each DECL_REAL() declares a function pointer and a constructor that resolves
 * it from the real library at startup.  We use a priority of 101 so these run
 * before any other constructors that might call into Wayland.
 */
#define DECL_REAL(ret, name, ...)                                              \
  static ret (*real_##name)(__VA_ARGS__) = NULL;                               \
  __attribute__((constructor(101))) static void _init_##name(void)             \
  {                                                                            \
    if (!real_##name)                                                          \
    {                                                                          \
      void *lib = real_wl_lib();                                               \
      if (lib)                                                                 \
        real_##name = dlsym(lib, #name);                                       \
      if (!real_##name)                                                        \
        log_error("[wl_stub] WARNING: cannot resolve %s from %s: %s\n", #name, \
                  WL_REAL_LIB, dlerror());                                     \
    }                                                                          \
  }

#define DECL_CURSOR_REAL(ret, name, ...)                                       \
  static ret (*real_##name)(__VA_ARGS__) = NULL;                               \
  __attribute__((constructor(101))) static void _init_##name(void)             \
  {                                                                            \
    if (!real_##name)                                                          \
    {                                                                          \
      void *lib = real_wl_cursor_lib();                                        \
      if (lib)                                                                 \
        real_##name = dlsym(lib, #name);                                       \
      if (!real_##name)                                                        \
        log_error("[wl_stub] WARNING: cannot resolve %s from %s: %s", #name,   \
                  WL_CURSOR_LIB, dlerror());                                   \
    }                                                                          \
  }

DECL_REAL(struct wl_display *, wl_display_connect, const char *)
DECL_REAL(void, wl_display_disconnect, struct wl_display *)
DECL_REAL(int, wl_display_get_fd, struct wl_display *)
DECL_REAL(int, wl_display_dispatch, struct wl_display *)
DECL_REAL(int, wl_display_dispatch_pending, struct wl_display *)
DECL_REAL(int, wl_display_dispatch_queue, struct wl_display *,
          struct wl_event_queue *)
DECL_REAL(int, wl_display_dispatch_queue_pending, struct wl_display *,
          struct wl_event_queue *)
DECL_REAL(int, wl_display_roundtrip, struct wl_display *)
DECL_REAL(int, wl_display_flush, struct wl_display *)
DECL_REAL(int, wl_display_prepare_read, struct wl_display *)
DECL_REAL(int, wl_display_prepare_read_queue, struct wl_display *,
          struct wl_event_queue *)
DECL_REAL(void, wl_display_cancel_read, struct wl_display *)
DECL_REAL(int, wl_display_read_events, struct wl_display *)
DECL_REAL(uint32_t, wl_proxy_get_version, struct wl_proxy *)
DECL_REAL(uint32_t, wl_proxy_get_id, struct wl_proxy *)
DECL_REAL(int, wl_proxy_add_listener, struct wl_proxy *, void (**)(void),
          void *)
DECL_REAL(void, wl_proxy_destroy, struct wl_proxy *)
DECL_REAL(void, wl_proxy_marshal, struct wl_proxy *, uint32_t, ...)
DECL_REAL(struct wl_proxy *, wl_proxy_marshal_constructor, struct wl_proxy *,
          uint32_t, const struct wl_interface *, ...)
DECL_REAL(struct wl_proxy *, wl_proxy_marshal_array_constructor,
          struct wl_proxy *, uint32_t, union wl_argument *,
          const struct wl_interface *)
DECL_REAL(struct wl_proxy *, wl_proxy_marshal_constructor_versioned,
          struct wl_proxy *, uint32_t, const struct wl_interface *, uint32_t,
          ...)
DECL_REAL(struct wl_proxy *, wl_proxy_marshal_array_constructor_versioned,
          struct wl_proxy *, uint32_t, union wl_argument *,
          const struct wl_interface *, uint32_t)
DECL_REAL(struct wl_proxy *, wl_proxy_marshal_flags, struct wl_proxy *,
          uint32_t, const struct wl_interface *, uint32_t, uint32_t, ...)
DECL_CURSOR_REAL(struct wl_cursor_theme *, wl_cursor_theme_load, const char *,
                 int, struct wl_shm *)
DECL_CURSOR_REAL(struct wl_cursor *, wl_cursor_theme_get_cursor,
                 struct wl_cursor_theme *, const char *)
DECL_CURSOR_REAL(void, wl_cursor_theme_destroy, struct wl_cursor_theme *)

// for SDL2:
DECL_REAL(void, wl_proxy_set_user_data, struct wl_proxy *, void *)
DECL_REAL(void *, wl_proxy_get_user_data, struct wl_proxy *)
DECL_REAL(void, wl_proxy_set_tag, struct wl_proxy *, const char *const *)
DECL_REAL(const char *const *, wl_proxy_get_tag, struct wl_proxy *)
DECL_REAL(struct wl_event_queue *, wl_display_create_queue, struct wl_display *)
DECL_REAL(void, wl_event_queue_destroy, struct wl_event_queue *)
DECL_REAL(void *, wl_proxy_create_wrapper, void *)
DECL_REAL(void, wl_proxy_wrapper_destroy, void *)
DECL_REAL(void, wl_proxy_set_queue, struct wl_proxy *, struct wl_event_queue *)

/* ── Handle encoding ─────────────────────────────────────────────────────── */
#define H2P(n) ((void *)(uintptr_t)(uint32_t)(n))
#define P2H(p) ((uint32_t)(uintptr_t)(p))
#define IS_BH(p) (P2H(p) > 0 && P2H(p) < 1024)

#define BH_REGISTRY 1
#define BH_COMPOSITOR 2
#define BH_SHELL 3
#define BH_WEBOS_SHELL 4
#define BH_SHM 5
#define BH_WEBOS_INPUT_MGR 6
#define BH_SEAT_BASE 10
#define BH_OUTPUT_BASE 20
#define BH_KEYBOARD_BASE 30
#define BH_POINTER_BASE 40
#define BH_WEBOS_SEAT_BASE 50 /* up to MAX_SEATS: 50, 51 */
#define BH_SURFACE_BASE 100
#define BH_SHELL_SURF_BASE 200
#define BH_WEBOS_SURF_BASE 300
#define BH_STARFISH_POINTER 7
#define BH_STARFISH_OUTPUT 8
#define BH_WEBOS_FOREIGN 9
#define BH_DUMMY_SYNC 0xFD
#define BH_DUMMY 0xFE

/* Maximum number of concurrently tracked surfaces per display connection   */
#define SURF_MAP_MAX 8

#define BRIDGE_MAGIC 0x42524944 /* "BRID" */

typedef struct
{
  uint32_t magic;
  uint32_t slot;
} BridgeDisplay;

#define IS_BRIDGE_DPY(p)                                                       \
  ({                                                                           \
    BridgeDisplay *bd = (BridgeDisplay *)(p);                                  \
    (void *)bd >= (void *)&g_bridge_displays[1] &&                             \
        (void *)bd <= (void *)&g_bridge_displays[EGL_BRIDGE_MAX_DISPLAYS] &&   \
        bd->magic == BRIDGE_MAGIC;                                             \
  })

static BridgeDisplay g_bridge_displays[EGL_BRIDGE_MAX_DISPLAYS];
static int g_active_bridge_displays = 0;

#define IS_BRIDGE_PROXY(p) (IS_BH(p) || IS_BRIDGE_DPY(p))

/* ── Per-listener storage ────────────────────────────────────────────────── */
#define MAX_SEATS 2
#define MAX_OUTPUTS 4

typedef struct
{
  void (**funcs)(void);
  void *data;
} listener_slot_t;

/* ── Bridge state ────────────────────────────────────────────────────────── */
typedef struct
{
  uint32_t id_watermark;
  uint32_t num_seats;
  uint32_t num_outputs;

  /*
   * surface_slot_map[i] = proxy slot for the i-th stub surface handle,
   * where i = (stub_handle - BH_SURFACE_BASE).
   * Indexed by the same offset for shell-surf and webos-surf handles.
   */
  uint32_t surface_slot_map[SURF_MAP_MAX];

  /* Legacy: last-created proxy slot (used for ops that don't have a handle) */
  uint32_t surface_proxy_slot;

  /* Fake display fd that client polls */
  int event_efd;

  /* Next surface handle to hand out */
  uint32_t next_surface;

  /* Per-connect seat/output bind counters */
  uint32_t seat_cnt;
  uint32_t out_cnt;

  /* Registry listener */
  const struct wl_registry_listener *reg_listener;
  void *reg_listener_data;

  /* webOS shell surface listener — one per surface slot */
  void (**webos_ss_listener)(void); /* legacy single-listener */
  void *webos_ss_listener_data;
  void (**webos_ss_listeners[SURF_MAP_MAX])(void);
  void *webos_ss_listener_data_arr[SURF_MAP_MAX];

  listener_slot_t seats[MAX_SEATS];
  listener_slot_t outputs[MAX_OUTPUTS];

/* Per-callback listener storage — indexed by handle offset from BH_DUMMY    */
#define MAX_CALLBACKS 32
#define BH_CALLBACK_BASE 0x200
#define BRIDGE_DUMMY_QUEUE ((struct wl_event_queue *)((uintptr_t)1))

  int output_events_pending[MAX_OUTPUTS]; /* 1 = need to fire events on next
                                             dispatch */
  int callback_pending[MAX_CALLBACKS];

  listener_slot_t keyboards[MAX_SEATS];
  listener_slot_t pointers[MAX_SEATS];

  int globals_fired;
  int keyboard_enter_sent;

  uint32_t webos_seat_received;
  uint32_t webos_seat_id;
  uint32_t webos_seat_designator;
  uint32_t webos_seat_capabilities;
  char webos_seat_name[256];

  listener_slot_t webos_seat_listeners[MAX_SEATS];
  int webos_seat_info_fired[MAX_SEATS];

  uint32_t active_surface_handle; // stub handle of surface that went fullscreen
  uint32_t keyboard_focus_handle; // handle focus was actually sent to

} wl_bridge_state_t;

static wl_bridge_state_t g_bs;

struct wl_webos_shell_surface;
struct wl_webos_seat;

#define MAX_PROXY_TAGS 512
static const char *const *g_proxy_tags[MAX_PROXY_TAGS];

typedef struct
{
  void (**funcs)(void);
  void *data;
  int pending; /* 1 = done event not yet delivered */
} CallbackSlot;

static CallbackSlot g_callbacks[MAX_CALLBACKS];
static uint32_t g_callback_next = 0;

static uint32_t alloc_callback(void)
{
  uint32_t idx = g_callback_next % MAX_CALLBACKS;
  g_callback_next++;
  g_callbacks[idx].funcs = NULL;
  g_callbacks[idx].data = NULL;
  g_callbacks[idx].pending = 0;
  return idx;
}

static int is_callback_handle(uint32_t h)
{
  return h >= BH_CALLBACK_BASE && h < BH_CALLBACK_BASE + MAX_CALLBACKS;
}

/* Called from wl_proxy_add_listener when h is a callback handle */
static void _fire_callback_done(uint32_t idx)
{
  CallbackSlot *s = &g_callbacks[idx];
  if (!s->funcs || !s->funcs[0])
    return;
  /* wl_callback_listener index 0 = done(data, callback, serial) */
  typedef void (*done_fn)(void *, struct wl_callback *, uint32_t);
  ((done_fn)s->funcs[0])(s->data,
                         (struct wl_callback *)H2P(BH_CALLBACK_BASE + idx),
                         0 /* serial */);
}

static void _fire_output_events(uint32_t idx);

/* ── Helpers: slot lookup ────────────────────────────────────────────────── */

/*
 * slot_for_surface(stub_handle) — maps a stub surface handle to the proxy
 * slot returned by OP_wl_compositor_create_surface.
 */
static uint32_t slot_for_surface(uint32_t h)
{
  uint32_t idx = h - BH_SURFACE_BASE;
  if (idx < SURF_MAP_MAX)
    return g_bs.surface_slot_map[idx];
  // return g_bs.surface_proxy_slot; /* safe fallback */
#ifdef DEBUG_WAYLAND
  log_console("wl_proxy_get_id - returning 0");
#endif
  return 0;
}

/* Same index for shell-surf and webos-surf handles (offset from their base) */
static uint32_t slot_for_shellsurf(uint32_t h)
{
  uint32_t idx = h - BH_SHELL_SURF_BASE;
  if (idx < SURF_MAP_MAX)
    return g_bs.surface_slot_map[idx];
  // return g_bs.surface_proxy_slot;
#ifdef DEBUG_WAYLAND
  log_console("slot_for_shellsurf - returning 0");
#endif
  return 0;
}

static uint32_t slot_for_webosurf(uint32_t h)
{
  uint32_t idx = h - BH_WEBOS_SURF_BASE;
  if (idx < SURF_MAP_MAX)
    return g_bs.surface_slot_map[idx];
  // return g_bs.surface_proxy_slot;
#ifdef DEBUG_WAYLAND
  log_console("slot_for_webosurf - returning 0");
#endif
  return 0;
}

/* ── Internal: fire globals / dispatch ──────────────────────────────────── */
static void _fire_globals(void)
{
  if (g_bs.globals_fired || !g_bs.reg_listener || !g_bs.reg_listener->global)
    return;
  g_bs.globals_fired = 1;

  struct wl_registry *reg = (struct wl_registry *)H2P(BH_REGISTRY);
  const struct wl_registry_listener *L = g_bs.reg_listener;
  void *d = g_bs.reg_listener_data;

  L->global(d, reg, BH_COMPOSITOR, "wl_compositor", 4);
  L->global(d, reg, BH_SHELL, "wl_shell", 1);
  L->global(d, reg, BH_WEBOS_SHELL, "wl_webos_shell", 2);
  L->global(d, reg, BH_SHM, "wl_shm", 1);
  for (uint32_t i = 0; i < g_bs.num_seats && i < MAX_SEATS; i++)
    L->global(d, reg, BH_SEAT_BASE + i, "wl_seat", 7);
  for (uint32_t i = 0; i < g_bs.num_outputs && i < MAX_OUTPUTS; i++)
    L->global(d, reg, BH_OUTPUT_BASE + i, "wl_output", 3);
  L->global(d, reg, BH_WEBOS_INPUT_MGR, "wl_webos_input_manager", 1);
  L->global(d, reg, BH_STARFISH_POINTER, "wl_starfish_pointer", 1);
  L->global(d, reg, BH_STARFISH_OUTPUT, "wl_starfish_output", 1);
  L->global(d, reg, BH_WEBOS_FOREIGN, "wl_webos_foreign", 1);
}

/* Drain keyboard events and webOS shell surface state events packed by
 * the proxy into result_buf.
 *
 * Layout (uint32_t words):
 *   [0]                  keyboard event count N
 *   [1 .. 1+N*2-1]       key/state pairs
 *   [BASE]               webOS state event count M   (BASE = 1+INPUT_EVT_MAX*2)
 *   [BASE+1 .. BASE+M*2] proxy_slot/state pairs
 */
#define WEBOS_STATE_BASE (1 + INPUT_EVT_MAX * 2)

static void _fire_keyboard_focus_to(uint32_t surf_handle)
{
  if (g_bs.keyboard_focus_handle == surf_handle)
    return; /* already focused here, nothing to do */

  g_bs.keyboard_focus_handle = surf_handle;
  g_bs.keyboard_enter_sent = 1;

  struct wl_surface *surf = (struct wl_surface *)H2P(surf_handle);

  for (uint32_t s = 0; s < MAX_SEATS; s++)
  {
    listener_slot_t *kb = &g_bs.keyboards[s];
    if (!kb->funcs)
      continue;

#ifdef DEBUG_WAYLAND
    log_console("fire_keyboard_focus: seat_%u surface %p", s, surf);
#endif

    if (kb->funcs[0])
    {
      typedef void (*keymap_fn)(void *, struct wl_keyboard *, uint32_t, int32_t,
                                uint32_t);
      ((keymap_fn)kb->funcs[0])(
          kb->data, (struct wl_keyboard *)H2P(BH_KEYBOARD_BASE + s), 0, -1, 0);
    }

    if (kb->funcs[1])
    {
      typedef void (*enter_fn)(void *, struct wl_keyboard *, uint32_t,
                               struct wl_surface *, struct wl_array *);
      struct wl_array empty = {0, 0, NULL};
      ((enter_fn)kb->funcs[1])(kb->data,
                               (struct wl_keyboard *)H2P(BH_KEYBOARD_BASE + s),
                               1, surf, &empty);
    }
  }
}

/* ── Give keyboard focus ─────────────── */
static void _fire_keyboard_focus(void)
{
#ifdef DEBUG_WAYLAND_VERBOSE
  log_console("_fire_keyboard_focus: keyboard_enter_sent=%d next_surface=%u",
              g_bs.keyboard_enter_sent, g_bs.next_surface);
#endif
  if (g_bs.keyboard_enter_sent)
    return;

  /* Wait until at least one surface has been created AND gone fullscreen */
  if (!g_bs.active_surface_handle)
    return;

  _fire_keyboard_focus_to(g_bs.active_surface_handle);
}

static void _deliver_input_events(void)
{
  BridgeCtrl *C = BRIDGE_CTRL_WL();
  const uint32_t *rb = (const uint32_t *)C->result_buf;

  /* ── keyboard events ── */
  uint32_t count = rb[0];
#ifdef DEBUG_WAYLAND
  if (count > 0)
    log_console("Delivering %u input events", count);
#endif
  for (uint32_t i = 0; i < count; i++)
  {
    uint32_t key = rb[1 + i * 2];
    uint32_t state = rb[2 + i * 2];
    for (uint32_t s = 0; s < g_bs.num_seats && s < MAX_SEATS; s++)
    {
      listener_slot_t *kb = &g_bs.keyboards[s];
      if (!kb->funcs || !kb->funcs[3])
        continue;
      typedef void (*key_fn)(void *, struct wl_keyboard *, uint32_t, uint32_t,
                             uint32_t, uint32_t);
      ((key_fn)kb->funcs[3])(kb->data,
                             (struct wl_keyboard *)H2P(BH_KEYBOARD_BASE + s),
                             0 /* serial */, 0 /* time */, key, state);
    }
  }

  /* ── webOS shell surface state_changed events ── */
  uint32_t wss_count = rb[WEBOS_STATE_BASE];
  const uint32_t *wss = rb + WEBOS_STATE_BASE + 1;

  for (uint32_t i = 0; i < wss_count; i++)
  {
    uint32_t proxy_slot = wss[i * 2 + 0]; /* 0-based proxy slot */
    uint32_t wstate = wss[i * 2 + 1];

    /* stub surface index: proxy slots are 1-based, stub offsets 0-based */
    uint32_t stub_idx = proxy_slot > 0 ? proxy_slot - 1 : 0;

    if (wstate == 3)
    {
      uint32_t new_surf_handle = BH_SURFACE_BASE + stub_idx;
      g_bs.active_surface_handle = new_surf_handle;

      /*
       * Only redirect keyboard focus if this surface has an EGL window
       * backing it */
      if (g_bs.keyboard_enter_sent &&
          g_bs.keyboard_focus_handle != new_surf_handle &&
          g_bs.keyboards[0].funcs != NULL)
      {
#ifdef DEBUG_WAYLAND
        log_console("_deliver_input_events: re-focusing keyboard "
                    "from 0x%x to 0x%x (stub_idx=%u)",
                    g_bs.keyboard_focus_handle, new_surf_handle, stub_idx);
#endif
        _fire_keyboard_focus_to(new_surf_handle);
      }
    }

#ifdef DEBUG_WAYLAND
    log_console("_deliver_input_events: wss state proxy_slot=%u wstate=%u "
                "stub_idx=%u g_bs.webos_ss_listeners[stub_idx]=%p "
                "g_bs.webos_ss_listener=%p g_bs.active_surface_handle=%p "
                "g_bs.keyboard_enter_sent=%d",
                proxy_slot, wstate, stub_idx, g_bs.webos_ss_listeners[stub_idx],
                g_bs.webos_ss_listener, g_bs.active_surface_handle,
                g_bs.keyboard_enter_sent);
#endif

    struct wl_webos_shell_surface *surf_h =
        (struct wl_webos_shell_surface *)H2P(BH_WEBOS_SURF_BASE + stub_idx);

    /* prefer per-surface listener, fall back to global */
    void (**listener)(void) = NULL;
    void *ldata = NULL;
    if (stub_idx < SURF_MAP_MAX && g_bs.webos_ss_listeners[stub_idx])
    {
      listener = g_bs.webos_ss_listeners[stub_idx];
      ldata = g_bs.webos_ss_listener_data_arr[stub_idx];
    }
    else if (g_bs.webos_ss_listener)
    {
      listener = g_bs.webos_ss_listener;
      ldata = g_bs.webos_ss_listener_data;
    }

    if (!listener || !listener[0])
    {
      log_error("_deliver_input_events: NO LISTENER");
      continue;
    }

    typedef void (*sc_fn)(void *, struct wl_webos_shell_surface *, uint32_t);
    ((sc_fn)listener[0])(ldata, surf_h, wstate);
  }
}

void _bridge_dispatch(void)
{
  _fire_globals();

  /* ── deliver pending output events (deferred from wl_proxy_add_listener) ──
   * Must happen after _fire_globals so SDL2 has had a chance to register its
   * output listener before we call it.                                       */
  for (uint32_t i = 0; i < g_bs.num_outputs && i < MAX_OUTPUTS; i++)
  {
    if (g_bs.output_events_pending[i])
    {
      g_bs.output_events_pending[i] = 0;
      _fire_output_events(i);
    }
  }

  /* ── deliver pending wl_callback done events ── */
  for (uint32_t i = 0; i < MAX_CALLBACKS; i++)
  {
    if (g_bs.callback_pending[i] && g_callbacks[i].funcs)
    {
      g_bs.callback_pending[i] = 0;
      _fire_callback_done(i);
    }
  }

  BRIDGE_BEGIN_WL();

  BRIDGE_CTRL_WL()->opcode = OP_wl_roundtrip;
  BRIDGE_CTRL_WL()->args_len = 0;
  BRIDGE_CTRL_WL()->data_offset = 0;
  BRIDGE_CTRL_WL()->data_size = 0;

  bridge_send_void_wl();

  /* ── fire keyboard focus before delivering events ── */
  _fire_keyboard_focus();

  uint64_t one = 1;
  (void)write(g_bs.event_efd, &one, sizeof(one));

  _deliver_input_events();
}

static void _fire_seat_capabilities(uint32_t idx)
{
  listener_slot_t *s = &g_bs.seats[idx];
  if (!s->funcs)
    return;
  if (s->funcs[0])
  {
    typedef void (*cap_fn)(void *, struct wl_seat *, uint32_t);
    ((cap_fn)s->funcs[0])(s->data, (struct wl_seat *)H2P(BH_SEAT_BASE + idx),
                          3u /* POINTER=1 | KEYBOARD=2 */);
  }
  if (s->funcs[1])
  {
    typedef void (*name_fn)(void *, struct wl_seat *, const char *);
    ((name_fn)s->funcs[1])(s->data, (struct wl_seat *)H2P(BH_SEAT_BASE + idx),
                           "default");
  }
}

static void _fire_output_events(uint32_t idx)
{
  listener_slot_t *s = &g_bs.outputs[idx];
  if (!s->funcs)
    return;
  struct wl_output *out = (struct wl_output *)H2P(BH_OUTPUT_BASE + idx);

  /* index 0: geometry */
  if (s->funcs[0])
  {
    typedef void (*geo_fn)(void *, struct wl_output *, int32_t, int32_t,
                           int32_t, int32_t, int32_t, const char *,
                           const char *, int32_t);
    ((geo_fn)s->funcs[0])(s->data, out, 0, 0, /* x, y */
                          527, 296,           /* physical mm */
                          WL_OUTPUT_SUBPIXEL_UNKNOWN, "LG", "webOS TV",
                          WL_OUTPUT_TRANSFORM_NORMAL);
  }

  /* index 1: mode — flag 3 = WL_OUTPUT_MODE_CURRENT|WL_OUTPUT_MODE_PREFERRED */
  if (s->funcs[1])
  {
    typedef void (*mode_fn)(void *, struct wl_output *, uint32_t, int32_t,
                            int32_t, int32_t);
    ((mode_fn)s->funcs[1])(s->data, out, 3, 1920, 1080, 60000);
  }

  /* index 2: scale — SDL2 Wayland uses this to set the DPI/HiDPI factor */
  if (s->funcs[2])
  {
    typedef void (*scale_fn)(void *, struct wl_output *, int32_t);
    ((scale_fn)s->funcs[2])(s->data, out, 1 /* scale factor 1:1 */);
  }

  /* index 3: done — signals SDL2 that all output data has arrived;
   * SDL2 calls SDL_AddVideoDisplay() inside this callback             */
  if (s->funcs[3])
  {
    typedef void (*done_fn)(void *, struct wl_output *);
    ((done_fn)s->funcs[3])(s->data, out);
  }
}

/* ══════════════════════════════════════════════════════════════════════════
 * wl_display
 * ══════════════════════════════════════════════════════════════════════════ */

struct wl_display *wl_display_connect(const char *name)
{
#ifdef DEBUG_WAYLAND
  log_console("wl_display_connect() name = %s", name);
  log_console("pre-reset: event_efd=%d next_surface=%u id_watermark=%u",
              g_bs.event_efd, g_bs.next_surface, g_bs.id_watermark);
#endif

  BRIDGE_BEGIN_WL();

  if (g_bs.event_efd > 0)
    close(g_bs.event_efd);
  memset(&g_bs, 0, sizeof(g_bs));
  g_bs.next_surface = BH_SURFACE_BASE;
  g_bs.event_efd = eventfd(0, EFD_NONBLOCK);

#ifdef DEBUG_WAYLAND
  log_console("post-reset: event_efd=%d", g_bs.event_efd);
#endif

  BridgeCtrl *C = BRIDGE_CTRL_WL();
  C->opcode = OP_wl_display_connect;
  C->args_len = 0;
  C->data_offset = 0;
  C->data_size = 0;

  uint32_t slot = (uint32_t)bridge_send_call_wl();

  if (!slot)
  {
    log_error("wl_display_connect: proxy FAILED");
    return NULL;
  }

  const uint32_t *rb = (const uint32_t *)C->result_buf;
  g_bs.num_seats = rb[1] ? rb[1] : 1;
  g_bs.num_outputs = rb[2] ? rb[2] : 1;
  g_bs.id_watermark = rb[3];

  /* webos_seat info */
  g_bs.webos_seat_received = rb[4];
  g_bs.webos_seat_id = rb[5];
  g_bs.webos_seat_designator = rb[6];
  g_bs.webos_seat_capabilities = rb[7];
  strncpy(g_bs.webos_seat_name, (const char *)&rb[8],
          sizeof(g_bs.webos_seat_name) - 1);
#ifdef DEBUG_WAYLAND
  log_console("wl_display_connect: webos_seat received=%u name='%s'",
              g_bs.webos_seat_received, g_bs.webos_seat_name);
#endif
  g_bridge_displays[slot].magic = BRIDGE_MAGIC;
  g_bridge_displays[slot].slot = slot;
  g_active_bridge_displays++;

#ifdef DEBUG_WAYLAND
  log_console("connected: num_seats=%u num_outputs=%u id_watermark=%u disp "
              "slot=%u g_active_bridge_displays=%d",
              g_bs.num_seats, g_bs.num_outputs, g_bs.id_watermark, slot,
              g_active_bridge_displays);
#endif
  return (struct wl_display *)&g_bridge_displays[slot];
}

void wl_display_disconnect(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
  {
    log_error("wl_display_disconnect() - calling REAL disconnect");
    REAL(wl_display_disconnect)(dpy);
    return;
  }
  BridgeDisplay *bd = (BridgeDisplay *)dpy;
  uint32_t slot = bd->slot;

#ifdef DEBUG_WAYLAND
  log_console("wl_display_disconnect() %p fd=%d disp slot=%d", dpy,
              wl_display_get_fd(dpy), slot);
#endif

  BRIDGE_BEGIN_WL();
  BridgeCtrl *C = BRIDGE_CTRL_WL();
  C->opcode = OP_wl_display_disconnect;

  ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
  aw_u32(&W, slot);
  C->args_len = W.pos;

  C->data_offset = 0;
  C->data_size = 0;
  C->data2_offset = 0;
  C->data2_size = 0;

  bridge_send_call_wl();

  /* Only close the shared event fd when the LAST active bridge display
   * disconnects.  Closing it prematurely kills wl_display_get_fd() for
   * every other active display (they all share g_bs.event_efd). */
  if (--g_active_bridge_displays <= 0)
  {
    close(g_bs.event_efd);
    g_bs.event_efd = -1;
    g_active_bridge_displays = 0;
    if (g_bs.event_efd > 0)
    {
      close(g_bs.event_efd);
      g_bs.event_efd = -1;
    }
  }
}

int wl_display_get_fd(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_get_fd)(dpy);
  return g_bs.event_efd;
}

int wl_display_dispatch(struct wl_display *dpy)
{
  BridgeDisplay *bd = (BridgeDisplay *)dpy;
  uint32_t slot = -1;

  if (bd)
    slot = bd->slot;

#ifdef DEBUG_WAYLAND
  log_console("wl_display_dispatch() dpy: %p fd=%d disp slot=%d", dpy,
              wl_display_get_fd(dpy), slot);
#endif

  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_dispatch)(dpy);
  _bridge_dispatch();
  return 0;
}

int wl_display_dispatch_pending(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_dispatch_pending)(dpy);
  _bridge_dispatch();
  return 0;
}

int wl_display_dispatch_queue(struct wl_display *dpy, struct wl_event_queue *q)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_dispatch_queue)(dpy, q);
  _bridge_dispatch();
  return 0;
}

int wl_display_dispatch_queue_pending(struct wl_display *dpy,
                                      struct wl_event_queue *q)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_dispatch_queue_pending)(dpy, q);
  _bridge_dispatch();
  return 0;
}

int wl_display_roundtrip(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_roundtrip)(dpy);
  _bridge_dispatch();
  return 0;
}

int wl_display_flush(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_flush)(dpy);
  BRIDGE_BEGIN_WL();
  BRIDGE_CTRL_WL()->opcode = OP_wl_flush;
  BRIDGE_CTRL_WL()->args_len = 0;
  bridge_send_void_wl();
  return 0;
}

int wl_display_prepare_read(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_prepare_read)(dpy);
  return 0;
}

int wl_display_prepare_read_queue(struct wl_display *dpy,
                                  struct wl_event_queue *q)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_prepare_read_queue)(dpy, q);
  return 0;
}

void wl_display_cancel_read(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    REAL(wl_display_cancel_read)(dpy);
}

int wl_display_read_events(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_read_events)(dpy);
  _bridge_dispatch();
  return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * wl_proxy
 * ══════════════════════════════════════════════════════════════════════════ */

uint32_t wl_proxy_get_version(struct wl_proxy *proxy)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_get_version)(proxy);
  return 4;
}

/*
 * wl_proxy_get_id — called by egl_stub to get the surface "wire ID" which
 * we repurpose as the proxy slot index.  With the slot map, each surface
 * handle returns the correct proxy slot rather than always returning the
 * last-created slot.
 */
uint32_t wl_proxy_get_id(struct wl_proxy *proxy)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_get_id)(proxy);
  if (IS_BRIDGE_DPY(proxy))
    return 1;

  uint32_t h = P2H(proxy);

  /* Watermark probes from egl_stub (wl_display_sync returns BH_DUMMY_SYNC) */
  if (h >= 0xF0)
    return g_bs.id_watermark;

  /* Surface handles — return the specific proxy slot for this surface */
  if (h >= BH_SURFACE_BASE && h < BH_SHELL_SURF_BASE)
  {
#ifdef DEBUG_WAYLAND
    log_console("wl_proxy_get_id - slot_for_surface - h=%d slot=%d", h,
                slot_for_surface(h));
#endif
    return slot_for_surface(h);
  }

  // return g_bs.surface_proxy_slot;
  //  wont work for proxy owns EGL/WL as retroarch
  //  egl_create_surface(native_window=0x103e64c0) is new
#ifdef DEBUG_WAYLAND
  log_console("wl_proxy_get_id - slot_for_surface - h=%d returning 0", h);
#endif

  return 0;
}

void wl_proxy_destroy(struct wl_proxy *proxy)
{
  if (!IS_BRIDGE_PROXY(proxy))
  {
    REAL(wl_proxy_destroy)(proxy);
    return;
  }
  uint32_t h = P2H(proxy);
  /* Free callback slot */
  if (is_callback_handle(h))
  {
    uint32_t idx = h - BH_CALLBACK_BASE;
    g_callbacks[idx].funcs = NULL;
    g_callbacks[idx].data = NULL;
    g_callbacks[idx].pending = 0;
  }
}

int wl_proxy_add_listener(struct wl_proxy *proxy, void (**impl)(void),
                          void *data)
{
#ifdef DEBUG_WAYLAND
  log_console("wl_proxy_add_listener h=%u proxy=%p data=%p", P2H(proxy), proxy,
              data);
#endif

  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_add_listener)(proxy, impl, data);

  uint32_t h = P2H(proxy);

  if (h == BH_REGISTRY)
  {
#ifdef DEBUG_WAYLAND
    log_console("wl_proxy_add_listener BH_REGISTRY h=%u proxy=%p data=%p", h,
                proxy, data);
#endif
    g_bs.reg_listener = (const struct wl_registry_listener *)impl;
    g_bs.reg_listener_data = data;
    return 0;
  }
  if (h >= BH_SEAT_BASE && h < BH_SEAT_BASE + MAX_SEATS)
  {
    uint32_t idx = h - BH_SEAT_BASE;
#ifdef DEBUG_WAYLAND
    log_console("wl_proxy_add_listener BH_SEAT h=%u idx=%u proxy=%p data=%p", h,
                idx, proxy, data);
#endif
    g_bs.seats[idx].funcs = impl;
    g_bs.seats[idx].data = data;
    _fire_seat_capabilities(idx);
    return 0;
  }
  if (h >= BH_OUTPUT_BASE && h < BH_OUTPUT_BASE + MAX_OUTPUTS)
  {
    uint32_t idx = h - BH_OUTPUT_BASE;
#ifdef DEBUG_WAYLAND
    log_console("wl_proxy_add_listener BH_OUTPUT h=%u idx=%u proxy=%p data=%p",
                h, idx, proxy, data);
#endif
    g_bs.outputs[idx].funcs = impl;
    g_bs.outputs[idx].data = data;
    /* Don't fire output events inline (we may be inside a global callback).
     * Mark as pending so they fire on the next dispatch/roundtrip call.  */
    g_bs.output_events_pending[idx] = 1;
    return 0;
  }
  if (h >= BH_WEBOS_SURF_BASE && h < BH_WEBOS_SURF_BASE + 100)
  {
    uint32_t idx = h - BH_WEBOS_SURF_BASE;
#ifdef DEBUG_WAYLAND
    log_console(
        "wl_proxy_add_listener BH_WEBOS_SURF h=%u idx=%u proxy=%p data=%p", h,
        idx, proxy, data);
#endif
    /* Store both globally (legacy) and per-surface */
    g_bs.webos_ss_listener = impl;
    g_bs.webos_ss_listener_data = data;
    if (idx < SURF_MAP_MAX)
    {
      g_bs.webos_ss_listeners[idx] = impl;
      g_bs.webos_ss_listener_data_arr[idx] = data;
    }
    return 0;
  }
  if (h >= BH_KEYBOARD_BASE && h < BH_KEYBOARD_BASE + MAX_SEATS)
  {
    uint32_t idx = h - BH_KEYBOARD_BASE;
#ifdef DEBUG_WAYLAND
    log_console(
        "wl_proxy_add_listener BH_KEYBOARD h=%u idx=%u proxy=%p data=%p", h,
        idx, proxy, data);
#endif
    g_bs.keyboards[idx].funcs = impl;
    g_bs.keyboards[idx].data = data;
    return 0;
  }
  if (h >= BH_POINTER_BASE && h < BH_POINTER_BASE + MAX_SEATS)
  {
    uint32_t idx = h - BH_POINTER_BASE;
#ifdef DEBUG_WAYLAND
    log_console("wl_proxy_add_listener BH_POINTER h=%u idx=%u proxy=%p data=%p",
                h, idx, proxy, data);
#endif
    g_bs.pointers[idx].funcs = impl;
    g_bs.pointers[idx].data = data;
    return 0;
  }

  /* starfish / foreign / dummy objects: accept listener silently */
  if (h == BH_STARFISH_POINTER || h == BH_STARFISH_OUTPUT ||
      h == BH_WEBOS_FOREIGN || h == BH_DUMMY)
  {
#ifdef DEBUG_WAYLAND
    log_console(
        "wl_proxy_add_listener BH_STARFISH_POINTER/OUTPUT/FOREIGN/DUMMY h=%u "
        "proxy=%p data=%p",
        h, proxy, data);
#endif
    return 0;
  }

  /* wl_callback */
  if (is_callback_handle(h))
  {
    uint32_t idx = h - BH_CALLBACK_BASE;
    g_callbacks[idx].funcs = impl;
    g_callbacks[idx].data = data;
    g_bs.callback_pending[idx] = 1;
    return 0;
  }

  if (h >= BH_WEBOS_SEAT_BASE && h < BH_WEBOS_SEAT_BASE + MAX_SEATS)
  {
    uint32_t idx = h - BH_WEBOS_SEAT_BASE;
#ifdef DEBUG_WAYLAND
    log_console(
        "wl_proxy_add_listener BH_WEBOS_SEAT h=%u idx=%u proxy=%p data=%p", h,
        idx, proxy, data);
#endif
    g_bs.webos_seat_listeners[idx].funcs = impl;
    g_bs.webos_seat_listeners[idx].data = data;
    /* Fire info immediately if we have it */
    if (g_bs.webos_seat_received && !g_bs.webos_seat_info_fired[idx])
    {
      g_bs.webos_seat_info_fired[idx] = 1;
      typedef void (*info_fn)(void *, struct wl_webos_seat *, uint32_t,
                              const char *, uint32_t, uint32_t);
      if (impl[0])
        ((info_fn)impl[0])(
            data, (struct wl_webos_seat *)H2P(BH_WEBOS_SEAT_BASE + idx),
            g_bs.webos_seat_id, g_bs.webos_seat_name,
            g_bs.webos_seat_designator, g_bs.webos_seat_capabilities);
    }
    return 0;
  }

  return 0;
}

/*
 * wl_proxy_marshal_flags — intercepts all generated protocol helpers.
 */
struct wl_proxy *wl_proxy_marshal_flags(struct wl_proxy *proxy, uint32_t opcode,
                                        const struct wl_interface *interface,
                                        uint32_t version, uint32_t flags, ...)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_marshal_flags)(proxy, opcode, interface, version,
                                        flags);

  va_list ap;
  va_start(ap, flags);

  /* ── Display-level constructors ─────────────────────────────────────── */
  if (IS_BRIDGE_DPY(proxy))
  {
    struct wl_proxy *ret = (struct wl_proxy *)H2P(BH_DUMMY_SYNC);
    if (interface && !strcmp(interface->name, "wl_registry"))
      ret = (struct wl_proxy *)H2P(BH_REGISTRY);
    va_end(ap);
    return ret;
  }

  uint32_t h = P2H(proxy);

  /* ── Registry bind ─────────────────────────────────────────────────── */
  if (h == BH_REGISTRY)
  {
    struct wl_proxy *ret = (struct wl_proxy *)H2P(BH_DUMMY);
    if (interface)
    {
      const char *n = interface->name;
      if (!strcmp(n, "wl_compositor"))
        ret = (struct wl_proxy *)H2P(BH_COMPOSITOR);
      else if (!strcmp(n, "wl_shell"))
        ret = (struct wl_proxy *)H2P(BH_SHELL);
      else if (!strcmp(n, "wl_webos_shell"))
        ret = (struct wl_proxy *)H2P(BH_WEBOS_SHELL);
      else if (!strcmp(n, "wl_shm"))
        ret = (struct wl_proxy *)H2P(BH_SHM);
      else if (!strcmp(n, "wl_seat"))
      {
        uint32_t idx = g_bs.seat_cnt++ & (MAX_SEATS - 1);
        ret = (struct wl_proxy *)H2P(BH_SEAT_BASE + idx);
      }
      else if (!strcmp(n, "wl_output"))
      {
        uint32_t idx = g_bs.out_cnt++ & (MAX_OUTPUTS - 1);
        ret = (struct wl_proxy *)H2P(BH_OUTPUT_BASE + idx);
      }
      else if (!strcmp(n, "wl_webos_input_manager"))
        ret = (struct wl_proxy *)H2P(BH_WEBOS_INPUT_MGR);
      else if (!strcmp(n, "wl_starfish_pointer"))
        ret = (struct wl_proxy *)H2P(BH_STARFISH_POINTER);
      else if (!strcmp(n, "wl_starfish_output"))
        ret = (struct wl_proxy *)H2P(BH_STARFISH_OUTPUT);
      else if (!strcmp(n, "wl_webos_foreign"))
        ret = (struct wl_proxy *)H2P(BH_WEBOS_FOREIGN);
    }
    va_end(ap);
    return ret;
  }

  /* ── wl_compositor::create_surface ─────────────────────────────────── */
  if (h == BH_COMPOSITOR && interface && !strcmp(interface->name, "wl_surface"))
  {
    va_end(ap);
    BRIDGE_BEGIN_WL();
    BridgeCtrl *C = BRIDGE_CTRL_WL();
    C->opcode = OP_wl_compositor_create_surface;
    C->args_len = 0;
    C->data_offset = 0;
    C->data_size = 0;
    uint32_t surf_slot = (uint32_t)bridge_send_call_wl();

    /* Record this slot for the specific surface handle */
    uint32_t surf_h = g_bs.next_surface++;
    uint32_t idx = surf_h - BH_SURFACE_BASE;
    if (idx < SURF_MAP_MAX)
      g_bs.surface_slot_map[idx] = surf_slot;
    g_bs.surface_proxy_slot = surf_slot; /* legacy: keep last slot */

#ifdef DEBUG_WAYLAND
    log_console("stub=%u idx=%u surf_slot=%u map[%u]=%u", surf_h, idx,
                surf_slot, idx, g_bs.surface_slot_map[idx]);
#endif

    return (struct wl_proxy *)H2P(surf_h);
  }

  /* ── wl_shell::get_shell_surface ────────────────────────────────────── */
  if (h == BH_SHELL && interface &&
      !strcmp(interface->name, "wl_shell_surface"))
  {
    (void)va_arg(ap, struct wl_proxy *); /* NULL new-id placeholder */
    struct wl_proxy *surf = va_arg(ap, struct wl_proxy *);
    va_end(ap);
    uint32_t slot = slot_for_surface(P2H(surf));
    assert(slot != 0);
    BRIDGE_BEGIN_WL();
    BridgeCtrl *C = BRIDGE_CTRL_WL();
    C->opcode = OP_wl_shell_get_shell_surface;
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
    aw_u32(&W, slot);
    C->args_len = W.pos;
    C->data_offset = 0;
    C->data_size = 0;
    bridge_send_call_wl();
    return (struct wl_proxy *)H2P(BH_SHELL_SURF_BASE +
                                  (P2H(surf) - BH_SURFACE_BASE));
  }

  /* ── wl_webos_shell::get_shell_surface (opcode 1) ───────────────────── */
  if (h == BH_WEBOS_SHELL && opcode == 1 && interface &&
      !strcmp(interface->name, "wl_webos_shell_surface"))
  {
    (void)va_arg(ap, struct wl_proxy *); /* NULL new-id placeholder */
    struct wl_proxy *surf = va_arg(ap, struct wl_proxy *);
    va_end(ap);
    uint32_t slot = slot_for_surface(P2H(surf));
    assert(slot != 0);
    BRIDGE_BEGIN_WL();
    BridgeCtrl *C = BRIDGE_CTRL_WL();
    C->opcode = OP_wl_webos_shell_get_shell_surface;
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
    aw_u32(&W, slot);
    C->args_len = W.pos;
    C->data_offset = 0;
    C->data_size = 0;
    bridge_send_call_wl();
    return (struct wl_proxy *)H2P(BH_WEBOS_SURF_BASE +
                                  (P2H(surf) - BH_SURFACE_BASE));
  }

  /* ── wl_shell_surface::set_toplevel (opcode 3) ──────────────────────── */
  if (h >= BH_SHELL_SURF_BASE && h < BH_WEBOS_SURF_BASE && opcode == 3 &&
      !interface)
  {
    va_end(ap);
    uint32_t slot = slot_for_shellsurf(h);
    assert(slot != 0);
    BRIDGE_BEGIN_WL();
    BridgeCtrl *C = BRIDGE_CTRL_WL();
    C->opcode = OP_wl_shell_surface_set_toplevel;
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
    aw_u32(&W, slot);
    C->args_len = W.pos;
    C->data_offset = 0;
    C->data_size = 0;
    bridge_send_void_wl();
    return NULL;
  }

  /* ── wl_webos_shell_surface::set_state (opcode 1) ──────────────────────
   * Called by client when going fullscreen/windowed. */
  if (h >= BH_WEBOS_SURF_BASE && h < BH_WEBOS_SURF_BASE + 100 && opcode == 1 &&
      !interface)
  {
    uint32_t state = va_arg(ap, uint32_t);
    va_end(ap);
    uint32_t slot = slot_for_webosurf(h);

#ifdef DEBUG_WAYLAND
    log_console("wl_proxy_marshal_flags - OP_wl_webos_shell_surface_set_state "
                "- state=%d slot=%d",
                state, slot);
#endif
    if (slot)
    {
      BRIDGE_BEGIN_WL();
      BridgeCtrl *C = BRIDGE_CTRL_WL();
      C->opcode = OP_wl_webos_shell_surface_set_state;
      ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
      aw_u32(&W, slot);
      aw_u32(&W, state);
      C->args_len = W.pos;
      C->data_offset = 0;
      C->data_size = 0;
      bridge_send_void_wl();
    }
    return NULL;
  }

  /* ── wl_webos_shell_surface::set_property (opcode 2) ───────────────────
   * write name and value as ONE contiguous block so the proxy's
   * value = name + strlen(name) + 1                  */
  if (h >= BH_WEBOS_SURF_BASE && h < BH_WEBOS_SURF_BASE + 100 && opcode == 2 &&
      !interface)
  {
    const char *name = va_arg(ap, const char *);
    const char *value = va_arg(ap, const char *);
    va_end(ap);
    if (name && value)
    {
#ifdef DEBUG_WAYLAND
      log_console("wl_proxy_marshal_flags: name:%s value:%s", name, value);
#endif

      size_t nlen = strlen(name) + 1;
      size_t vlen = strlen(value) + 1;
      size_t total = nlen + vlen;
      char *blob = (char *)alloca(total);
      memcpy(blob, name, nlen);
      memcpy(blob + nlen, value, vlen);
      uint32_t slot = slot_for_webosurf(h);
      assert(slot != 0);
      BRIDGE_BEGIN_WL();
      BridgeCtrl *C = BRIDGE_CTRL_WL();
      C->opcode = OP_wl_webos_set_property;
      ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
      aw_u32(&W, slot);
      C->args_len = W.pos;
      uint32_t off = bridge_data_write_wl(blob, total);
      C->data_offset = off;
      C->data_size = (uint32_t)total;
      bridge_send_void_wl();
    }
    return NULL;
  }

  /* ── wl_surface::frame (opcode 3) — returns a wl_callback ─────────── */
  if (h >= BH_SURFACE_BASE && h < BH_SHELL_SURF_BASE && opcode == 3 &&
      interface)
  {
    va_end(ap);
    uint32_t idx = alloc_callback();
    g_callbacks[idx].pending = 1;
    return (struct wl_proxy *)H2P(BH_CALLBACK_BASE + idx);
  }

  /* ── wl_surface::commit (opcode 6) ─────────────────────────────────── */
  if (h >= BH_SURFACE_BASE && h < BH_SHELL_SURF_BASE && opcode == 6 &&
      !interface)
  {
    va_end(ap);
    uint32_t slot = slot_for_surface(h);
    assert(slot != 0);
    BRIDGE_BEGIN_WL();
    BridgeCtrl *C = BRIDGE_CTRL_WL();
    C->opcode = OP_wl_surface_commit;
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
    aw_u32(&W, slot);
    C->args_len = W.pos;
    C->data_offset = 0;
    C->data_size = 0;
    bridge_send_void_wl();
    return NULL;
  }

  /* ── wl_surface::destroy (opcode 0) ────────────────────────────────── */
  if (h >= BH_SURFACE_BASE && h < BH_SHELL_SURF_BASE && opcode == 0 &&
      !interface)
  {
    va_end(ap);
    uint32_t slot = slot_for_surface(h);
    assert(slot != 0);
    BRIDGE_BEGIN_WL();
    BridgeCtrl *C = BRIDGE_CTRL_WL();
    C->opcode = OP_wl_surface_destroy;
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
    aw_u32(&W, slot);
    C->args_len = W.pos;
    C->data_offset = 0;
    C->data_size = 0;
    bridge_send_void_wl();
    return NULL;
  }

  /* ── wl_seat::get_keyboard (op 0) / get_pointer (op 1) ─────────────── */
  if (h >= BH_SEAT_BASE && h < BH_SEAT_BASE + MAX_SEATS && interface)
  {
    uint32_t idx = h - BH_SEAT_BASE;
    va_end(ap);
    if (!strcmp(interface->name, "wl_keyboard"))
      return (struct wl_proxy *)H2P(BH_KEYBOARD_BASE + idx);
    if (!strcmp(interface->name, "wl_pointer"))
      return (struct wl_proxy *)H2P(BH_POINTER_BASE + idx);
    return (struct wl_proxy *)H2P(BH_DUMMY);
  }

  /* ── wl_starfish_pointer / wl_starfish_output: accept any opcode, return
   * BH_DUMMY so callers get a non-NULL handle to store listeners on       */
  if (h == BH_STARFISH_POINTER || h == BH_STARFISH_OUTPUT)
  {
    va_end(ap);
    return interface ? (struct wl_proxy *)H2P(BH_DUMMY) : NULL;
  }

  /* ── wl_webos_foreign: export_element (op 1) returns a dummy exported
   * handle; import_element (op 2) returns a dummy imported handle.
   * destroy (op 0) is a void.                                             */
  if (h == BH_WEBOS_FOREIGN)
  {
    va_end(ap);
    if (!interface)
      return NULL; /* destroy or void opcode */
    /* Both export_element and import_element return a new object */
    return (struct wl_proxy *)H2P(BH_DUMMY);
  }

  /* wl_webos_input_manager::get_webos_seat */
  if (h == BH_WEBOS_INPUT_MGR && interface &&
      !strcmp(interface->name, "wl_webos_seat"))
  {
    (void)va_arg(ap, struct wl_proxy *); /* new_id placeholder */
    struct wl_proxy *seat = va_arg(ap, struct wl_proxy *);
    va_end(ap);
    uint32_t sh = P2H(seat);
    uint32_t idx = (sh >= BH_SEAT_BASE && sh < BH_SEAT_BASE + MAX_SEATS)
                       ? (sh - BH_SEAT_BASE)
                       : 0;
    return (struct wl_proxy *)H2P(BH_WEBOS_SEAT_BASE + idx);
  }

  /* ── wl_callback::destroy (opcode 0, no interface) ────────────────── */
  if (is_callback_handle(h) && !interface)
  {
    va_end(ap);
    uint32_t idx = h - BH_CALLBACK_BASE;
    g_callbacks[idx].funcs = NULL;
    g_callbacks[idx].data = NULL;
    g_callbacks[idx].pending = 0;
    return NULL;
  }

  /* ── generic wl_surface opcodes SDL2 calls (attach, damage, scale …) ─ */
  if (h >= BH_SURFACE_BASE && h < BH_SHELL_SURF_BASE)
  {
    va_end(ap);
    /* constructor opcodes return a dummy; void opcodes return NULL */
    return interface ? (struct wl_proxy *)H2P(BH_DUMMY) : NULL;
  }

  va_end(ap);
  if (interface)
    return (struct wl_proxy *)H2P(BH_DUMMY);
  return NULL;
}

struct wl_proxy *
wl_proxy_marshal_constructor(struct wl_proxy *proxy, uint32_t opcode,
                             const struct wl_interface *interface, ...)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_marshal_constructor)(proxy, opcode, interface);
  return wl_proxy_marshal_flags(proxy, opcode, interface, 0, 0);
}

struct wl_proxy *
wl_proxy_marshal_array_constructor(struct wl_proxy *proxy, uint32_t opcode,
                                   union wl_argument *args,
                                   const struct wl_interface *interface)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_marshal_array_constructor)(proxy, opcode, args,
                                                    interface);
  return wl_proxy_marshal_flags(proxy, opcode, interface, 0, 0);
}

struct wl_proxy *
wl_proxy_marshal_constructor_versioned(struct wl_proxy *proxy, uint32_t opcode,
                                       const struct wl_interface *interface,
                                       uint32_t version, ...)
{
  if (!IS_BRIDGE_PROXY(proxy))
  {
    va_list ap;
    va_start(ap, version);
    uint32_t a0 = va_arg(ap, uint32_t);
    const char *a1 = va_arg(ap, const char *);
    uint32_t a2 = va_arg(ap, uint32_t);
    va_end(ap);
    union wl_argument args[3];
    args[0].u = a0;
    args[1].s = a1;
    args[2].u = a2;
    return REAL(wl_proxy_marshal_array_constructor_versioned)(
        proxy, opcode, args, interface, version);
  }
  return wl_proxy_marshal_flags(proxy, opcode, interface, version, 0);
}

struct wl_proxy *wl_proxy_marshal_array_constructor_versioned(
    struct wl_proxy *proxy, uint32_t opcode, union wl_argument *args,
    const struct wl_interface *interface, uint32_t version)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_marshal_array_constructor_versioned)(
        proxy, opcode, args, interface, version);
  return wl_proxy_marshal_flags(proxy, opcode, interface, version, 0);
}

void wl_proxy_marshal(struct wl_proxy *proxy, uint32_t opcode, ...)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return;
  uint32_t h = P2H(proxy);
  /* wl_surface::commit via legacy wl_proxy_marshal (opcode 6) */
  if (h >= BH_SURFACE_BASE && h < BH_SHELL_SURF_BASE && opcode == 6)
  {
    uint32_t slot = slot_for_surface(h);
    assert(slot != 0);
    BRIDGE_BEGIN_WL();
    BridgeCtrl *C = BRIDGE_CTRL_WL();
    C->opcode = OP_wl_surface_commit;
    ArgWriter W = aw_init(C->args, BRIDGE_ARGS_SIZE);
    aw_u32(&W, slot);
    C->args_len = W.pos;
    C->data_offset = 0;
    C->data_size = 0;
    bridge_send_void_wl();
  }
}

/* ══════════════════════════════════════════════════════════════════════════
 * Cursor — return NULL when called with bridge objects
 * ══════════════════════════════════════════════════════════════════════════ */

struct wl_cursor_theme *wl_cursor_theme_load(const char *name, int size,
                                             struct wl_shm *shm)
{
  if (!IS_BH(shm))
    return REAL(wl_cursor_theme_load)(name, size, shm);
  return NULL;
}

struct wl_cursor *wl_cursor_theme_get_cursor(struct wl_cursor_theme *t,
                                             const char *name)
{
  (void)t;
  (void)name;
  return NULL;
}

void wl_cursor_theme_destroy(struct wl_cursor_theme *t)
{
  if (t)
    REAL(wl_cursor_theme_destroy)(t);
}

/* ── Per-proxy user-data table ───────────────────────────────────────────── */
/* Bridge proxy handles are small integers cast to pointers (max ~512). */
#define MAX_PROXY_USERDATA 512
static void *g_proxy_userdata[MAX_PROXY_USERDATA];

void wl_proxy_set_user_data(struct wl_proxy *proxy, void *user_data)
{
#ifdef DEBUG_WAYLAND
  log_console("wl_proxy_set_user_data: proxy=%p user_data=%p", proxy,
              user_data);
#endif

  if (!IS_BRIDGE_PROXY(proxy))
  {
    REAL(wl_proxy_set_user_data)(proxy, user_data);
    return;
  }
  uint32_t h = P2H(proxy);
  if (h < MAX_PROXY_USERDATA)
    g_proxy_userdata[h] = user_data;
}

void *wl_proxy_get_user_data(struct wl_proxy *proxy)
{
#ifdef DEBUG_WAYLAND
  log_console("wl_proxy_get_user_data: proxy=%p", proxy);
#endif
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_get_user_data)(proxy);
  uint32_t h = P2H(proxy);
  if (h < MAX_PROXY_USERDATA)
    return g_proxy_userdata[h];
  return NULL;
}

void wl_proxy_set_tag(struct wl_proxy *proxy, const char *const *tag)
{
  if (!IS_BRIDGE_PROXY(proxy))
  {
    REAL(wl_proxy_set_tag)(proxy, tag);
    return;
  }

  uint32_t h = P2H(proxy);

  if (h < MAX_PROXY_TAGS)
    g_proxy_tags[h] = tag;
}

const char *const *wl_proxy_get_tag(struct wl_proxy *proxy)
{
  if (!IS_BRIDGE_PROXY(proxy))
    return REAL(wl_proxy_get_tag)(proxy);

  uint32_t h = P2H(proxy);

  if (h < MAX_PROXY_TAGS)
    return g_proxy_tags[h];

  return NULL;
}

struct wl_event_queue *wl_display_create_queue(struct wl_display *dpy)
{
  if (!IS_BRIDGE_DPY(dpy))
    return REAL(wl_display_create_queue)(dpy);
  return BRIDGE_DUMMY_QUEUE;
}

void wl_event_queue_destroy(struct wl_event_queue *queue)
{
  if (queue && queue != BRIDGE_DUMMY_QUEUE)
    REAL(wl_event_queue_destroy)(queue);
}

/* wl_proxy_create_wrapper */
void *wl_proxy_create_wrapper(void *proxy)
{
  if (!IS_BRIDGE_PROXY((struct wl_proxy *)proxy))
    return REAL(wl_proxy_create_wrapper)(proxy);
  /* Bridge handle: return it unchanged — no real proxy to wrap */
  return (void *)proxy;
}

void wl_proxy_wrapper_destroy(void *proxy_wrapper)
{
  if (!IS_BRIDGE_PROXY(proxy_wrapper))
    REAL(wl_proxy_wrapper_destroy)(proxy_wrapper);
  /* Bridge handle: nothing to destroy */
}

void wl_proxy_set_queue(struct wl_proxy *proxy, struct wl_event_queue *queue)
{
  if (!IS_BRIDGE_PROXY(proxy))
    REAL(wl_proxy_set_queue)(proxy, queue);
  /* Bridge proxy: queue concept doesn't apply, silently ignore */
}
