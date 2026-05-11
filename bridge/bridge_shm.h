#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

  typedef struct BridgeShm
  {
    void *ptr;
    int shmid; // SysV
  } BridgeShm;

  /* create new segment (owner side) */
  BridgeShm shm_create(const char *name_or_key, size_t size);

  /* attach to existing segment (client side) */
  BridgeShm shm_attach(const char *name_or_key, size_t size);

  /* detach local mapping */
  void shm_detach(BridgeShm *s);

  /* destroy globally (owner cleanup) */
  void shm_destroy(const char *key, BridgeShm *s);

#ifdef __cplusplus
}
#endif
