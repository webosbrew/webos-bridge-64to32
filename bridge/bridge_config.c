#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bridge_config.h"
#include "shared_util.h"

#define BRIDGE_CONFIG_PATH "/media/developer/apps/usr/palm/applications/org.webosbrew.bridge-64to32/bridge.conf"
#define BRIDGE_RING_SLOTS_MIN 64u
#define BRIDGE_RING_SLOTS_MAX 65536u

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
 * '#'. Only "ring_slots" is currently recognised. */
static uint32_t read_config_file(void)
{
  FILE *f = fopen(BRIDGE_CONFIG_PATH, "r");
  if (!f)
  {
    log_console("bridge_config: %s not found, creating with default "
                "ring_slots=%u",
                BRIDGE_CONFIG_PATH, BRIDGE_RING_SLOTS_DEFAULT);

    FILE *out = fopen(BRIDGE_CONFIG_PATH, "w");
    if (out)
    {
      fprintf(out, "# proxy configuration\n"
                   "# ring_slots must be a power of two\n"
                   "ring_slots=%u\n",
              BRIDGE_RING_SLOTS_DEFAULT);
      fclose(out);
    }
    else
    {
      log_error("bridge_config: failed to create %s: %s", BRIDGE_CONFIG_PATH,
                strerror(errno));
    }

    return BRIDGE_RING_SLOTS_DEFAULT;
  }

  uint32_t slots = BRIDGE_RING_SLOTS_DEFAULT;
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

    if (strcmp(kp, "ring_slots") != 0)
      continue;

    if (val <= 0)
    {
      log_error("bridge_config: ring_slots=%ld in %s is invalid, using "
                "default %u",
                val, BRIDGE_CONFIG_PATH, BRIDGE_RING_SLOTS_DEFAULT);
      slots = BRIDGE_RING_SLOTS_DEFAULT;
    }
    else
    {
      slots = (uint32_t)val;
    }
    break;
  }

  fclose(f);

  return slots;
}

static uint32_t load_ring_slots(void)
{
  uint32_t slots = read_config_file();

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
