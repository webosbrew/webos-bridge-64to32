#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  /*
   * Settings are loaded from BRIDGE_CONFIG_PATH; each falls back to its
   * *_DEFAULT if the file or key is missing
   */

#define BRIDGE_CONFIG_PATH                                                     \
  "/media/developer/apps/usr/palm/applications/org.webosbrew.bridge-64to32/"   \
  "bridge.conf"
#define BRIDGE_RING_SLOTS_DEFAULT 4096u
#define BRIDGE_RING_SLOTS_MIN 64u
#define BRIDGE_RING_SLOTS_MAX 65536u
#define BRIDGE_ASYNC_SWAP_DEFAULT 1u
#define BRIDGE_SPIN_US_DEFAULT 50u
#define BRIDGE_FAST_CLIENT_WAIT_DEFAULT 1u

  uint32_t bridge_config_ring_slots(void);
  uint32_t
  bridge_config_ring_mask(void); /* == bridge_config_ring_slots() - 1 */
  int bridge_config_async_swap(void);
  uint32_t bridge_config_spin_us(void);
  int bridge_config_fast_client_wait(void);

#ifdef __cplusplus
}
#endif
