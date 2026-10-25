#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_config.h"
#include "shared_util.h"

typedef struct
{
  int loaded;
  long ring_slots;
  long async_swap;
  long spin_us;
  long fast_client_wait;
} BridgeConfig;

static BridgeConfig g_cfg;
static uint32_t g_ring_slots = 0; /* 0 == not yet loaded */

static int is_pow2(uint32_t v)
{
  return v != 0 && (v & (v - 1u)) == 0;
}

static uint32_t round_up_pow2(uint32_t v)
{
  if (v < 2u)
    return 1u;
  v--;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return v + 1u;
}

/* Minimal "key=value" parser. Ignores blank lines and lines starting with
 * '#'. Unknown keys are ignored, missing keys keep their defaults. */
static void read_config_file(void)
{
  g_cfg.ring_slots = BRIDGE_RING_SLOTS_DEFAULT;
  g_cfg.async_swap = BRIDGE_ASYNC_SWAP_DEFAULT;
  g_cfg.spin_us = BRIDGE_SPIN_US_DEFAULT;
  g_cfg.fast_client_wait = BRIDGE_FAST_CLIENT_WAIT_DEFAULT;
  g_cfg.loaded = 1;

  FILE *f = fopen(BRIDGE_CONFIG_PATH, "r");
  if (!f)
  {
    log_console("bridge_config: %s not found, creating with defaults",
                BRIDGE_CONFIG_PATH);

    FILE *out = fopen(BRIDGE_CONFIG_PATH, "w");
    if (out)
    {
      fprintf(out,
              "# proxy configuration\n"
              "# ring_slots must be a power of two\n"
              "ring_slots=%u\n"
              "# 1 = eglSwapBuffers returns without waiting for the proxy to\n"
              "# present (at most one frame)\n"
              "async_swap=%u\n"
              "# microseconds to busy-wait before sleeping on an eventfd\n"
              "# (0 = always sleep)\n"
              "spin_us=%u\n"
              "# 1 = glClientWaitSync(GL_TIMEOUT_IGNORED) on persistently\n"
              "# mapped buffers returns immediately; the proxy waits on the\n"
              "# fence just before it next writes into a real mapping.\n"
              "fast_client_wait=%u\n",
              BRIDGE_RING_SLOTS_DEFAULT, BRIDGE_ASYNC_SWAP_DEFAULT,
              BRIDGE_SPIN_US_DEFAULT, BRIDGE_FAST_CLIENT_WAIT_DEFAULT);
      fclose(out);
    }
    else
    {
      log_error("bridge_config: failed to create %s: %s", BRIDGE_CONFIG_PATH,
                strerror(errno));
    }

    return;
  }

  char line[256];

  while (fgets(line, sizeof(line), f))
  {
    char *p = line;

    while (isspace((unsigned char)*p))
      p++;

    if (*p == '#' || *p == '\0' || *p == '\n')
      continue;

    char key[64] = {0};
    long val = 0;

    if (sscanf(p, "%63[^=]=%ld", key, &val) != 2)
      continue;

    size_t klen = strlen(key);
    while (klen > 0 && isspace((unsigned char)key[klen - 1]))
      key[--klen] = '\0';

    char *kp = key;
    while (isspace((unsigned char)*kp))
      kp++;

    if (strcmp(kp, "ring_slots") == 0)
    {
      if (val <= 0)
      {
        log_error("bridge_config: ring_slots=%ld in %s is invalid, using "
                  "default %u",
                  val, BRIDGE_CONFIG_PATH, BRIDGE_RING_SLOTS_DEFAULT);
        val = BRIDGE_RING_SLOTS_DEFAULT;
      }
      g_cfg.ring_slots = val;
    }
    else if (strcmp(kp, "async_swap") == 0)
      g_cfg.async_swap = val != 0;
    else if (strcmp(kp, "spin_us") == 0)
      g_cfg.spin_us = val < 0 ? 0 : val > 10000 ? 10000 : val;
    else if (strcmp(kp, "fast_client_wait") == 0)
      g_cfg.fast_client_wait = val != 0;
  }

  fclose(f);
}

static void ensure_loaded(void)
{
  if (g_cfg.loaded)
    return;

  read_config_file();

  log_always("bridge_config: async_swap=%ld spin_us=%ld fast_client_wait=%ld",
             g_cfg.async_swap, g_cfg.spin_us, g_cfg.fast_client_wait);
}

static uint32_t load_ring_slots(void)
{
  ensure_loaded();

  uint32_t slots = (uint32_t)g_cfg.ring_slots;

  if (slots < BRIDGE_RING_SLOTS_MIN || slots > BRIDGE_RING_SLOTS_MAX)
  {
    log_error("bridge_config: ring_slots=%u outside allowed range [%u,%u], "
              "clamping",
              slots, BRIDGE_RING_SLOTS_MIN, BRIDGE_RING_SLOTS_MAX);
    slots = slots < BRIDGE_RING_SLOTS_MIN ? BRIDGE_RING_SLOTS_MIN
                                          : BRIDGE_RING_SLOTS_MAX;
  }

  if (!is_pow2(slots))
  {
    uint32_t rounded = round_up_pow2(slots);
    log_error("bridge_config: ring_slots=%u is not a power of two "
              "(required for `seq & mask` ring indexing), rounding up to %u",
              slots, rounded);
    slots = rounded;
  }

  log_always("bridge_config: BRIDGE_RING_SLOTS=%u (from %s)", slots,
             BRIDGE_CONFIG_PATH);

  return slots;
}

uint32_t bridge_config_ring_slots(void)
{
  if (!g_ring_slots)
    g_ring_slots = load_ring_slots();

  return g_ring_slots;
}

uint32_t bridge_config_ring_mask(void)
{
  return bridge_config_ring_slots() - 1u;
}

int bridge_config_async_swap(void)
{
  ensure_loaded();
  return (int)g_cfg.async_swap;
}

uint32_t bridge_config_spin_us(void)
{
  ensure_loaded();
  return (uint32_t)g_cfg.spin_us;
}

int bridge_config_fast_client_wait(void)
{
  ensure_loaded();
  return (int)g_cfg.fast_client_wait;
}
