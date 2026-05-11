#pragma once

#include <EGL/egl.h>
#include <stdbool.h>

#include "gles_bridge_protocol.h"

/* ══════════════════════════════════════════════════════════════════════════
 * EGL handle tables
 *
 * The 64-bit stub sends 1-based uint32 indices; we map them to real
 * 32-bit EGL pointers here.  Index 0 == EGL_NO_* for all table types.
 * ══════════════════════════════════════════════════════════════════════════ */
typedef struct
{
  void *handle;    /* real EGL pointer */
  pid_t owner_pid; /* client that owns this object */

  uint8_t initialized;
  EGLint major;
  EGLint minor;
} EGLHandleEntry;

extern EGLHandleEntry egl_displays[EGL_BRIDGE_MAX_DISPLAYS + 1];
extern EGLHandleEntry egl_configs[EGL_BRIDGE_MAX_CONFIGS + 1];
extern EGLHandleEntry egl_contexts[EGL_BRIDGE_MAX_CONTEXTS + 1];
extern EGLHandleEntry egl_surfaces[EGL_BRIDGE_MAX_SURFACES + 1];

extern uint32_t g_current_ctx;

void egl_tables_init(void);
void dump_ctx(const char *where);

/* ── EGL handlers ────────────────────────────────────────────────────────── */
void h_eglGetPlatformDisplayEXT(BridgeCtrl *C, uint8_t *D);
void h_eglGetDisplay(BridgeCtrl *C, uint8_t *D);
void h_eglInitialize(BridgeCtrl *C, uint8_t *D);
void h_eglTerminate(BridgeCtrl *C, uint8_t *D);
void h_eglReleaseThread(BridgeCtrl *C, uint8_t *D);
void h_eglQueryString(BridgeCtrl *C, uint8_t *D);
void h_eglGetConfigs(BridgeCtrl *C, uint8_t *D);
void h_eglChooseConfig(BridgeCtrl *C, uint8_t *D);
void h_eglGetConfigAttrib(BridgeCtrl *C, uint8_t *D);
void h_eglCreateWindowSurface(BridgeCtrl *C, uint8_t *D);
void h_eglCreatePbufferSurface(BridgeCtrl *C, uint8_t *D);
void h_eglCreatePixmapSurface(BridgeCtrl *C, uint8_t *D);
void h_eglDestroySurface(BridgeCtrl *C, uint8_t *D);
void h_eglQuerySurface(BridgeCtrl *C, uint8_t *D);
void h_eglSurfaceAttrib(BridgeCtrl *C, uint8_t *D);
void h_eglBindTexImage(BridgeCtrl *C, uint8_t *D);
void h_eglReleaseTexImage(BridgeCtrl *C, uint8_t *D);
void h_eglCopyBuffers(BridgeCtrl *C, uint8_t *D);
void h_eglCreateContext(BridgeCtrl *C, uint8_t *D);
void h_eglDestroyContext(BridgeCtrl *C, uint8_t *D);
void h_eglMakeCurrent(BridgeCtrl *C, uint8_t *D);
void h_eglGetCurrentContext(BridgeCtrl *C, uint8_t *D);
void h_eglGetCurrentSurface(BridgeCtrl *C, uint8_t *D);
void h_eglGetCurrentDisplay(BridgeCtrl *C, uint8_t *D);
void h_eglQueryContext(BridgeCtrl *C, uint8_t *D);
void h_eglSwapBuffers(BridgeCtrl *C, uint8_t *D);
void h_eglSwapInterval(BridgeCtrl *C, uint8_t *D);
void h_eglWaitGL(BridgeCtrl *C, uint8_t *D);
void h_eglWaitNative(BridgeCtrl *C, uint8_t *D);
void h_eglWaitClient(BridgeCtrl *C, uint8_t *D);
void h_eglBindAPI(BridgeCtrl *C, uint8_t *D);
void h_eglQueryAPI(BridgeCtrl *C, uint8_t *D);
void h_eglGetProcAddress(BridgeCtrl *C, uint8_t *D);
void h_eglCreateImageKHR(BridgeCtrl *C, uint8_t *D);
void h_eglDestroyImageKHR(BridgeCtrl *C, uint8_t *D);

/* ── EGL extension handler ──────────────────────────────────────────────── */
void h_InvokeDynamic(BridgeCtrl *C, uint8_t *D);
void h_InvokeDynamicEGL(BridgeCtrl *C, uint8_t *D);
