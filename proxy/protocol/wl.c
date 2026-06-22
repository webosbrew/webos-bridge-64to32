#include "../proxy.h"

#define LOG_PREFIX "[proxy/wl]"

extern const struct wl_interface wl_surface_interface;
extern const struct wl_interface wl_webos_shell_surface_interface;

/* ══════════════════════════════════════════════════════════════════════════
 * Proxy-side Wayland object tables
 *
 * The stub drives all Wayland object creation by forwarding client's
 * protocol calls.  We keep per-slot arrays so we can look up real pointers
 * when EGL needs the wl_surface, or when set_property/commit are called.
 *
 * Slot 0 is used for the first (and usually only) surface created.
 * ══════════════════════════════════════════════════════════════════════════ */
struct wl_surface *g_surfs[PROXY_SURF_MAX]; // was proxy_wl_surface
pid_t g_surfs_owner[PROXY_SURF_MAX];
struct wl_shell_surface *g_shell_surfs[PROXY_SURF_MAX];
pid_t g_shell_surfs_owner[PROXY_SURF_MAX];
struct wl_webos_shell_surface *g_webos_shell_surfaces[PROXY_SURF_MAX];
pid_t g_webos_shell_surfaces_owner[PROXY_SURF_MAX];
static uint32_t g_surf_next = PROXY_SURF_NEXT;

/* ── Proxy-side Wayland state ────────────────────────────────────────────── */
/* ── globals ───────────────────────────────────────────────────────────── */
struct wl_display *proxy_wl_display = NULL;
struct wl_compositor *proxy_wl_compositor = NULL;
struct wl_shell *proxy_wl_shell = NULL;
struct wl_webos_shell *proxy_wl_webos_shell = NULL;
struct wl_registry *proxy_wl_registry = NULL;
static struct wl_webos_input_manager *proxy_webos_input_manager = NULL;

static struct wl_display *display_table[EGL_BRIDGE_MAX_DISPLAYS];
bool demo_surface_consumed = false;

static struct wl_output *proxy_wl_output = NULL;

typedef struct
{
  int32_t width, height;  /* current mode, pixels */
  int32_t refresh;        /* mHz */
  int32_t phys_w, phys_h; /* physical size, mm */
  int32_t scale;
  int received;
} ProxyOutputInfo;

static ProxyOutputInfo proxy_output_info_store = {0};

typedef struct
{
  uint32_t id;
  uint32_t designator;
  uint32_t capabilities;
  char name[256];
  int received;
} ProxyWebOSSeatInfo;

static ProxyWebOSSeatInfo proxy_webos_seat_info_store = {0};

/* wl_webos_shell_surface: 5 requests, 6 events (transform_configure is extra)
 */
static const struct wl_message _pxy_webos_ss_reqs[] = {
    {"set_location_hint", "u", NULL}, /* opcode 0 */
    {"set_state", "u", NULL},         /* opcode 1 */
    {"set_property", "ss", NULL},     /* opcode 2 */
    {"set_key_mask", "u", NULL},      /* opcode 3 */
    {"set_size", "ii", NULL},         /* opcode 4 */
};
static const struct wl_message _pxy_webos_ss_events[] = {
    {"state_changed", "u", NULL},
    {"position_changed", "ii", NULL},
    {"close", "", NULL},
    {"exposed", "a", NULL},
    {"state_about_to_change", "u", NULL},
    {"transform_configure", "2iii", NULL} /* undocumented extra event */
};
static const struct wl_interface _pxy_webos_ss_iface = {
    "wl_webos_shell_surface", 1, 5, _pxy_webos_ss_reqs, 6, _pxy_webos_ss_events,
};

static const struct wl_interface *_pxy_gss_types[] = {&_pxy_webos_ss_iface,
                                                      &wl_surface_interface};

static const struct wl_message _pxy_webos_shell_reqs[] = {
    {"get_system_pip", "", NULL},
    {"get_shell_surface", "no", _pxy_gss_types},
};
static const struct wl_interface _pxy_webos_shell_iface = {
    "wl_webos_shell", 2, 2, _pxy_webos_shell_reqs, 0, NULL,
};

/* ── width/height etc. of real wl ────────────────────────────────────────── */
static void _pxy_output_geometry(void *data, struct wl_output *wl_output,
                                 int32_t x, int32_t y, int32_t physical_width,
                                 int32_t physical_height, int32_t subpixel,
                                 const char *make, const char *model,
                                 int32_t transform)
{
  (void)wl_output;
  (void)x;
  (void)y;
  (void)subpixel;
  (void)make;
  (void)model;
  (void)transform;
  ProxyOutputInfo *info = (ProxyOutputInfo *)data;
  info->phys_w = physical_width;
  info->phys_h = physical_height;
}

static void _pxy_output_mode(void *data, struct wl_output *wl_output,
                             uint32_t flags, int32_t width, int32_t height,
                             int32_t refresh)
{
  (void)wl_output;
  ProxyOutputInfo *info = (ProxyOutputInfo *)data;
  if (flags & WL_OUTPUT_MODE_CURRENT)
  {
    info->width = width;
    info->height = height;
    info->refresh = refresh;
  }
}

static void _pxy_output_done(void *data, struct wl_output *wl_output)
{
  (void)wl_output;
  ((ProxyOutputInfo *)data)->received = 1;
}

static void _pxy_output_scale(void *data, struct wl_output *wl_output,
                              int32_t factor)
{
  (void)wl_output;
  ((ProxyOutputInfo *)data)->scale = factor;
}

static const struct wl_output_listener _pxy_output_listener = {
    _pxy_output_geometry, _pxy_output_mode, _pxy_output_done,
    _pxy_output_scale};

/* ── Listener for proxy's webOS shell surface ──── */

/*
 * Per-slot state tracking so h_wl_roundtrip can relay the most recent
 * state_changed event back to the stub.  We track the last state and a
 * "pending" flag that is cleared once the stub has consumed the event.
 */
typedef struct
{
  uint32_t last_state; /* last compositor state (e.g. FULLSCREEN=3) */
  int pending;         /* 1 = not yet delivered to stub              */
  uint32_t width;      /* last transform_configure width             */
  uint32_t height;     /* last transform_configure height            */
  int transform_pending;
} WSSState;

static WSSState g_wss_state[PROXY_SURF_MAX];

/* Helper: find the slot index for a wl_webos_shell_surface pointer */
static int _wss_slot(struct wl_webos_shell_surface *s)
{
  for (int i = 0; i < PROXY_SURF_MAX; i++)
    if (g_webos_shell_surfaces[i] == s)
      return i;
  return -1;
}

static void _wss_state_changed(void *d, struct wl_webos_shell_surface *s,
                               uint32_t v)
{
  (void)d;
#ifdef DEBUG_WAYLAND
  log_console("webos_ss event: state=%u surf=%p", v, s);
#endif
  int slot = _wss_slot(s);
  if (slot >= 0)
  {
    g_wss_state[slot].last_state = v;
    g_wss_state[slot].pending = 1;
  }
}

static void _wss_position_changed(void *d, struct wl_webos_shell_surface *s,
                                  int32_t x, int32_t y)
{
  (void)d;
  (void)s;
  (void)x;
  (void)y;
}

static void _wss_close(void *d, struct wl_webos_shell_surface *s)
{
  (void)d;
  (void)s;
  log_console("webos_ss event: close");
}

static void _wss_exposed(void *d, struct wl_webos_shell_surface *s,
                         struct wl_array *a)
{
  (void)d;
  (void)s;
  (void)a;
}

static void _wss_state_about_to_change(void *d,
                                       struct wl_webos_shell_surface *s,
                                       uint32_t v)
{
  (void)d;
  (void)s;
  (void)v;
}

static void _wss_transform_configure(void *d, struct wl_webos_shell_surface *s,
                                     int32_t w, int32_t h, int32_t transform)
{
  (void)d;
#ifdef DEBUG_WAYLAND
  log_console("webos_ss event: transform_configure s:%p %dx%d transform:%d", s,
              w, h, transform);
#endif
  int slot = _wss_slot(s);
  if (slot >= 0)
  {
    g_wss_state[slot].width = w;
    g_wss_state[slot].height = h;
    g_wss_state[slot].transform_pending = 1;
    /* A transform_configure from the compositor implies the surface is
     * now visible/fullscreen — treat it like state=FULLSCREEN (3) so the
     * stub gets unblocked even if state_changed fires slightly later.   */
    if (!g_wss_state[slot].pending)
    {
      g_wss_state[slot].last_state =
          3; /* WL_WEBOS_SHELL_SURFACE_STATE_FULLSCREEN */
      g_wss_state[slot].pending = 1;
    }
  }
}

/* Cast to void** so we can append a 6th pointer beyond the 5-field struct */
static void *_wss_listener_ptrs[6] = {
    (void *)_wss_state_changed,
    (void *)_wss_position_changed,
    (void *)_wss_close,
    (void *)_wss_exposed,
    (void *)_wss_state_about_to_change,
    (void *)_wss_transform_configure,
};

/* ── Input event buffer ─────────────────────────────────────────────────── */
typedef struct
{
  uint32_t key;
  uint32_t state;
} input_evt_t;

static input_evt_t g_input_evts[INPUT_EVT_MAX];
static uint32_t g_input_evt_count = 0;

static struct wl_seat *proxy_wl_seat = NULL;
static struct wl_keyboard *proxy_wl_keyboard = NULL;

static void _kbd_noop_keymap(void *d, struct wl_keyboard *k, uint32_t fmt,
                             int32_t fd, uint32_t sz)
{
  (void)d;
  (void)k;
  (void)fmt;
  close(fd);
  (void)sz;
}
static void _kbd_noop_enter(void *d, struct wl_keyboard *k, uint32_t ser,
                            struct wl_surface *s, struct wl_array *a)
{
  (void)d;
  (void)k;
  (void)ser;
  (void)s;
  (void)a;
}
static void _kbd_noop_leave(void *d, struct wl_keyboard *k, uint32_t ser,
                            struct wl_surface *s)
{
  (void)d;
  (void)k;
  (void)ser;
  (void)s;
}
static void _kbd_key(void *d, struct wl_keyboard *k, uint32_t serial,
                     uint32_t time, uint32_t key, uint32_t state)
{
#ifdef DEBUG_WAYLAND
  log_console("kbd event: key=%u state=%u", key, state);
#endif
  (void)d;
  (void)k;
  (void)serial;
  (void)time;
  if (g_input_evt_count < INPUT_EVT_MAX)
  {
    g_input_evts[g_input_evt_count].key = key;
    g_input_evts[g_input_evt_count].state = state;
    g_input_evt_count++;
  }
}
static void _kbd_noop_mods(void *d, struct wl_keyboard *k, uint32_t ser,
                           uint32_t dm, uint32_t lm, uint32_t lk, uint32_t grp)
{
  (void)d;
  (void)k;
  (void)ser;
  (void)dm;
  (void)lm;
  (void)lk;
  (void)grp;
}
static void _kbd_noop_repeat(void *d, struct wl_keyboard *k, int32_t rate,
                             int32_t delay)
{
  (void)d;
  (void)k;
  (void)rate;
  (void)delay;
}

static const struct wl_keyboard_listener _kbd_listener = {
    .keymap = _kbd_noop_keymap,
    .enter = _kbd_noop_enter,
    .leave = _kbd_noop_leave,
    .key = _kbd_key,
    .modifiers = _kbd_noop_mods,
    .repeat_info = _kbd_noop_repeat,
};

static void _seat_caps(void *d, struct wl_seat *seat, uint32_t caps)
{
  (void)d;
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD))
  {
#ifdef DEBUG_WAYLAND
    if (proxy_wl_keyboard)
      log_console("Warning: multiple proxy_wl_keyboard received, only using "
                  "the last one");
#endif
    proxy_wl_keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(proxy_wl_keyboard, &_kbd_listener, NULL);
#ifdef DEBUG_WAYLAND
    log_console("_seat_caps: keyboard subscribed %p", proxy_wl_keyboard);
#endif
  }
}
static void _seat_name(void *d, struct wl_seat *s, const char *n)
{
  (void)d;
  (void)s;
  (void)n;
}

static const struct wl_seat_listener _seat_listener = {
    .capabilities = _seat_caps,
    .name = _seat_name,
};

/* --- wl_webos_seat interface (proxy side) --- */
/* We only need the "info" event (index 0) */
typedef void (*pxy_webos_seat_info_fn)(void *, void *, uint32_t, const char *,
                                       uint32_t, uint32_t);
static void pxy_on_webos_seat_info(void *data, void *seat, uint32_t id,
                                   const char *name, uint32_t desig,
                                   uint32_t caps)
{
  ProxyWebOSSeatInfo *s = (ProxyWebOSSeatInfo *)data;
  s->id = id;
  s->designator = desig;
  s->capabilities = caps;
  if (name)
    strncpy(s->name, name, sizeof(s->name) - 1);
  else
    s->name[0] = '\0';
  s->received = 1;
  log_console("pxy_webos_seat_info: id=%u name='%s' desig=%u caps=%u", id,
              s->name, desig, caps);
}
static void *pxy_webos_seat_listener[1] = {(void *)pxy_on_webos_seat_info};

/* wl_webos_seat interface description for proxy */
static const struct wl_message _pxy_webos_seat_events[] = {
    {"info", "usuu", NULL},
};
static const struct wl_interface _pxy_webos_seat_iface = {
    "wl_webos_seat", 1, 0, NULL, 1, _pxy_webos_seat_events,
};

/* wl_webos_input_manager interface description for proxy */
static const struct wl_interface *_pxy_wbim_types[] = {
    &_pxy_webos_seat_iface,
    &wl_seat_interface,
};
static const struct wl_message _pxy_wbim_requests[] = {
    {"set_cursor_visibility", "u", NULL},
    {"get_webos_seat", "no", _pxy_wbim_types},
};
static void _pxy_wbim_noop_cursor_vis(void *d, void *mgr, uint32_t vis,
                                      void *seat)
{
  (void)d;
  (void)mgr;
  (void)vis;
  (void)seat;
}
static void *_pxy_wbim_listener[1] = {(void *)_pxy_wbim_noop_cursor_vis};

static const struct wl_message _pxy_webos_input_mgr_events[] = {
    /* cursor_visibility(visibility: uint, webos_seat: object|null) */
    {"cursor_visibility", "u?o", NULL},
};

static const struct wl_interface _pxy_webos_input_mgr_iface = {
    "wl_webos_input_manager",    1, 2, _pxy_wbim_requests, 1,
    _pxy_webos_input_mgr_events, /* was 0 — now 1 */
};

/* ── Registry listener — only binds globals we need ─────────────────────── */
static void _pxy_reg_add(void *d, struct wl_registry *reg, uint32_t name,
                         const char *iface, uint32_t ver)
{
#ifdef DEBUG_WAYLAND
  log_console("_pxy_reg_add name: %d iface: %s ver: %d", name, iface, ver);
#endif
  (void)d;
  if (!strcmp(iface, "wl_compositor"))
    proxy_wl_compositor =
        wl_registry_bind(reg, name, &wl_compositor_interface, 1);
  else if (!strcmp(iface, "wl_shell"))
    proxy_wl_shell = wl_registry_bind(reg, name, &wl_shell_interface, 1);
  else if (!strcmp(iface, "wl_webos_shell"))
    proxy_wl_webos_shell =
        wl_registry_bind(reg, name, &_pxy_webos_shell_iface, ver < 2 ? ver : 2);
  else if (!strcmp(iface, "wl_output"))
  {
#ifdef DEBUG_WAYLAND
    if (proxy_wl_output)
      log_console(
          "Warning: multiple wl_output globals, only using the last one");
#endif
    proxy_wl_output =
        wl_registry_bind(reg, name, &wl_output_interface, ver < 2 ? ver : 2);
    memset(&proxy_output_info_store, 0, sizeof(proxy_output_info_store));
    wl_output_add_listener(proxy_wl_output, &_pxy_output_listener,
                           &proxy_output_info_store);
  }
  else if (!strcmp(iface, "wl_seat"))
  {
#ifdef DEBUG_WAYLAND
    if (proxy_wl_seat)
      log_console("Warning: multiple wl_seat globals, only using the last one");
#endif
    proxy_wl_seat =
        wl_registry_bind(reg, name, &wl_seat_interface, ver < 4 ? ver : 4);
    wl_seat_add_listener(proxy_wl_seat, &_seat_listener, NULL);
  }
  else if (!strcmp(iface, "wl_webos_input_manager"))
  {
#ifdef DEBUG_WAYLAND
    log_console("binding wl_webos_input_manager global");
#endif
    proxy_webos_input_manager =
        wl_registry_bind(reg, name, &_pxy_webos_input_mgr_iface, 1);
    /* register noop listener so cursor_visibility events are dispatched */
    wl_proxy_add_listener((struct wl_proxy *)proxy_webos_input_manager,
                          (void (**)(void))_pxy_wbim_listener, NULL);
  }
}

static void _pxy_reg_del(void *d, struct wl_registry *r, uint32_t n)
{
  (void)d;
  (void)r;
  (void)n;
}
static const struct wl_registry_listener _pxy_reg_listener = {_pxy_reg_add,
                                                              _pxy_reg_del};

static uint32_t alloc_slot(void)
{
  for (uint32_t i = 1; i < EGL_BRIDGE_MAX_DISPLAYS; i++)
  {
    if (!display_table[i])
      return i;
  }

  log_console("no slot available");
  return 0;
}

/* ── Connect to Wayland and discover globals (no surface created here) ── */
void proxy_wayland_init(void)
{
#ifdef DEBUG_WAYLAND
  if (proxy_wl_display)
  {
    log_console("proxy_wayland_init: already exists? proxy_wl_display=%p fd=%d",
                proxy_wl_display, wl_display_get_fd(proxy_wl_display));
  }
#endif

  memset(g_surfs, 0, sizeof(g_surfs));
  memset(g_shell_surfs, 0, sizeof(g_shell_surfs));
  memset(g_webos_shell_surfaces, 0, sizeof(g_webos_shell_surfaces));
  g_surf_next = PROXY_SURF_NEXT;

  proxy_wl_compositor = NULL;
  proxy_wl_shell = NULL;
  proxy_wl_webos_shell = NULL;

  proxy_wl_display = wl_display_connect(NULL);

#ifdef DEBUG_WAYLAND
  log_console("wl_display_connect: created proxy_wl_display=%p",
              proxy_wl_display);
#endif

  if (!proxy_wl_display)
  {
    log_error("proxy_wayland_init: connect failed");
    return;
  }
#ifdef DEBUG_WAYLAND
  log_console("proxy_wayland_init: connected fd=%d now binding listeners..",
              wl_display_get_fd(proxy_wl_display));
#endif

  proxy_wl_registry = wl_display_get_registry(proxy_wl_display);
  wl_registry_add_listener(proxy_wl_registry, &_pxy_reg_listener, NULL);
  wl_display_dispatch(proxy_wl_display);
  wl_display_roundtrip(proxy_wl_display); // fires _pxy_reg_add - binds seat

  /* ── Collect webos_seat info ── */
  if (proxy_webos_input_manager && proxy_wl_seat)
  {
    /* get_webos_seat opcode = 1 */
    struct wl_proxy *wbs = wl_proxy_marshal_constructor(
        (struct wl_proxy *)proxy_webos_input_manager, 1, &_pxy_webos_seat_iface,
        NULL, proxy_wl_seat);

    if (wbs)
    {
      memset(&proxy_webos_seat_info_store, 0,
             sizeof(proxy_webos_seat_info_store));
      wl_proxy_add_listener(wbs, (void (**)(void))pxy_webos_seat_listener,
                            &proxy_webos_seat_info_store);
      wl_display_roundtrip(proxy_wl_display); /* fires info event */
    }
  }

  /* ── Collect real output mode/geometry ── */
  if (proxy_wl_output)
    wl_display_roundtrip(proxy_wl_display);

  if (!proxy_wl_compositor)
    log_error("proxy_wayland_init: WARNING no wl_compositor");
  if (!proxy_wl_webos_shell)
    log_error("proxy_wayland_init: WARNING no wl_webos_shell");

#ifdef DEBUG_WAYLAND
  log_console("proxy_wayland_init: globals ready (proxy_wl_display=%p "
              "proxy_wl_compositor=%p "
              "proxy_wl_shell=%p proxy_wl_webos_shell=%p)",
              proxy_wl_display, proxy_wl_compositor, proxy_wl_shell,
              proxy_wl_webos_shell);
  log_console("proxy_wayland_init: seat=%p keyboard=%p webos_seat_name='%s' "
              "received=%d",
              proxy_wl_seat, proxy_wl_keyboard,
              proxy_webos_seat_info_store.name,
              proxy_webos_seat_info_store.received);
#endif
}

void proxy_wayland_reset(void)
{
}

/* ── h_wl_display_connect ──────────────────────────────────────────────── */
void h_wl_display_connect(BridgeCtrl *C, uint8_t *D)
{
  proxy_wayland_init();
  proxy_wayland_reset();

  if (!proxy_wl_display)
  {
    C->result = 0;
    return;
  }

  /* Watermark: highest ID currently allocated on this connection */
  struct wl_callback *probe = wl_display_sync(proxy_wl_display);
  uint32_t id_watermark = wl_proxy_get_id((struct wl_proxy *)probe) + 8;
  wl_callback_destroy(probe);
  wl_display_roundtrip(proxy_wl_display);

  uint32_t *rb = (uint32_t *)C->result_buf;
  rb[0] = 0;
  rb[1] = 1; /* num_seats   */
  rb[2] = 1; /* num_outputs */
  rb[3] = id_watermark;
  rb[4] = proxy_webos_seat_info_store.received ? 1 : 0;
  rb[5] = proxy_webos_seat_info_store.id;
  rb[6] = proxy_webos_seat_info_store.designator;
  rb[7] = proxy_webos_seat_info_store.capabilities;

  rb[8] = (uint32_t)(proxy_output_info_store.width > 0
                         ? proxy_output_info_store.width
                         : 1920);
  rb[9] = (uint32_t)(proxy_output_info_store.height > 0
                         ? proxy_output_info_store.height
                         : 1080);
  rb[10] = (uint32_t)(proxy_output_info_store.refresh > 0
                          ? proxy_output_info_store.refresh
                          : 60000);
  rb[11] = (uint32_t)(proxy_output_info_store.phys_w > 0
                          ? proxy_output_info_store.phys_w
                          : 527);
  rb[12] = (uint32_t)(proxy_output_info_store.phys_h > 0
                          ? proxy_output_info_store.phys_h
                          : 296);
  rb[13] = (uint32_t)(proxy_output_info_store.scale > 0
                          ? proxy_output_info_store.scale
                          : 1);

#ifdef DEBUG_WAYLAND
  log_console(
      "h_wl_display_connect: proxy_output_info_store.width=%d "
      "proxy_output_info_store.height=%d proxy_output_info_store.refresh=%d",
      proxy_output_info_store.width, proxy_output_info_store.height,
      proxy_output_info_store.refresh);
#endif

  /* name as string starting at rb[14] */
  strncpy((char *)&rb[14], proxy_webos_seat_info_store.name,
          BRIDGE_RESULT_SIZE - 56 - 1);
  uint32_t allocated_slot = alloc_slot();
  display_table[allocated_slot] = proxy_wl_display;

#ifdef DEBUG_WAYLAND
  log_console("h_wl_display_connect: proxy_wl_display=%p id_watermark=%u disp "
              "slot(proxy)=%u",
              proxy_wl_display, id_watermark, allocated_slot);
#endif

  C->result = allocated_slot;
  (void)D;
}

void h_wl_display_disconnect(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);

#ifdef DEBUG
  log_console("h_wl_display_disconnect: disp slot=%u", slot);
#endif

  struct wl_display *dpy = display_table[slot];
  if (dpy)
  {
    log_console("calling REAL wl_display_disconnect for display: %p", dpy);
    wl_display_disconnect(dpy);
    display_table[slot] = NULL;
  }

  C->result = 0;
  (void)D;
}

/* ── h_wl_compositor_create_surface ────────────────────────────────────── */
void h_wl_compositor_create_surface(BridgeCtrl *C, uint8_t *D)
{
  if (!proxy_wl_compositor || g_surf_next >= PROXY_SURF_MAX)
  {
    log_error("h_wl_compositor_create_surface: FAILED (compositor=%p next=%u)",
              proxy_wl_compositor, g_surf_next);
    C->result = 0;
    return;
  }

  uint32_t slot = g_surf_next;
  struct wl_surface *surf = NULL;
  bool need_create_surface = true;

  if (need_create_surface)
  {
    g_surf_next++;
#ifdef DEBUG_WAYLAND
    log_console(
        "h_wl_compositor_create_surface: surf slot=%u need_create_surface=%d",
        slot, need_create_surface);
#endif
    surf = wl_compositor_create_surface(proxy_wl_compositor);
  }

  if (!surf)
  {
    log_error(
        "h_wl_compositor_create_surface: wl_compositor_create_surface FAILED");
    C->result = 0;
    return;
  }

  if (need_create_surface)
    g_surfs[slot] = surf;
  g_surfs_owner[slot] = C->client_pid;

#ifdef DEBUG_WAYLAND
  log_console(
      "h_wl_compositor_create_surface: display=%p compositor=%p surf slot=%u "
      "g_surfs[slot](surf)=%p wire_id=%u wl_display fd=%d",
      proxy_wl_display, proxy_wl_compositor, slot, surf,
      wl_proxy_get_id((struct wl_proxy *)surf),
      wl_display_get_fd(proxy_wl_display));
#endif

  C->result = slot;
  (void)D;
}

/* ── h_wl_shell_get_shell_surface ──────────────────────────────────────── */
void h_wl_shell_get_shell_surface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);
#ifdef DEBUG_WAYLAND
  log_console("h_wl_shell_get_shell_surface: slot=%u", slot);
#endif
  if (g_shell_surfs[slot])
  {
    log_console("h_wl_shell_get_shell_surface: reusing existing slot=%u", slot);
    C->result = slot;
    (void)D;
    return;
  }

  if (!proxy_wl_shell || slot >= PROXY_SURF_MAX || !g_surfs[slot])
  {
    log_error("h_wl_shell_get_shell_surface: FAILED slot=%u", slot);
    C->result = 0;
    return;
  }

  struct wl_shell_surface *shell_surface =
      wl_shell_get_shell_surface(proxy_wl_shell, g_surfs[slot]);
  g_shell_surfs[slot] = shell_surface;
  g_shell_surfs_owner[slot] = C->client_pid;

#ifdef DEBUG_WAYLAND
  log_console("h_wl_shell_get_shell_surface: slot=%u shell surf=%p", slot,
              shell_surface);
#endif

  C->result = slot;
  (void)D;
}

/* ── h_wl_shell_surface_set_toplevel ────────────────────────────────────── */
void h_wl_shell_surface_set_toplevel(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);

  if (slot < PROXY_SURF_MAX && g_shell_surfs[slot])
  {
#ifdef DEBUG_WAYLAND
    log_console("h_wl_shell_surface_set_toplevel: slot=%u shell surf=%p", slot,
                g_shell_surfs[slot]);
#endif
    wl_shell_surface_set_toplevel(g_shell_surfs[slot]);
  }
  C->result = 0;
  (void)D;
}

/* ── h_wl_webos_shell_get_shell_surface ─────────────────────────────────── */
void h_wl_webos_shell_get_shell_surface(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);

#ifdef DEBUG_WAYLAND
  log_console("h_wl_webos_shell_get_shell_surface: slot=%u shell surf=%p", slot,
              g_surfs[slot]);
#endif

  if (g_webos_shell_surfaces[slot])
  {
#ifdef DEBUG_WAYLAND
    log_console("h_wl_webos_shell_get_shell_surface: reusing existing slot=%u",
                slot);
#endif
    C->result = slot;
    (void)D;
    return;
  }

  if (!proxy_wl_webos_shell || slot >= PROXY_SURF_MAX || !g_surfs[slot])
  {
    log_error("h_wl_webos_shell_get_shell_surface: FAILED slot=%u", slot);
    C->result = 0;
    return;
  }

  /* Use the generated helper from webos-shell-32.h (opcode 1, returns
   * _pxy_webos_ss_iface) */
  struct wl_webos_shell_surface *wss =
      wl_webos_shell_get_shell_surface(proxy_wl_webos_shell, g_surfs[slot]);

  if (!wss)
  {
    log_error("h_wl_webos_shell_get_shell_surface: FAILED (returned NULL)");
    C->result = 0;
    return;
  }

  /* Clear state tracking for this slot before registering the listener */
  g_wss_state[slot].last_state = 0;
  g_wss_state[slot].pending = 0;
  g_wss_state[slot].width = 0;
  g_wss_state[slot].height = 0;
  g_wss_state[slot].transform_pending = 0;

  /* Register a listener with all 6 handlers so the extra event is never a
   * surprise */
  wl_proxy_add_listener((struct wl_proxy *)wss,
                        (void (**)(void))_wss_listener_ptrs, NULL);

  g_webos_shell_surfaces[slot] = wss;
  g_webos_shell_surfaces_owner[slot] = C->client_pid;
#ifdef DEBUG_WAYLAND
  log_console(
      "h_wl_webos_shell_get_shell_surface: slot=%u webOS shell surf=%p (wss)",
      slot, wss);
#endif
  C->result = slot;
  (void)D;
}

/* ── h_wl_webos_set_property ────────────────────────────────────────────── */
void h_wl_webos_set_property(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);
  const char *name = (const char *)dp_wl(C->data_offset);
  const char *value = name + strlen(name) + 1;

  if (slot < PROXY_SURF_MAX && g_webos_shell_surfaces[slot])
  {
    wl_webos_shell_surface_set_property(g_webos_shell_surfaces[slot], name,
                                        value);
    wl_display_flush(proxy_wl_display);
#ifdef DEBUG_WAYLAND
    log_console(
        "h_wl_webos_set_property: slot=%u [%s]=[%s] webOS shell surf=%p", slot,
        name, value, g_webos_shell_surfaces[slot]);
#endif
  }
#ifdef DEBUG_WAYLAND
  else
  {
    log_console("h_wl_webos_set_property: SKIP (slot=%u no webos_ss)", slot);
  }
#endif
  C->result = 0;
  (void)D;
}

/* ── h_wl_surface_commit ────────────────────────────────────────────────── */
void h_wl_surface_commit(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);
  if (slot < PROXY_SURF_MAX && g_surfs[slot])
  {
    wl_surface_commit(g_surfs[slot]);
    wl_display_flush(proxy_wl_display);
#ifdef DEBUG_WAYLAND
    log_console("h_wl_surface_commit: slot=%u shell surf=%p owner=%d", slot,
                g_surfs[slot], g_surfs_owner[slot]);
#endif
  }
  C->result = 0;
  (void)D;
}

/* ── h_wl_surface_destroy ───────────────────────────────────────────────── */
void h_wl_surface_destroy(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);
  if (slot < PROXY_SURF_MAX && g_surfs[slot])
  {
    wl_surface_destroy(g_surfs[slot]);
    g_surfs[slot] = NULL;
  }
  C->result = 0;
  (void)D;
}

/* ── h_wl_proxy_destroy ─────────────────────────────────────────────────── */
void h_wl_proxy_destroy(BridgeCtrl *C, uint8_t *D)
{
  /* generic destroy: slot is sent, try all tables */
  AR(r);
  uint32_t slot = ar_u32(&r);

  if (slot < PROXY_SURF_MAX)
  {
    if (g_webos_shell_surfaces[slot])
    {
      wl_proxy_destroy((struct wl_proxy *)g_webos_shell_surfaces[slot]);
      g_webos_shell_surfaces[slot] = NULL;
    }
    if (g_shell_surfs[slot])
    {
      wl_proxy_destroy((struct wl_proxy *)g_shell_surfs[slot]);
      g_shell_surfs[slot] = NULL;
    }
  }
  C->result = 0;
  (void)D;
}

/* ── h_wl_webos_set_state ───────────────────────────────────── */
void h_wl_webos_shell_surface_set_state(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  uint32_t slot = ar_u32(&r);
  uint32_t state = ar_u32(&r);
  log_console("wl_webos_shell_surface_set_state slot=%d surf=%p state=%d", slot,
              g_webos_shell_surfaces[slot], state);

  if (g_webos_shell_surfaces[slot])
    wl_webos_shell_surface_set_state(g_webos_shell_surfaces[slot], state);
  C->result = 0;
  (void)D;
}

/* ── h_wl_roundtrip ─────────────────────────────────────────────────────── */
/*
 * result_buf layout (all uint32_t words):
 *
 *   [0]                    = keyboard event count  (N)
 *   [1 .. 1+N*2-1]         = key/state pairs
 *
 * After the keyboard block (at WEBOS_STATE_BASE = 1 + INPUT_EVT_MAX*2):
 *   [BASE+0]               = number of pending webos surface state events (M)
 *   [BASE+1 .. BASE+1+M*2] = slot/state pairs (one per surface)
 *
 * The stub's _deliver_input_events reads WEBOS_STATE_BASE the same way.
 */
#define PROXY_WEBOS_STATE_BASE (1 + INPUT_EVT_MAX * 2)

void h_wl_roundtrip(BridgeCtrl *C, uint8_t *D)
{
  if (proxy_wl_display)
    wl_display_roundtrip(proxy_wl_display);

  uint32_t *rb = (uint32_t *)C->result_buf;

  /* ── keyboard events ── */
  rb[0] = g_input_evt_count;

#ifdef DEBUG_WAYLAND_VERBOSE
  log_console("h_wl_roundtrip: g_input_evt_count=%u", g_input_evt_count);
#endif

  for (uint32_t i = 0; i < g_input_evt_count; i++)
  {
    rb[1 + i * 2] = g_input_evts[i].key;
    rb[2 + i * 2] = g_input_evts[i].state;
  }
  g_input_evt_count = 0;

  /* ── webOS shell surface state events ── */
  uint32_t wss_count = 0;
  uint32_t *wss_out = rb + PROXY_WEBOS_STATE_BASE + 1; /* +1 for count word */

  for (int i = 0; i < PROXY_SURF_MAX; i++)
  {
    if (g_wss_state[i].pending && g_webos_shell_surfaces[i])
    {
      wss_out[wss_count * 2 + 0] = (uint32_t)i;               /* proxy slot */
      wss_out[wss_count * 2 + 1] = g_wss_state[i].last_state; /* state value */
      g_wss_state[i].pending = 0;
      wss_count++;
#ifdef DEBUG_WAYLAND
      log_console("h_wl_roundtrip: relaying wss state slot=%d state=%u", i,
                  g_wss_state[i].last_state);
#endif
    }
  }
  rb[PROXY_WEBOS_STATE_BASE] = wss_count;

  C->result = 0;
  (void)D;
}

/* ── h_wl_flush ─────────────────────────────────────────────────────────── */
void h_wl_flush(BridgeCtrl *C, uint8_t *D)
{
  if (proxy_wl_display)
    wl_display_flush(proxy_wl_display);
  C->result = 0;
  (void)D;
}
