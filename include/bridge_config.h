#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /*
   * BRIDGE_RING_SLOTS is loaded from config
   * Falls back to BRIDGE_RING_SLOTS_DEFAULT if the file is missing
   */

#define BRIDGE_CONFIG_PATH                                                     \
  "/media/developer/apps/usr/palm/applications/org.webosbrew.bridge-64to32/"   \
  "bridge.conf"
#define BRIDGE_RING_SLOTS_DEFAULT 256u
#define BRIDGE_RING_SLOTS_MIN 64u
#define BRIDGE_RING_SLOTS_MAX 65536u

  uint32_t bridge_config_ring_slots(void);
  uint32_t
  bridge_config_ring_mask(void); /* == bridge_config_ring_slots() - 1 */

#ifdef __cplusplus
}
#endif
