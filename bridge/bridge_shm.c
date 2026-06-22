#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include "bridge_shm.h"
#include "shared_util.h"

static void *map_fd(int fd, size_t size)
{
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  return (p == MAP_FAILED) ? NULL : p;
}
/* ───────────────── SYSV ───────────────── */

static BridgeShm sysv_create(key_t key, size_t size)
{
#ifdef DEBUG
  log_console("sysv_create key:%d size:%ld", key, size);
#endif
  BridgeShm s = {0};

  int old = shmget(key, 0, 0600);
  if (old >= 0)
  {
#ifdef DEBUG
    log_console("sysv_create: removing stale shmid=%d", old);
#endif
    if (shmctl(old, IPC_RMID, NULL) < 0)
    {
      log_error("sysv_create: shmctl IPC_RMID failed: %s", strerror(errno));
    }
  }

  s.shmid = shmget(key, size, IPC_CREAT | IPC_EXCL | 0600);
  if (s.shmid < 0)
  {
    log_error("sysv_create: shmget FAILED key=%d size=%zu errno=%d (%s)", key,
              size, errno, strerror(errno));
    return s;
  }

  s.ptr = shmat(s.shmid, NULL, 0);
  if (s.ptr == (void *)-1)
  {
    log_error("sysv_create: shmat FAILED shmid=%d errno=%d (%s)", s.shmid,
              errno, strerror(errno));
    s.ptr = NULL;
    return s;
  }

  memset(s.ptr, 0, size);
  return s;
}

static BridgeShm sysv_attach(key_t key, size_t size)
{
#ifdef DEBUG
  log_console("sysv_attach key:%d size:%ld", key, size);
#endif
  BridgeShm s = {0};

  s.shmid = shmget(key, size, 0600);
  if (s.shmid < 0)
    return s;

  s.ptr = shmat(s.shmid, NULL, 0);
  if (s.ptr == (void *)-1)
  {
    s.ptr = NULL;
  }

  return s;
}

static void sysv_destroy(BridgeShm *s)
{
  if (s->ptr)
  {
    shmdt(s->ptr);
  }

  if (s->shmid >= 0)
  {
    shmctl(s->shmid, IPC_RMID, NULL);
  }
}

/* ───────────────── PUBLIC API ───────────────── */

BridgeShm shm_create(const char *name_or_key, size_t size)
{
  return sysv_create((key_t)strtoul(name_or_key, NULL, 0), size);
}

BridgeShm shm_attach(const char *name_or_key, size_t size)
{
  return sysv_attach((key_t)strtoul(name_or_key, NULL, 0), size);
}

void shm_detach(BridgeShm *s)
{
  if (!s)
    return;

  if (s->ptr && s->shmid >= 0)
  {
    shmdt(s->ptr);
  }

  s->ptr = NULL;
  s->shmid = -1;
}

void shm_destroy(const char *key, BridgeShm *s)
{
  if (!s)
  {
    log_error("shm_destroy: key: %s - s is NULL");
    return;
  }

#ifdef DEBUG
  log_console("shm_destroy: key: %s s->ptr:%p s->shmid:%d", key, s->ptr,
              s->shmid);
#endif

  if (s->ptr != NULL)
    sysv_destroy(s);

  s->ptr = NULL;
  s->shmid = -1;
}
