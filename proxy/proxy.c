/*
 * proxy.c  —  armv7a GL proxy process
 *
 * Links against the real 32-bit libGLESv2.so.
 *
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/ipc.h>
#include <sys/prctl.h>
#include <sys/sem.h>
#include <sys/shm.h>

#ifdef DEBUG_VERBOSE
#include <GLES3/gl32.h>
#endif

#include "../bridge/bridge_shm.h"
#include "gles_bridge_protocol.h"

#include "proxy.h"

static int req_efd = -1;
static int resp_efd = -1;

static pthread_t wl_thread;
static volatile int wl_running = 1;

static int req_wl_efd = -1;
static int resp_wl_efd = -1;
static WLBridge g_wl;

#ifdef DEBUG_BRIDGE_VERBOSE
uint32_t last_opc = -1;
#endif

/* ── Shared memory ───────────────────────────────────────────────────────── */
static BridgeRing *ring = NULL;
uint8_t *data = NULL;

static BridgeShm ctrl_shm;
static BridgeShm data_shm;

/* Wayland shared memory */
static BridgeCtrl *wl_ctrl = NULL;
uint8_t *wl_data = NULL;

static BridgeShm wl_ctrl_shm;
static BridgeShm wl_data_shm;

/* Performance monitoring */
#ifdef DEBUG_OPCODES
#define STATS_WINDOW_SWAPS 10
static uint64_t g_opcode_count[OP_MAX];
static uint64_t g_opcode_call_count[OP_MAX];
static uint64_t g_opcode_count_prev[OP_MAX];
static uint64_t g_opcode_call_count_prev[OP_MAX];
static uint64_t g_frame_count = 0;
#endif

/* Sync */
void h_Sync(BridgeCtrl *C, uint8_t *D)
{
  AR(r);
  (void)D;
  C->result = 0; /* unused */
}

#ifdef DEBUG_OPCODES
static void dump_opcode_stats(void)
{
  log_always("=== opcode stats after %llu swaps ===",
             (unsigned long long)g_frame_count);
  for (int i = 1; i < OP_MAX; i++)
  {
    if (g_opcode_count[i] == 0)
      continue;
    log_console("  %-32s total=%-8llu calls(blocking)=%llu",
                opcode_to_string(i), (unsigned long long)g_opcode_count[i],
                (unsigned long long)g_opcode_call_count[i]);
  }
}

static void dump_opcode_stats_window(void)
{
  log_always("=== opcode delta, swaps %llu-%llu ===",
             (unsigned long long)(g_frame_count - STATS_WINDOW_SWAPS + 1),
             (unsigned long long)g_frame_count);

  for (int i = 1; i < OP_MAX; i++)
  {
    uint64_t d_total = g_opcode_count[i] - g_opcode_count_prev[i];
    uint64_t d_calls = g_opcode_call_count[i] - g_opcode_call_count_prev[i];

    if (d_total)
      log_always("  %-32s total=%-8llu calls(blocking)=%llu",
                 opcode_to_string(i), (unsigned long long)d_total,
                 (unsigned long long)d_calls);

    g_opcode_count_prev[i] = g_opcode_count[i];
    g_opcode_call_count_prev[i] = g_opcode_call_count[i];
  }
}
#endif

/* ══════════════════════════════════════════════════════════════════════════
 * Dispatch table — indexed by GLBridgeOpcode
 * ══════════════════════════════════════════════════════════════════════════ */
typedef void (*HandlerFn)(BridgeCtrl *, uint8_t *);

#define ENTRY(op, fn) [op] = fn
static const HandlerFn dispatch_table[OP_MAX] = {
    ENTRY(OP_glActiveTexture, h_glActiveTexture),
    ENTRY(OP_glBindTexture, h_glBindTexture),
    ENTRY(OP_glGenTextures, h_glGenTextures),
    ENTRY(OP_glDeleteTextures, h_glDeleteTextures),
    ENTRY(OP_glTexImage2D, h_glTexImage2D),
    ENTRY(OP_glTexSubImage2D, h_glTexSubImage2D),
    ENTRY(OP_glCompressedTexImage2D, h_glCompressedTexImage2D),
    ENTRY(OP_glCompressedTexSubImage2D, h_glCompressedTexSubImage2D),
    ENTRY(OP_glCopyTexImage2D, h_glCopyTexImage2D),
    ENTRY(OP_glCopyTexSubImage2D, h_glCopyTexSubImage2D),
    ENTRY(OP_glTexParameterf, h_glTexParameterf),
    ENTRY(OP_glTexParameterfv, h_glTexParameterfv),
    ENTRY(OP_glTexParameteri, h_glTexParameteri),
    ENTRY(OP_glTexParameteriv, h_glTexParameteriv),
    ENTRY(OP_glGetTexParameterfv, h_glGetTexParameterfv),
    ENTRY(OP_glGetTexParameteriv, h_glGetTexParameteriv),
    ENTRY(OP_glGenerateMipmap, h_glGenerateMipmap),
    ENTRY(OP_glPixelStorei, h_glPixelStorei),
    ENTRY(OP_glGenBuffers, h_glGenBuffers),
    ENTRY(OP_glDeleteBuffers, h_glDeleteBuffers),
    ENTRY(OP_glBindBuffer, h_glBindBuffer),
    ENTRY(OP_glBufferData, h_glBufferData),
    ENTRY(OP_glBufferSubData, h_glBufferSubData),
    ENTRY(OP_glGetBufferParameteriv, h_glGetBufferParameteriv),
    ENTRY(OP_glGenFramebuffers, h_glGenFramebuffers),
    ENTRY(OP_glDeleteFramebuffers, h_glDeleteFramebuffers),
    ENTRY(OP_glBindFramebuffer, h_glBindFramebuffer),
    ENTRY(OP_glFramebufferTexture2D, h_glFramebufferTexture2D),
    ENTRY(OP_glFramebufferRenderbuffer, h_glFramebufferRenderbuffer),
    ENTRY(OP_glCheckFramebufferStatus, h_glCheckFramebufferStatus),
    ENTRY(OP_glGetFramebufferAttachmentParameteriv,
          h_glGetFramebufferAttachmentParameteriv),
    ENTRY(OP_glGenRenderbuffers, h_glGenRenderbuffers),
    ENTRY(OP_glDeleteRenderbuffers, h_glDeleteRenderbuffers),
    ENTRY(OP_glBindRenderbuffer, h_glBindRenderbuffer),
    ENTRY(OP_glRenderbufferStorage, h_glRenderbufferStorage),
    ENTRY(OP_glGetRenderbufferParameteriv, h_glGetRenderbufferParameteriv),
    ENTRY(OP_glCreateShader, h_glCreateShader),
    ENTRY(OP_glDeleteShader, h_glDeleteShader),
    ENTRY(OP_glShaderSource, h_glShaderSource),
    ENTRY(OP_glCompileShader, h_glCompileShader),
    ENTRY(OP_glGetShaderiv, h_glGetShaderiv),
    ENTRY(OP_glGetShaderInfoLog, h_glGetShaderInfoLog),
    ENTRY(OP_glGetShaderSource, h_glGetShaderSource),
    ENTRY(OP_glShaderBinary, h_glShaderBinary),
    ENTRY(OP_glReleaseShaderCompiler, h_glReleaseShaderCompiler),
    ENTRY(OP_glGetShaderPrecisionFormat, h_glGetShaderPrecisionFormat),
    ENTRY(OP_glCreateProgram, h_glCreateProgram),
    ENTRY(OP_glDeleteProgram, h_glDeleteProgram),
    ENTRY(OP_glAttachShader, h_glAttachShader),
    ENTRY(OP_glDetachShader, h_glDetachShader),
    ENTRY(OP_glLinkProgram, h_glLinkProgram),
    ENTRY(OP_glUseProgram, h_glUseProgram),
    ENTRY(OP_glValidateProgram, h_glValidateProgram),
    ENTRY(OP_glGetProgramiv, h_glGetProgramiv),
    ENTRY(OP_glGetProgramInfoLog, h_glGetProgramInfoLog),
    ENTRY(OP_glGetAttachedShaders, h_glGetAttachedShaders),
    ENTRY(OP_glGetUniformLocation, h_glGetUniformLocation),
    ENTRY(OP_glGetActiveUniform, h_glGetActiveUniform),
    ENTRY(OP_glGetUniformfv, h_glGetUniformfv),
    ENTRY(OP_glGetUniformiv, h_glGetUniformiv),
    ENTRY(OP_glUniform1f, h_glUniform1f),
    ENTRY(OP_glUniform1fv, h_glUniform1fv),
    ENTRY(OP_glUniform1i, h_glUniform1i),
    ENTRY(OP_glUniform1iv, h_glUniform1iv),
    ENTRY(OP_glUniform2f, h_glUniform2f),
    ENTRY(OP_glUniform2fv, h_glUniform2fv),
    ENTRY(OP_glUniform2i, h_glUniform2i),
    ENTRY(OP_glUniform2iv, h_glUniform2iv),
    ENTRY(OP_glUniform3f, h_glUniform3f),
    ENTRY(OP_glUniform3fv, h_glUniform3fv),
    ENTRY(OP_glUniform3i, h_glUniform3i),
    ENTRY(OP_glUniform3iv, h_glUniform3iv),
    ENTRY(OP_glUniform4f, h_glUniform4f),
    ENTRY(OP_glUniform4fv, h_glUniform4fv),
    ENTRY(OP_glUniform4i, h_glUniform4i),
    ENTRY(OP_glUniform4iv, h_glUniform4iv),
    ENTRY(OP_glUniformMatrix2fv, h_glUniformMatrix2fv),
    ENTRY(OP_glUniformMatrix3fv, h_glUniformMatrix3fv),
    ENTRY(OP_glUniformMatrix4fv, h_glUniformMatrix4fv),
    ENTRY(OP_glGetAttribLocation, h_glGetAttribLocation),
    ENTRY(OP_glGetActiveAttrib, h_glGetActiveAttrib),
    ENTRY(OP_glBindAttribLocation, h_glBindAttribLocation),
    ENTRY(OP_glVertexAttribPointer, h_glVertexAttribPointer),
    ENTRY(OP_glEnableVertexAttribArray, h_glEnableVertexAttribArray),
    ENTRY(OP_glDisableVertexAttribArray, h_glDisableVertexAttribArray),
    ENTRY(OP_glGetVertexAttribfv, h_glGetVertexAttribfv),
    ENTRY(OP_glGetVertexAttribiv, h_glGetVertexAttribiv),
    ENTRY(OP_glGetVertexAttribPointerv, h_glGetVertexAttribPointerv),
    ENTRY(OP_glVertexAttrib1f, h_glVertexAttrib1f),
    ENTRY(OP_glVertexAttrib1fv, h_glVertexAttrib1fv),
    ENTRY(OP_glVertexAttrib2f, h_glVertexAttrib2f),
    ENTRY(OP_glVertexAttrib2fv, h_glVertexAttrib2fv),
    ENTRY(OP_glVertexAttrib3f, h_glVertexAttrib3f),
    ENTRY(OP_glVertexAttrib3fv, h_glVertexAttrib3fv),
    ENTRY(OP_glVertexAttrib4f, h_glVertexAttrib4f),
    ENTRY(OP_glVertexAttrib4fv, h_glVertexAttrib4fv),
    ENTRY(OP_glDrawArrays, h_glDrawArrays),
    ENTRY(OP_glDrawElements, h_glDrawElements),
    ENTRY(OP_glViewport, h_glViewport), ENTRY(OP_glScissor, h_glScissor),
    ENTRY(OP_glEnable, h_glEnable), ENTRY(OP_glDisable, h_glDisable),
    ENTRY(OP_glIsEnabled, h_glIsEnabled), ENTRY(OP_glCullFace, h_glCullFace),
    ENTRY(OP_glFrontFace, h_glFrontFace), ENTRY(OP_glLineWidth, h_glLineWidth),
    ENTRY(OP_glPolygonOffset, h_glPolygonOffset),
    ENTRY(OP_glSampleCoverage, h_glSampleCoverage), ENTRY(OP_glHint, h_glHint),
    ENTRY(OP_glBlendColor, h_glBlendColor),
    ENTRY(OP_glBlendEquation, h_glBlendEquation),
    ENTRY(OP_glBlendEquationSeparate, h_glBlendEquationSeparate),
    ENTRY(OP_glBlendFunc, h_glBlendFunc),
    ENTRY(OP_glBlendFuncSeparate, h_glBlendFuncSeparate),
    ENTRY(OP_glDepthFunc, h_glDepthFunc), ENTRY(OP_glDepthMask, h_glDepthMask),
    ENTRY(OP_glDepthRangef, h_glDepthRangef),
    ENTRY(OP_glColorMask, h_glColorMask),
    ENTRY(OP_glStencilFunc, h_glStencilFunc),
    ENTRY(OP_glStencilFuncSeparate, h_glStencilFuncSeparate),
    ENTRY(OP_glStencilMask, h_glStencilMask),
    ENTRY(OP_glStencilMaskSeparate, h_glStencilMaskSeparate),
    ENTRY(OP_glStencilOp, h_glStencilOp),
    ENTRY(OP_glStencilOpSeparate, h_glStencilOpSeparate),
    ENTRY(OP_glClear, h_glClear), ENTRY(OP_glClearColor, h_glClearColor),
    ENTRY(OP_glClearDepthf, h_glClearDepthf),
    ENTRY(OP_glClearStencil, h_glClearStencil),
    ENTRY(OP_glGetError, h_glGetError),
    ENTRY(OP_glGetBooleanv, h_glGetBooleanv),
    ENTRY(OP_glGetFloatv, h_glGetFloatv),
    ENTRY(OP_glGetIntegerv, h_glGetIntegerv),
    ENTRY(OP_glGetString, h_glGetString),
    ENTRY(OP_glReadPixels, h_glReadPixels), ENTRY(OP_glFinish, h_glFinish),
    ENTRY(OP_glFlush, h_glFlush), ENTRY(OP_glIsBuffer, h_glIsBuffer),
    ENTRY(OP_glIsFramebuffer, h_glIsFramebuffer),
    ENTRY(OP_glIsProgram, h_glIsProgram),
    ENTRY(OP_glIsRenderbuffer, h_glIsRenderbuffer),
    ENTRY(OP_glIsShader, h_glIsShader), ENTRY(OP_glIsTexture, h_glIsTexture),

    /* ── EGL ── */
    ENTRY(OP_eglGetError, h_eglGetError),
    ENTRY(OP_eglGetDisplay, h_eglGetDisplay),
    ENTRY(OP_eglInitialize, h_eglInitialize),
    ENTRY(OP_eglTerminate, h_eglTerminate),
    ENTRY(OP_eglQueryString, h_eglQueryString),
    ENTRY(OP_eglGetConfigs, h_eglGetConfigs),
    ENTRY(OP_eglChooseConfig, h_eglChooseConfig),
    ENTRY(OP_eglGetConfigAttrib, h_eglGetConfigAttrib),
    ENTRY(OP_eglCreateWindowSurface, h_eglCreateWindowSurface),
    ENTRY(OP_eglCreatePbufferSurface, h_eglCreatePbufferSurface),
    ENTRY(OP_eglCreatePixmapSurface, h_eglCreatePixmapSurface),
    ENTRY(OP_eglDestroySurface, h_eglDestroySurface),
    ENTRY(OP_eglQuerySurface, h_eglQuerySurface),
    ENTRY(OP_eglSurfaceAttrib, h_eglSurfaceAttrib),
    ENTRY(OP_eglBindTexImage, h_eglBindTexImage),
    ENTRY(OP_eglReleaseTexImage, h_eglReleaseTexImage),
    ENTRY(OP_eglCreateContext, h_eglCreateContext),
    ENTRY(OP_eglDestroyContext, h_eglDestroyContext),
    ENTRY(OP_eglMakeCurrent, h_eglMakeCurrent),
    ENTRY(OP_eglGetCurrentContext, h_eglGetCurrentContext),
    ENTRY(OP_eglGetCurrentSurface, h_eglGetCurrentSurface),
    ENTRY(OP_eglGetCurrentDisplay, h_eglGetCurrentDisplay),
    ENTRY(OP_eglQueryContext, h_eglQueryContext),
    ENTRY(OP_eglSwapBuffers, h_eglSwapBuffers),
    ENTRY(OP_eglSwapInterval, h_eglSwapInterval),
    ENTRY(OP_eglWaitGL, h_eglWaitGL), ENTRY(OP_eglWaitNative, h_eglWaitNative),
    ENTRY(OP_eglWaitClient, h_eglWaitClient),
    ENTRY(OP_eglBindAPI, h_eglBindAPI), ENTRY(OP_eglQueryAPI, h_eglQueryAPI),
    ENTRY(OP_eglReleaseThread, h_eglReleaseThread),
    ENTRY(OP_eglGetProcAddress, h_eglGetProcAddress),
    ENTRY(OP_eglGetPlatformDisplayEXT, h_eglGetPlatformDisplayEXT),
    ENTRY(OP_eglCopyBuffers, h_eglCopyBuffers),
    ENTRY(OP_eglCreateImageKHR, h_eglCreateImageKHR),
    ENTRY(OP_eglDestroyImageKHR, h_eglDestroyImageKHR),

    /* ================================================================= */
    /* GLES 3.0                                                          */
    /* ================================================================= */

    /* ---- Vertex Arrays ---- */
    ENTRY(OP_glGenVertexArrays, h_glGenVertexArrays),
    ENTRY(OP_glDeleteVertexArrays, h_glDeleteVertexArrays),
    ENTRY(OP_glBindVertexArray, h_glBindVertexArray),
    ENTRY(OP_glIsVertexArray, h_glIsVertexArray),

    /* ---- Integer Attributes ---- */
    ENTRY(OP_glVertexAttribI4i, h_glVertexAttribI4i),
    ENTRY(OP_glVertexAttribI4iv, h_glVertexAttribI4iv),
    ENTRY(OP_glVertexAttribI4ui, h_glVertexAttribI4ui),
    ENTRY(OP_glVertexAttribI4uiv, h_glVertexAttribI4uiv),
    ENTRY(OP_glVertexAttribIPointer, h_glVertexAttribIPointer),
    ENTRY(OP_glGetVertexAttribIiv, h_glGetVertexAttribIiv),
    ENTRY(OP_glGetVertexAttribIuiv, h_glGetVertexAttribIuiv),

    /* ---- Instancing ---- */
    ENTRY(OP_glDrawArraysInstanced, h_glDrawArraysInstanced),
    ENTRY(OP_glDrawElementsInstanced, h_glDrawElementsInstanced),
    ENTRY(OP_glVertexAttribDivisor, h_glVertexAttribDivisor),

    /* ---- Buffer Mapping ---- */
    ENTRY(OP_glMapBufferRange, h_glMapBufferRange),
    ENTRY(OP_glFlushMappedBufferRange, h_glFlushMappedBufferRange),
    ENTRY(OP_glUnmapBuffer, h_glUnmapBuffer),
    ENTRY(OP_glCopyBufferSubData, h_glCopyBufferSubData),
    ENTRY(OP_glGetBufferPointerv, h_glGetBufferPointerv),

    /* ---- Query Objects ---- */
    ENTRY(OP_glGenQueries, h_glGenQueries),
    ENTRY(OP_glDeleteQueries, h_glDeleteQueries),
    ENTRY(OP_glBeginQuery, h_glBeginQuery), ENTRY(OP_glEndQuery, h_glEndQuery),
    ENTRY(OP_glGetQueryiv, h_glGetQueryiv),
    ENTRY(OP_glGetQueryObjectuiv, h_glGetQueryObjectuiv),
    ENTRY(OP_glIsQuery, h_glIsQuery),

    /* ---- Samplers ---- */
    ENTRY(OP_glGenSamplers, h_glGenSamplers),
    ENTRY(OP_glDeleteSamplers, h_glDeleteSamplers),
    ENTRY(OP_glBindSampler, h_glBindSampler),
    ENTRY(OP_glIsSampler, h_glIsSampler),
    ENTRY(OP_glSamplerParameteri, h_glSamplerParameteri),
    ENTRY(OP_glSamplerParameteriv, h_glSamplerParameteriv),
    ENTRY(OP_glSamplerParameterf, h_glSamplerParameterf),
    ENTRY(OP_glSamplerParameterfv, h_glSamplerParameterfv),
    ENTRY(OP_glGetSamplerParameteriv, h_glGetSamplerParameteriv),
    ENTRY(OP_glGetSamplerParameterfv, h_glGetSamplerParameterfv),

    /* ---- Transform Feedback ---- */
    ENTRY(OP_glBeginTransformFeedback, h_glBeginTransformFeedback),
    ENTRY(OP_glEndTransformFeedback, h_glEndTransformFeedback),
    ENTRY(OP_glTransformFeedbackVaryings, h_glTransformFeedbackVaryings),
    ENTRY(OP_glGetTransformFeedbackVarying, h_glGetTransformFeedbackVarying),
    ENTRY(OP_glBindBufferBase, h_glBindBufferBase),
    ENTRY(OP_glBindBufferRange, h_glBindBufferRange),
    ENTRY(OP_glGenTransformFeedbacks, h_glGenTransformFeedbacks),
    ENTRY(OP_glDeleteTransformFeedbacks, h_glDeleteTransformFeedbacks),
    ENTRY(OP_glBindTransformFeedback, h_glBindTransformFeedback),
    ENTRY(OP_glPauseTransformFeedback, h_glPauseTransformFeedback),
    ENTRY(OP_glResumeTransformFeedback, h_glResumeTransformFeedback),
    ENTRY(OP_glIsTransformFeedback, h_glIsTransformFeedback),

    /* ---- Uniform Integer ---- */
    ENTRY(OP_glGetUniformuiv, h_glGetUniformuiv),
    ENTRY(OP_glUniform1ui, h_glUniform1ui),
    ENTRY(OP_glUniform1uiv, h_glUniform1uiv),
    ENTRY(OP_glUniform2ui, h_glUniform2ui),
    ENTRY(OP_glUniform2uiv, h_glUniform2uiv),
    ENTRY(OP_glUniform3ui, h_glUniform3ui),
    ENTRY(OP_glUniform3uiv, h_glUniform3uiv),
    ENTRY(OP_glUniform4ui, h_glUniform4ui),
    ENTRY(OP_glUniform4uiv, h_glUniform4uiv),

    /* ---- Uniform Blocks ---- */
    ENTRY(OP_glGetUniformIndices, h_glGetUniformIndices),
    ENTRY(OP_glGetActiveUniformsiv, h_glGetActiveUniformsiv),
    ENTRY(OP_glGetUniformBlockIndex, h_glGetUniformBlockIndex),
    ENTRY(OP_glGetActiveUniformBlockiv, h_glGetActiveUniformBlockiv),
    ENTRY(OP_glGetActiveUniformBlockName, h_glGetActiveUniformBlockName),
    ENTRY(OP_glUniformBlockBinding, h_glUniformBlockBinding),

    /* ---- 3D Textures ---- */
    ENTRY(OP_glTexImage3D, h_glTexImage3D),
    ENTRY(OP_glTexSubImage3D, h_glTexSubImage3D),
    ENTRY(OP_glCopyTexSubImage3D, h_glCopyTexSubImage3D),
    ENTRY(OP_glCompressedTexImage3D, h_glCompressedTexImage3D),
    ENTRY(OP_glCompressedTexSubImage3D, h_glCompressedTexSubImage3D),
    ENTRY(OP_glTexStorage2D, h_glTexStorage2D),
    ENTRY(OP_glTexStorage3D, h_glTexStorage3D),

    /* ---- Indexed State ---- */
    ENTRY(OP_glColorMaski, h_glColorMaski), ENTRY(OP_glEnablei, h_glEnablei),
    ENTRY(OP_glDisablei, h_glDisablei), ENTRY(OP_glIsEnabledi, h_glIsEnabledi),
    ENTRY(OP_glGetBooleani_v, h_glGetBooleani_v),
    ENTRY(OP_glGetTexParameterIiv, h_glGetTexParameterIiv),
    ENTRY(OP_glGetTexParameterIuiv, h_glGetTexParameterIuiv),
    ENTRY(OP_glTexParameterIiv, h_glTexParameterIiv),
    ENTRY(OP_glTexParameterIuiv, h_glTexParameterIuiv),

    /* ---- Sync ---- */
    ENTRY(OP_glFenceSync, h_glFenceSync),
    ENTRY(OP_glClientWaitSync, h_glClientWaitSync),
    ENTRY(OP_glWaitSync, h_glWaitSync), ENTRY(OP_glDeleteSync, h_glDeleteSync),
    ENTRY(OP_glGetSynciv, h_glGetSynciv), ENTRY(OP_glIsSync, h_glIsSync),

    /* ---- Misc ---- */
    ENTRY(OP_glGetStringi, h_glGetStringi),
    ENTRY(OP_glGetInteger64v, h_glGetInteger64v),
    ENTRY(OP_glGetFragDataLocation, h_glGetFragDataLocation),

    /* ---- Multisample/FBO ---- */
    ENTRY(OP_glRenderbufferStorageMultisample,
          h_glRenderbufferStorageMultisample),
    ENTRY(OP_glBlitFramebuffer, h_glBlitFramebuffer),
    ENTRY(OP_glFramebufferTextureLayer, h_glFramebufferTextureLayer),

    /* ================================================================= */
    /* GLES 3.2                                                          */
    /* ================================================================= */

    /* ---- Debug ---- */
    ENTRY(OP_glDebugMessageCallback, h_glDebugMessageCallback),
    ENTRY(OP_glDebugMessageControl, h_glDebugMessageControl),

    ENTRY(OP_glBindImageTexture, h_glBindImageTexture),
    ENTRY(OP_glDrawRangeElements, h_glDrawRangeElements),
    ENTRY(OP_glSamplerParameterIiv, h_glSamplerParameterIiv),
    ENTRY(OP_glGetPointerv, h_glGetPointerv),
    ENTRY(OP_glPopDebugGroup, h_glPopDebugGroup),
    ENTRY(OP_glGetProgramBinary, h_glGetProgramBinary),

    ENTRY(OP_glClearBufferfi, h_glClearBufferfi),
    ENTRY(OP_glClearBufferfv, h_glClearBufferfv),
    ENTRY(OP_glClearBufferiv, h_glClearBufferiv),
    ENTRY(OP_glClearBufferuiv, h_glClearBufferuiv),
    ENTRY(OP_glCopyImageSubData, h_glCopyImageSubData),
    ENTRY(OP_glDebugMessageInsert, h_glDebugMessageInsert),
    ENTRY(OP_glDispatchCompute, h_glDispatchCompute),
    ENTRY(OP_glDispatchComputeIndirect, h_glDispatchComputeIndirect),
    ENTRY(OP_glDrawBuffers, h_glDrawBuffers),
    ENTRY(OP_glDrawElementsBaseVertex, h_glDrawElementsBaseVertex),
    ENTRY(OP_glDrawElementsInstancedBaseVertex,
          h_glDrawElementsInstancedBaseVertex),
    ENTRY(OP_glDrawRangeElementsBaseVertex, h_glDrawRangeElementsBaseVertex),
    ENTRY(OP_glGetBufferParameteri64v, h_glGetBufferParameteri64v),
    ENTRY(OP_glGetDebugMessageLog, h_glGetDebugMessageLog),
    ENTRY(OP_glGetInternalformativ, h_glGetInternalformativ),
    ENTRY(OP_glGetInteger64i_v, h_glGetInteger64i_v),
    ENTRY(OP_glGetIntegeri_v, h_glGetIntegeri_v),
    ENTRY(OP_glGetObjectLabel, h_glGetObjectLabel),
    ENTRY(OP_glGetObjectPtrLabel, h_glGetObjectPtrLabel),
    ENTRY(OP_glGetSamplerParameterIiv, h_glGetSamplerParameterIiv),
    ENTRY(OP_glGetSamplerParameterIuiv, h_glGetSamplerParameterIuiv),
    ENTRY(OP_glMemoryBarrier, h_glMemoryBarrier),
    ENTRY(OP_glMinSampleShading, h_glMinSampleShading),
    ENTRY(OP_glObjectLabel, h_glObjectLabel),
    ENTRY(OP_glObjectPtrLabel, h_glObjectPtrLabel),
    ENTRY(OP_glProgramBinary, h_glProgramBinary),
    ENTRY(OP_glProgramParameteri, h_glProgramParameteri),
    ENTRY(OP_glPushDebugGroup, h_glPushDebugGroup),
    ENTRY(OP_glReadBuffer, h_glReadBuffer),
    ENTRY(OP_glSamplerParameterIuiv, h_glSamplerParameterIuiv),
    ENTRY(OP_glTexBuffer, h_glTexBuffer),
    ENTRY(OP_glTexStorage2DMultisample, h_glTexStorage2DMultisample),
    ENTRY(OP_glTexStorage3DMultisample, h_glTexStorage3DMultisample),

    ENTRY(OP_glPrimitiveBoundingBox, h_glPrimitiveBoundingBox),
    ENTRY(OP_glUseProgramStages, h_glUseProgramStages),
    ENTRY(OP_glBindProgramPipeline, h_glBindProgramPipeline),
    ENTRY(OP_glGenProgramPipelines, h_glGenProgramPipelines),
    ENTRY(OP_glDeleteProgramPipelines, h_glDeleteProgramPipelines),
    ENTRY(OP_glIsProgramPipeline, h_glIsProgramPipeline),
    ENTRY(OP_glDrawArraysIndirect, h_glDrawArraysIndirect),
    ENTRY(OP_glDrawElementsIndirect, h_glDrawElementsIndirect),
    ENTRY(OP_glFramebufferTexture, h_glFramebufferTexture),

    ENTRY(OP_glUniformMatrix2x3fv, h_glUniformMatrix2x3fv),
    ENTRY(OP_glUniformMatrix3x2fv, h_glUniformMatrix3x2fv),
    ENTRY(OP_glUniformMatrix2x4fv, h_glUniformMatrix2x4fv),
    ENTRY(OP_glUniformMatrix4x2fv, h_glUniformMatrix4x2fv),
    ENTRY(OP_glUniformMatrix3x4fv, h_glUniformMatrix3x4fv),
    ENTRY(OP_glUniformMatrix4x3fv, h_glUniformMatrix4x3fv),

    ENTRY(OP_glGetProgramInterfaceiv, h_glGetProgramInterfaceiv),
    ENTRY(OP_glGetProgramResourceIndex, h_glGetProgramResourceIndex),
    ENTRY(OP_glGetProgramResourceName, h_glGetProgramResourceName),
    ENTRY(OP_glGetProgramResourceiv, h_glGetProgramResourceiv),
    ENTRY(OP_glGetProgramResourceLocation, h_glGetProgramResourceLocation),

    ENTRY(OP_glActiveShaderProgram, h_glActiveShaderProgram),
    ENTRY(OP_glCreateShaderProgramv, h_glCreateShaderProgramv),
    ENTRY(OP_glValidateProgramPipeline, h_glValidateProgramPipeline),
    ENTRY(OP_glGetProgramPipelineInfoLog, h_glGetProgramPipelineInfoLog),

    ENTRY(OP_glMemoryBarrierByRegion, h_glMemoryBarrierByRegion),
    ENTRY(OP_glBlendBarrier, h_glBlendBarrier),

    ENTRY(OP_glGetGraphicsResetStatus, h_glGetGraphicsResetStatus),
    ENTRY(OP_glReadnPixels, h_glReadnPixels),
    ENTRY(OP_glGetnUniformfv, h_glGetnUniformfv),
    ENTRY(OP_glGetnUniformiv, h_glGetnUniformiv),
    ENTRY(OP_glGetnUniformuiv, h_glGetnUniformuiv),

    ENTRY(OP_glPatchParameteri, h_glPatchParameteri),
    ENTRY(OP_glTexBufferRange, h_glTexBufferRange),

/* Wayland - EGL */
#ifdef HAVE_OWN_WAYLAND_EGL
    ENTRY(OP_wl_egl_window_create, h_wl_egl_window_create),
    ENTRY(OP_wl_egl_window_destroy, h_wl_egl_window_destroy),
    ENTRY(OP_wl_egl_window_resize, h_wl_egl_window_resize),
#endif

    ENTRY(OP_Sync, h_Sync),

    /* EGL - extensions */
    ENTRY(OP_InvokeDynamic, h_InvokeDynamic),
    ENTRY(OP_InvokeDynamicEGL, h_InvokeDynamicEGL)};

static const HandlerFn wl_dispatch_table[OP_WL_MAX] = {
    ENTRY(OP_wl_display_connect, h_wl_display_connect),
    ENTRY(OP_wl_display_disconnect, h_wl_display_disconnect),
    ENTRY(OP_wl_roundtrip, h_wl_roundtrip),
    ENTRY(OP_wl_flush, h_wl_flush),
    ENTRY(OP_wl_compositor_create_surface, h_wl_compositor_create_surface),
    ENTRY(OP_wl_surface_commit, h_wl_surface_commit),
    ENTRY(OP_wl_surface_destroy, h_wl_surface_destroy),
    ENTRY(OP_wl_webos_set_property, h_wl_webos_set_property),
    ENTRY(OP_wl_shell_get_shell_surface, h_wl_shell_get_shell_surface),
    ENTRY(OP_wl_shell_surface_set_toplevel, h_wl_shell_surface_set_toplevel),
    ENTRY(OP_wl_webos_shell_get_shell_surface,
          h_wl_webos_shell_get_shell_surface),
    ENTRY(OP_wl_proxy_destroy, h_wl_proxy_destroy),
    ENTRY(OP_wl_webos_shell_surface_set_state,
          h_wl_webos_shell_surface_set_state),
};

const char *get_executable_path(void)
{
  static char path[4096];
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);

  if (len < 0)
  {
    return "UNKNOWN_EXE";
  }

  path[len] = '\0';
  return path;
}

void close_wl_thread()
{
  wl_running = 0;

  struct sembuf op = {
      .sem_num = SEM_REQ,
      .sem_op = 1,
  };

  pthread_join(wl_thread, NULL);
}

void at_exit_proxy_cleanup()
{
#ifdef DEBUG
  log_console("at_exit_proxy_cleanup");
#endif

  shm_destroy(GLES_BRIDGE_CTRL_SHM, &ctrl_shm);
  shm_destroy(GLES_BRIDGE_DATA_SHM, &data_shm);

  shm_destroy(GLES_BRIDGE_WL_CTRL_SHM, &wl_ctrl_shm);
  shm_destroy(GLES_BRIDGE_WL_DATA_SHM, &wl_data_shm);

  close_wl_thread();
}

int setup_bridge()
{
  log_console("setting up bridge");
  ctrl_shm = shm_attach(GLES_BRIDGE_CTRL_SHM, GLES_BRIDGE_CTRL_SIZE);
  data_shm = shm_attach(GLES_BRIDGE_DATA_SHM, GLES_BRIDGE_DATA_SIZE);

  ring = (BridgeRing *)ctrl_shm.ptr;
  data = (uint8_t *)data_shm.ptr;

#ifdef DEBUG
  log_console("ring=%p g_data=%p ctrl_shm.shmid=%d "
              "data_shm.shmid=%d",
              ring, data, ctrl_shm.shmid, data_shm.shmid);

  log_console("sizeof(BridgeCtrl)=%zu "
              "client_pid offset=%zu "
              "ctrlsize=%u",
              sizeof(BridgeRing), offsetof(BridgeCtrl, client_pid),
              GLES_BRIDGE_CTRL_SIZE);
#endif

  return (!ring || !data);
}

int setup_bridge_wl(void)
{
  wl_ctrl_shm = shm_attach(GLES_BRIDGE_WL_CTRL_SHM, GLES_BRIDGE_CTRL_SIZE);

  wl_data_shm = shm_attach(GLES_BRIDGE_WL_DATA_SHM, GLES_BRIDGE_DATA_SIZE);

  wl_ctrl = (BridgeCtrl *)wl_ctrl_shm.ptr;
  wl_data = (uint8_t *)wl_data_shm.ptr;

#ifdef DEBUG
  log_console("wl_ctrl=%p wl_data=%p wl_ctrl_shm.shmid=%d "
              "wl_data_shm.shmid=%d",
              wl_ctrl, wl_data, wl_ctrl_shm.shmid, wl_data_shm.shmid);
#endif

  return 0;
}

static void *wl_dispatch_thread(void *arg)
{
  (void)arg;

  log_console("wl dispatch thread started");

  while (wl_running)
  {
    uint64_t val;

    if (read(req_wl_efd, &val, sizeof(val)) != sizeof(val))
    {
      break;
    }

    uint32_t opc = wl_ctrl->opcode;

    if (opc > 0 && opc < OP_MAX && wl_dispatch_table[opc])
    {
#ifdef DEBUG_WAYLAND_VERBOSE
      log_console("wl dispatch thread: opc=%u", opc);
#endif
      wl_dispatch_table[opc](wl_ctrl, wl_data);
    }

    if (wl_ctrl->needs_response)
    {
      uint64_t one = 1;

      write(resp_wl_efd, &one, sizeof(one));
    }
  }

  return NULL;
}

int setup_wl_thread(void)
{
  if (pthread_create(&wl_thread, NULL, wl_dispatch_thread, NULL) != 0)
  {
    log_error("[proxy] failed creating wl thread");
    return 1;
  }

  log_console("wl thread started");

  return 0;
}

/* ══════════════════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════════════════ */
int main(int argc, char **argv)
{
  log_console("starting (pid %d) from %s", (int)getpid(),
              get_executable_path());

  req_efd = atoi(argv[1]);
  resp_efd = atoi(argv[2]);
  req_wl_efd = atoi(argv[3]);
  resp_wl_efd = atoi(argv[4]);
  const char *appId = argv[5];

  if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0)
    return 1;

  if (getppid() == 1)
    return 1;

  if (!getenv("XDG_RUNTIME_DIR"))
    setenv("XDG_RUNTIME_DIR", "/tmp/xdg", 0);
  if (!getenv("WAYLAND_DISPLAY"))
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
  if (!getenv("EGL_PLATFORM"))
    setenv("EGL_PLATFORM", "wayland", 0);
  if (getenv("APPID") == NULL && appId && strlen(appId) > 0)
    setenv("APPID", appId, 0);

  log_console("req_efd=%d resp_efd=%d req_wl_efd=%d resp_wl_efd=%u appId=%s",
              req_efd, resp_efd, req_wl_efd, resp_wl_efd, appId);

  if (setup_bridge(argc, argv) == 1)
    return 1;

  atexit(at_exit_proxy_cleanup);

  if (!ring || !data)
  {
    log_error("FATAL: cannot map shm");
    return 1;
  }

  egl_tables_init();

  if (setup_bridge_wl() == 1)
    return 1;

  if (setup_wl_thread() == 1)
    return 1;

  // For testing output:
  // proxy_wayland_init();
  // create_window();

  log_console("shm mapped, entering dispatch loop");

  // opcode dump..
  /*for (int i = 0; i < OP_MAX; i++) {
    log_console("%d -> %s", i, opcode_to_string(i));
  }*/

  memset(&g_proxy_ctx[0], 0, sizeof(GLContextState));

  uint64_t completed = 0;

  for (;;)
  {
    atomic_store_explicit(&ring->consumer_waiting, 1, memory_order_seq_cst);

    uint64_t published =
        atomic_load_explicit(&ring->published_seq, memory_order_acquire);

    if (completed >= published)
    {
      atomic_store_explicit(&ring->consumer_waiting, 1, memory_order_seq_cst);

      published =
          atomic_load_explicit(&ring->published_seq, memory_order_acquire);
      if (completed >= published)
      {
        /* Caught up. Block until the producer publishes */
        uint64_t val;
        if (read(req_efd, &val, sizeof(val)) != sizeof(val))
        {
          log_error("eventfd read failed: %s", strerror(errno));
          break;
        }
      }

      atomic_store_explicit(&ring->consumer_waiting, 0, memory_order_seq_cst);
      continue;
    }

    BridgeCtrl *c = &ring->slots[completed & BRIDGE_RING_MASK];
    uint32_t opc = c->opcode;

#ifdef DEBUG_OPCODES
    log_console("dispatching opcode %u (%s)", opc, opcode_to_string(opc));
#endif

#ifdef DEBUG_BRIDGE_VERBOSE
    EGLint prev_err = glGetError();
    EGLint prev_egl_err = eglGetError();
    if (prev_err != GL_NO_ERROR || prev_egl_err != EGL_SUCCESS)
    {
      log_error(
          "\n\nFATAL: Stale GL/EGL error LAST OP=%s (%u) NEXT OP %s (%u)\n"
          "    GL error:  0x%04x\n    EGL error: 0x%04x\n\n",
          opcode_to_string(last_opc), last_opc, opcode_to_string(opc), opc,
          prev_err, prev_egl_err);
#ifdef DEBUG_ABORT_ON_GL_ERROR
      log_error("aborting due to stale GL/EGL error");
      abort();
#endif
    }
#endif

    if (opc == 0 || opc >= OP_MAX || !dispatch_table[opc])
      log_error("invalid opcode=%u", opc);
    else
      dispatch_table[opc](c, data);

#ifdef DEBUG_OPCODES
    // record performance stats
    g_opcode_count[opc]++;
    if (c->needs_response)
      g_opcode_call_count[opc]++;

    if (opc == OP_eglSwapBuffers)
    {
      g_frame_count++;
      if (g_frame_count % STATS_WINDOW_SWAPS == 0)
        dump_opcode_stats_window();
      if (g_frame_count % 60 == 0)
        dump_opcode_stats();
    }
#endif

#ifdef DEBUG_BRIDGE_VERBOSE
    // check for GL errors for every op code
    EGLint new_err = glGetError();
    EGLint new_egl_err = eglGetError();
    if (new_err != GL_NO_ERROR || new_egl_err != EGL_SUCCESS)
    {
      log_error("AFTER: %s (OP=%u) prev_err=0x%x err=0x%x prev_egl_err=0x%x "
                "new_egl_err=0x%x",
                opcode_to_string(opc), opc, prev_err, new_err, prev_egl_err,
                new_egl_err);
#ifdef DEBUG_ABORT_ON_GL_ERROR
      log_error("aborting due to GL/EGL error after opcode execution");
      abort();
#endif
    }
    last_opc = opc;
#endif

    /* Release this slot's data-arena range back to the producer. */
    if (c->data_watermark_valid)
      atomic_store_explicit(&ring->data_tail, c->data_watermark,
                            memory_order_release);

    int needs_resp = c->needs_response;

    completed++;
    atomic_store_explicit(&ring->completed_seq, completed,
                          memory_order_release);

    if (needs_resp)
    {
      uint64_t one = 1;
      if (write(resp_efd, &one, sizeof(one)) != sizeof(one))
      {
        log_error("eventfd write failed: %s", strerror(errno));
        break;
      }
    }
  }

  return 0;
}
