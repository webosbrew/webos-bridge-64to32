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

#define BRIDGE_RING_SLOTS_DEFAULT 256u

uint32_t bridge_config_ring_slots(void);
uint32_t bridge_config_ring_mask(void); /* == bridge_config_ring_slots() - 1 */

#ifdef __cplusplus
}
#endif
