#pragma once

/*
 * gles_bridge_protocol.h
 *
 * Shared memory layout and opcode definitions for the aarch64↔armv7a
 * GLES bridge.  Both the stub (aarch64) and the proxy (armv7a) include
 * this header
 *
 * Shared memory regions
 * ─────────────────────
 *   /gles_bridge_ctrl   4 KB   BridgeCtrl struct (semaphores + command)
 *   /gles_bridge_data  64 MB   bulk payload (textures, shader source, …)
 *
 * Communication model
 * ───────────────────
 *   Void calls  : stub writes args, posts req_sem, returns immediately.
 *   Query calls : stub writes args, sets needs_response=1, posts req_sem,
 *                 then waits on resp_sem; reads result.
 *   Proxy       : waits on req_sem, dispatches, writes result if
 *                 needs_response, posts resp_sem.
 */

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

/* ── Shared memory names ────────────────────────────────────────────────── */
#define GLES_BRIDGE_CTRL_SHM "0x4701"
#define GLES_BRIDGE_DATA_SHM "0x4702"

/* Packed argument buffer inside BridgeCtrl.  256 bytes covers every
 * GLES function's scalar arguments (the largest is glUniformMatrix4fv
 * with location+count+transpose = 12 bytes; array data goes via data shm). */
#define BRIDGE_ARGS_SIZE 256
#define BRIDGE_RESULT_SIZE 4096 /* for glGetString / info logs */

// alternative to shm: SysV shared memory segment
#define GLES_BRIDGE_CTRL_KEY 0x47534c43 /* 'GSLC' */
#define GLES_BRIDGE_DATA_KEY 0x47534c44 /* 'GSLD' */

/* Service mode only - instead of eventfd */
#define GLES_SEM_KEY 0x47534C32 /* 'GSL2' */

#define SEM_REQ 0
#define SEM_RESP 1
#define SEM_READY 2

#define SEM_COUNT 3

/* ── Shared memory names (WL) ──────────────────────────────────────────── */
#define GLES_WL_SEM_KEY 0x47534C33 /* 'GSL3' */

// Separate shm keys
#define GLES_BRIDGE_WL_CTRL_SHM "0x4703"
#define GLES_BRIDGE_WL_DATA_SHM "0x4704"

/* ── Opcodes ─────────────────────────────────────────────────────────────── */
typedef enum
{
  OP_NONE = 0,
  /* ---- textures ---- */
  OP_glActiveTexture,
  OP_glBindTexture,
  OP_glGenTextures,
  OP_glDeleteTextures,
  OP_glTexImage2D,
  OP_glTexSubImage2D,
  OP_glCompressedTexImage2D,
  OP_glCompressedTexSubImage2D,
  OP_glCopyTexImage2D,
  OP_glCopyTexSubImage2D,
  OP_glTexParameterf,
  OP_glTexParameterfv,
  OP_glTexParameteri,
  OP_glTexParameteriv,
  OP_glGetTexParameterfv,
  OP_glGetTexParameteriv,
  OP_glGenerateMipmap,
  OP_glPixelStorei,
  /* ---- buffers ---- */
  OP_glGenBuffers,
  OP_glDeleteBuffers,
  OP_glBindBuffer,
  OP_glBufferData,
  OP_glBufferSubData,
  OP_glGetBufferParameteriv,
  /* ---- framebuffers / renderbuffers ---- */
  OP_glGenFramebuffers,
  OP_glDeleteFramebuffers,
  OP_glBindFramebuffer,
  OP_glFramebufferTexture2D,
  OP_glFramebufferRenderbuffer,
  OP_glCheckFramebufferStatus,
  OP_glGetFramebufferAttachmentParameteriv,
  OP_glGenRenderbuffers,
  OP_glDeleteRenderbuffers,
  OP_glBindRenderbuffer,
  OP_glRenderbufferStorage,
  OP_glGetRenderbufferParameteriv,
  /* ---- shaders / programs ---- */
  OP_glCreateShader,
  OP_glDeleteShader,
  OP_glShaderSource,
  OP_glCompileShader,
  OP_glGetShaderiv,
  OP_glGetShaderInfoLog,
  OP_glGetShaderSource,
  OP_glShaderBinary,
  OP_glReleaseShaderCompiler,
  OP_glGetShaderPrecisionFormat,
  OP_glCreateProgram,
  OP_glDeleteProgram,
  OP_glAttachShader,
  OP_glDetachShader,
  OP_glLinkProgram,
  OP_glUseProgram,
  OP_glValidateProgram,
  OP_glGetProgramiv,
  OP_glGetProgramInfoLog,
  OP_glGetAttachedShaders,
  /* ---- uniforms ---- */
  OP_glGetUniformLocation,
  OP_glGetActiveUniform,
  OP_glGetUniformfv,
  OP_glGetUniformiv,
  OP_glUniform1f,
  OP_glUniform1fv,
  OP_glUniform1i,
  OP_glUniform1iv,
  OP_glUniform2f,
  OP_glUniform2fv,
  OP_glUniform2i,
  OP_glUniform2iv,
  OP_glUniform3f,
  OP_glUniform3fv,
  OP_glUniform3i,
  OP_glUniform3iv,
  OP_glUniform4f,
  OP_glUniform4fv,
  OP_glUniform4i,
  OP_glUniform4iv,
  OP_glUniformMatrix2fv,
  OP_glUniformMatrix3fv,
  OP_glUniformMatrix4fv,
  /* ---- attributes ---- */
  OP_glGetAttribLocation,
  OP_glGetActiveAttrib,
  OP_glBindAttribLocation,
  OP_glVertexAttribPointer,
  OP_glEnableVertexAttribArray,
  OP_glDisableVertexAttribArray,
  OP_glGetVertexAttribfv,
  OP_glGetVertexAttribiv,
  OP_glGetVertexAttribPointerv,
  OP_glVertexAttrib1f,
  OP_glVertexAttrib1fv,
  OP_glVertexAttrib2f,
  OP_glVertexAttrib2fv,
  OP_glVertexAttrib3f,
  OP_glVertexAttrib3fv,
  OP_glVertexAttrib4f,
  OP_glVertexAttrib4fv,
  /* ---- draw ---- */
  OP_glDrawArrays,
  OP_glDrawElements,
  /* ---- rasterisation state ---- */
  OP_glViewport,
  OP_glScissor,
  OP_glEnable,
  OP_glDisable,
  OP_glIsEnabled,
  OP_glCullFace,
  OP_glFrontFace,
  OP_glLineWidth,
  OP_glPolygonOffset,
  OP_glSampleCoverage,
  OP_glHint,
  /* ---- blend / depth / stencil ---- */
  OP_glBlendColor,
  OP_glBlendEquation,
  OP_glBlendEquationSeparate,
  OP_glBlendFunc,
  OP_glBlendFuncSeparate,
  OP_glDepthFunc,
  OP_glDepthMask,
  OP_glDepthRangef,
  OP_glColorMask,
  OP_glStencilFunc,
  OP_glStencilFuncSeparate,
  OP_glStencilMask,
  OP_glStencilMaskSeparate,
  OP_glStencilOp,
  OP_glStencilOpSeparate,
  /* ---- clear ---- */
  OP_glClear,
  OP_glClearColor,
  OP_glClearDepthf,
  OP_glClearStencil,
  /* ---- query ---- */
  OP_glGetError,
  OP_glGetBooleanv,
  OP_glGetFloatv,
  OP_glGetIntegerv,
  OP_glGetString,
  OP_glReadPixels,
  /* ---- misc ---- */
  OP_glFinish,
  OP_glFlush,
  OP_glIsBuffer,
  OP_glIsFramebuffer,
  OP_glIsProgram,
  OP_glIsRenderbuffer,
  OP_glIsShader,
  OP_glIsTexture,
  /* ---- EGL ---- */
  OP_eglGetError,
  OP_eglGetDisplay,
  OP_eglInitialize,
  OP_eglTerminate,
  OP_eglQueryString,
  OP_eglGetConfigs,
  OP_eglChooseConfig,
  OP_eglGetConfigAttrib,
  OP_eglCreateWindowSurface,
  OP_eglCreatePbufferSurface,
  OP_eglCreatePixmapSurface,
  OP_eglDestroySurface,
  OP_eglQuerySurface,
  OP_eglSurfaceAttrib,
  OP_eglBindTexImage,
  OP_eglReleaseTexImage,
  OP_eglCreateContext,
  OP_eglDestroyContext,
  OP_eglMakeCurrent,
  OP_eglGetCurrentContext,
  OP_eglGetCurrentSurface,
  OP_eglGetCurrentDisplay,
  OP_eglQueryContext,
  OP_eglSwapBuffers,
  OP_eglSwapInterval,
  OP_eglWaitGL,
  OP_eglWaitNative,
  OP_eglWaitClient,
  OP_eglBindAPI,
  OP_eglQueryAPI,
  OP_eglReleaseThread,
  OP_eglGetProcAddress,
  OP_eglGetPlatformDisplayEXT,
  OP_eglCopyBuffers,
  OP_eglCreateImageKHR,
  OP_eglDestroyImageKHR,

  /* ================================================================ */
  /* GLES 3.0                                                         */
  /* ================================================================ */

  /* ---- Vertex Arrays ---- */
  OP_glBindVertexArray,
  OP_glDeleteVertexArrays,
  OP_glGenVertexArrays,
  OP_glIsVertexArray,

  /* ---- Integer Attributes ---- */
  OP_glGetVertexAttribIiv,
  OP_glGetVertexAttribIuiv,
  OP_glVertexAttribI4i,
  OP_glVertexAttribI4iv,
  OP_glVertexAttribI4ui,
  OP_glVertexAttribI4uiv,
  OP_glVertexAttribIPointer,

  /* ---- Instancing ---- */
  OP_glDrawArraysInstanced,
  OP_glDrawElementsInstanced,
  OP_glVertexAttribDivisor,

  /* ---- Buffer Mapping ---- */
  OP_glCopyBufferSubData,
  OP_glFlushMappedBufferRange,
  OP_glGetBufferPointerv,
  OP_glMapBufferRange,
  OP_glUnmapBuffer,

  /* ---- Query Objects ---- */
  OP_glBeginQuery,
  OP_glDeleteQueries,
  OP_glEndQuery,
  OP_glGenQueries,
  OP_glGetQueryiv,
  OP_glGetQueryObjectuiv,
  OP_glIsQuery,

  /* ---- Samplers ---- */
  OP_glBindSampler,
  OP_glDeleteSamplers,
  OP_glGenSamplers,
  OP_glGetSamplerParameterfv,
  OP_glGetSamplerParameteriv,
  OP_glIsSampler,
  OP_glSamplerParameterf,
  OP_glSamplerParameterfv,
  OP_glSamplerParameteri,
  OP_glSamplerParameteriv,

  /* ---- Transform Feedback ---- */
  OP_glBeginTransformFeedback,
  OP_glBindBufferBase,
  OP_glBindBufferRange,
  OP_glBindTransformFeedback,
  OP_glDeleteTransformFeedbacks,
  OP_glEndTransformFeedback,
  OP_glGenTransformFeedbacks,
  OP_glGetTransformFeedbackVarying,
  OP_glIsTransformFeedback,
  OP_glPauseTransformFeedback,
  OP_glResumeTransformFeedback,
  OP_glTransformFeedbackVaryings,

  /* ---- Uniform Integer ---- */
  OP_glGetUniformuiv,
  OP_glUniform1ui,
  OP_glUniform1uiv,
  OP_glUniform2ui,
  OP_glUniform2uiv,
  OP_glUniform3ui,
  OP_glUniform3uiv,
  OP_glUniform4ui,
  OP_glUniform4uiv,

  /* ---- Uniform Blocks ---- */
  OP_glGetActiveUniformsiv,
  OP_glGetActiveUniformBlockiv,
  OP_glGetActiveUniformBlockName,
  OP_glGetUniformBlockIndex,
  OP_glGetUniformIndices,
  OP_glUniformBlockBinding,

  /* ---- 3D Textures ---- */
  OP_glCompressedTexImage3D,
  OP_glCompressedTexSubImage3D,
  OP_glCopyTexSubImage3D,
  OP_glTexImage3D,
  OP_glTexStorage2D,
  OP_glTexStorage3D,
  OP_glTexSubImage3D,

  /* ---- Indexed State ---- */
  OP_glColorMaski,
  OP_glDisablei,
  OP_glEnablei,
  OP_glGetBooleani_v,
  OP_glGetTexParameterIiv,
  OP_glGetTexParameterIuiv,
  OP_glIsEnabledi,
  OP_glTexParameterIiv,
  OP_glTexParameterIuiv,

  /* ---- Sync ---- */
  OP_glClientWaitSync,
  OP_glDeleteSync,
  OP_glFenceSync,
  OP_glGetSynciv,
  OP_glIsSync,
  OP_glWaitSync,

  /* ---- Misc ---- */
  OP_glGetFragDataLocation,
  OP_glGetInteger64v,
  OP_glGetStringi,

  /* ---- Multisample/FBO ---- */
  OP_glBlitFramebuffer,
  OP_glFramebufferTextureLayer,
  OP_glRenderbufferStorageMultisample,

  /* ================================================================ */
  /* GLES 3.2                                                         */
  /* ================================================================ */

  /* ---- Debug ---- */
  OP_glBindImageTexture,
  OP_glDebugMessageCallback,
  OP_glDebugMessageControl,
  OP_glDebugMessageInsert,
  OP_glDrawRangeElements,
  OP_glGetProgramBinary,
  OP_glGetPointerv,
  OP_glPopDebugGroup,

  OP_glClearBufferfi,
  OP_glClearBufferfv,
  OP_glClearBufferiv,
  OP_glClearBufferuiv,
  OP_glCopyImageSubData,
  OP_glDispatchCompute,
  OP_glDispatchComputeIndirect,
  OP_glDrawArraysIndirect,
  OP_glDrawBuffers,
  OP_glDrawElementsBaseVertex,
  OP_glDrawElementsInstancedBaseVertex,
  OP_glDrawElementsIndirect,
  OP_glDrawRangeElementsBaseVertex,
  OP_glFramebufferTexture,

  OP_glGetBufferParameteri64v,
  OP_glGetDebugMessageLog,
  OP_glGetInternalformativ,
  OP_glGetInteger64i_v,
  OP_glGetIntegeri_v,
  OP_glGetObjectLabel,
  OP_glGetObjectPtrLabel,
  OP_glGetSamplerParameterIiv,
  OP_glGetSamplerParameterIuiv,

  OP_glMemoryBarrier,
  OP_glMemoryBarrierByRegion,
  OP_glMinSampleShading,
  OP_glObjectLabel,
  OP_glObjectPtrLabel,
  OP_glProgramBinary,
  OP_glProgramParameteri,
  OP_glPushDebugGroup,
  OP_glReadBuffer,
  OP_glReadnPixels,
  OP_glSamplerParameterIiv,
  OP_glSamplerParameterIuiv,
  OP_glTexBuffer,
  OP_glTexBufferRange,
  OP_glTexStorage2DMultisample,
  OP_glTexStorage3DMultisample,
  OP_glPrimitiveBoundingBox,

  /* ---- Program Pipelines / Shaders ---- */
  OP_glActiveShaderProgram,
  OP_glBindProgramPipeline,
  OP_glCreateShaderProgramv,
  OP_glDeleteProgramPipelines,
  OP_glGenProgramPipelines,
  OP_glGetProgramInterfaceiv,
  OP_glGetProgramPipelineInfoLog,
  OP_glGetProgramResourceIndex,
  OP_glGetProgramResourceiv,
  OP_glGetProgramResourceLocation,
  OP_glGetProgramResourceName,
  OP_glIsProgramPipeline,
  OP_glUseProgramStages,
  OP_glValidateProgramPipeline,

  /* ---- State / Misc 3.2 ---- */
  OP_glBlendBarrier,
  OP_glGetGraphicsResetStatus,
  OP_glGetnUniformfv,
  OP_glGetnUniformiv,
  OP_glGetnUniformuiv,
  OP_glPatchParameteri,

  /* ---- Matrices ---- */
  OP_glUniformMatrix2x3fv,
  OP_glUniformMatrix2x4fv,
  OP_glUniformMatrix3x2fv,
  OP_glUniformMatrix3x4fv,
  OP_glUniformMatrix4x2fv,
  OP_glUniformMatrix4x3fv,

#ifdef HAVE_OWN_WAYLAND_EGL
  /* ── Wayland - EGL bridge ── */
  OP_wl_egl_window_create,
  OP_wl_egl_window_destroy,
  OP_wl_egl_window_resize,
#endif

  OP_Sync,

  /* should always be last as dynamic functions */
  OP_InvokeDynamic,
  OP_InvokeDynamicEGL,

  OP_MAX
} GLBridgeOpcode;

/* ── Wayland Opcodes ────────────────────────────────────────────────────────
 */
typedef enum
{
  OP_WL_NONE = 0,

  OP_wl_display_connect, /* proxy: connect + discover globals      */
  OP_wl_display_disconnect,
  OP_wl_roundtrip,                  /* proxy: dispatch + return events        */
  OP_wl_flush,                      /* proxy: display flush                   */
  OP_wl_compositor_create_surface,  /* proxy: create wl_surface - slot index  */
  OP_wl_surface_commit,             /* proxy: commit surface[slot]            */
  OP_wl_surface_destroy,            /* proxy: destroy surface[slot]           */
  OP_wl_webos_set_property,         /* proxy: set_property on webos_ss[slot]  */
  OP_wl_shell_get_shell_surface,    /* proxy: wl_shell_get_shell_surface      */
  OP_wl_shell_surface_set_toplevel, /* proxy: wl_shell_surface_set_toplevel   */
  OP_wl_webos_shell_get_shell_surface, /* proxy:
                                          wl_webos_shell_get_shell_surface*/
  OP_wl_proxy_destroy, /* proxy: destroy any tracked proxy       */
  OP_wl_webos_shell_surface_set_state, /* proxy: set_state on webos_ss[slot] */

  OP_WL_MAX
} WLBridgeOpcode;

static inline char *opcode_to_string(GLBridgeOpcode op)
{
  switch (op)
  {
  case OP_NONE:
    return "OP_NONE";

  /* ---- textures ---- */
  case OP_glActiveTexture:
    return "glActiveTexture";
  case OP_glBindTexture:
    return "glBindTexture";
  case OP_glGenTextures:
    return "glGenTextures";
  case OP_glDeleteTextures:
    return "glDeleteTextures";
  case OP_glTexImage2D:
    return "glTexImage2D";
  case OP_glTexSubImage2D:
    return "glTexSubImage2D";
  case OP_glCompressedTexImage2D:
    return "glCompressedTexImage2D";
  case OP_glCompressedTexSubImage2D:
    return "glCompressedTexSubImage2D";
  case OP_glCopyTexImage2D:
    return "glCopyTexImage2D";
  case OP_glCopyTexSubImage2D:
    return "glCopyTexSubImage2D";
  case OP_glTexParameterf:
    return "glTexParameterf";
  case OP_glTexParameterfv:
    return "glTexParameterfv";
  case OP_glTexParameteri:
    return "glTexParameteri";
  case OP_glTexParameteriv:
    return "glTexParameteriv";
  case OP_glGetTexParameterfv:
    return "glGetTexParameterfv";
  case OP_glGetTexParameteriv:
    return "glGetTexParameteriv";
  case OP_glGenerateMipmap:
    return "glGenerateMipmap";
  case OP_glPixelStorei:
    return "glPixelStorei";

  /* ---- buffers ---- */
  case OP_glGenBuffers:
    return "glGenBuffers";
  case OP_glDeleteBuffers:
    return "glDeleteBuffers";
  case OP_glBindBuffer:
    return "glBindBuffer";
  case OP_glBufferData:
    return "glBufferData";
  case OP_glBufferSubData:
    return "glBufferSubData";
  case OP_glGetBufferParameteriv:
    return "glGetBufferParameteriv";

  /* ---- framebuffers / renderbuffers ---- */
  case OP_glGenFramebuffers:
    return "glGenFramebuffers";
  case OP_glDeleteFramebuffers:
    return "glDeleteFramebuffers";
  case OP_glBindFramebuffer:
    return "glBindFramebuffer";
  case OP_glFramebufferTexture2D:
    return "glFramebufferTexture2D";
  case OP_glFramebufferRenderbuffer:
    return "glFramebufferRenderbuffer";
  case OP_glCheckFramebufferStatus:
    return "glCheckFramebufferStatus";
  case OP_glGetFramebufferAttachmentParameteriv:
    return "glGetFramebufferAttachmentParameteriv";
  case OP_glGenRenderbuffers:
    return "glGenRenderbuffers";
  case OP_glDeleteRenderbuffers:
    return "glDeleteRenderbuffers";
  case OP_glBindRenderbuffer:
    return "glBindRenderbuffer";
  case OP_glRenderbufferStorage:
    return "glRenderbufferStorage";
  case OP_glGetRenderbufferParameteriv:
    return "glGetRenderbufferParameteriv";

  /* ---- shaders / programs ---- */
  case OP_glCreateShader:
    return "glCreateShader";
  case OP_glDeleteShader:
    return "glDeleteShader";
  case OP_glShaderSource:
    return "glShaderSource";
  case OP_glCompileShader:
    return "glCompileShader";
  case OP_glGetShaderiv:
    return "glGetShaderiv";
  case OP_glGetShaderInfoLog:
    return "glGetShaderInfoLog";
  case OP_glGetShaderSource:
    return "glGetShaderSource";
  case OP_glShaderBinary:
    return "glShaderBinary";
  case OP_glReleaseShaderCompiler:
    return "glReleaseShaderCompiler";
  case OP_glGetShaderPrecisionFormat:
    return "glGetShaderPrecisionFormat";
  case OP_glCreateProgram:
    return "glCreateProgram";
  case OP_glDeleteProgram:
    return "glDeleteProgram";
  case OP_glAttachShader:
    return "glAttachShader";
  case OP_glDetachShader:
    return "glDetachShader";
  case OP_glLinkProgram:
    return "glLinkProgram";
  case OP_glUseProgram:
    return "glUseProgram";
  case OP_glValidateProgram:
    return "glValidateProgram";
  case OP_glGetProgramiv:
    return "glGetProgramiv";
  case OP_glGetProgramInfoLog:
    return "glGetProgramInfoLog";
  case OP_glGetAttachedShaders:
    return "glGetAttachedShaders";

  /* ---- uniforms ---- */
  case OP_glGetUniformLocation:
    return "glGetUniformLocation";
  case OP_glGetActiveUniform:
    return "glGetActiveUniform";
  case OP_glGetUniformfv:
    return "glGetUniformfv";
  case OP_glGetUniformiv:
    return "glGetUniformiv";
  case OP_glUniform1f:
    return "glUniform1f";
  case OP_glUniform1fv:
    return "glUniform1fv";
  case OP_glUniform1i:
    return "glUniform1i";
  case OP_glUniform1iv:
    return "glUniform1iv";
  case OP_glUniform2f:
    return "glUniform2f";
  case OP_glUniform2fv:
    return "glUniform2fv";
  case OP_glUniform2i:
    return "glUniform2i";
  case OP_glUniform2iv:
    return "glUniform2iv";
  case OP_glUniform3f:
    return "glUniform3f";
  case OP_glUniform3fv:
    return "glUniform3fv";
  case OP_glUniform3i:
    return "glUniform3i";
  case OP_glUniform3iv:
    return "glUniform3iv";
  case OP_glUniform4f:
    return "glUniform4f";
  case OP_glUniform4fv:
    return "glUniform4fv";
  case OP_glUniform4i:
    return "glUniform4i";
  case OP_glUniform4iv:
    return "glUniform4iv";
  case OP_glUniformMatrix2fv:
    return "glUniformMatrix2fv";
  case OP_glUniformMatrix3fv:
    return "glUniformMatrix3fv";
  case OP_glUniformMatrix4fv:
    return "glUniformMatrix4fv";

  /* ---- attributes ---- */
  case OP_glGetAttribLocation:
    return "glGetAttribLocation";
  case OP_glGetActiveAttrib:
    return "glGetActiveAttrib";
  case OP_glBindAttribLocation:
    return "glBindAttribLocation";
  case OP_glVertexAttribPointer:
    return "glVertexAttribPointer";
  case OP_glEnableVertexAttribArray:
    return "glEnableVertexAttribArray";
  case OP_glDisableVertexAttribArray:
    return "glDisableVertexAttribArray";
  case OP_glGetVertexAttribfv:
    return "glGetVertexAttribfv";
  case OP_glGetVertexAttribiv:
    return "glGetVertexAttribiv";
  case OP_glGetVertexAttribPointerv:
    return "glGetVertexAttribPointerv";
  case OP_glVertexAttrib1f:
    return "glVertexAttrib1f";
  case OP_glVertexAttrib1fv:
    return "glVertexAttrib1fv";
  case OP_glVertexAttrib2f:
    return "glVertexAttrib2f";
  case OP_glVertexAttrib2fv:
    return "glVertexAttrib2fv";
  case OP_glVertexAttrib3f:
    return "glVertexAttrib3f";
  case OP_glVertexAttrib3fv:
    return "glVertexAttrib3fv";
  case OP_glVertexAttrib4f:
    return "glVertexAttrib4f";
  case OP_glVertexAttrib4fv:
    return "glVertexAttrib4fv";

  /* ---- draw ---- */
  case OP_glDrawArrays:
    return "glDrawArrays";
  case OP_glDrawElements:
    return "glDrawElements";

  /* ---- rasterisation state ---- */
  case OP_glViewport:
    return "glViewport";
  case OP_glScissor:
    return "glScissor";
  case OP_glEnable:
    return "glEnable";
  case OP_glDisable:
    return "glDisable";
  case OP_glIsEnabled:
    return "glIsEnabled";
  case OP_glCullFace:
    return "glCullFace";
  case OP_glFrontFace:
    return "glFrontFace";
  case OP_glLineWidth:
    return "glLineWidth";
  case OP_glPolygonOffset:
    return "glPolygonOffset";
  case OP_glSampleCoverage:
    return "glSampleCoverage";
  case OP_glHint:
    return "glHint";

  /* ---- blend / depth / stencil ---- */
  case OP_glBlendColor:
    return "glBlendColor";
  case OP_glBlendEquation:
    return "glBlendEquation";
  case OP_glBlendEquationSeparate:
    return "glBlendEquationSeparate";
  case OP_glBlendFunc:
    return "glBlendFunc";
  case OP_glBlendFuncSeparate:
    return "glBlendFuncSeparate";
  case OP_glDepthFunc:
    return "glDepthFunc";
  case OP_glDepthMask:
    return "glDepthMask";
  case OP_glDepthRangef:
    return "glDepthRangef";
  case OP_glColorMask:
    return "glColorMask";
  case OP_glStencilFunc:
    return "glStencilFunc";
  case OP_glStencilFuncSeparate:
    return "glStencilFuncSeparate";
  case OP_glStencilMask:
    return "glStencilMask";
  case OP_glStencilMaskSeparate:
    return "glStencilMaskSeparate";
  case OP_glStencilOp:
    return "glStencilOp";
  case OP_glStencilOpSeparate:
    return "glStencilOpSeparate";

  /* ---- clear ---- */
  case OP_glClear:
    return "glClear";
  case OP_glClearColor:
    return "glClearColor";
  case OP_glClearDepthf:
    return "glClearDepthf";
  case OP_glClearStencil:
    return "glClearStencil";

  /* ---- query ---- */
  case OP_glGetError:
    return "glGetError";
  case OP_glGetBooleanv:
    return "glGetBooleanv";
  case OP_glGetFloatv:
    return "glGetFloatv";
  case OP_glGetIntegerv:
    return "glGetIntegerv";
  case OP_glGetString:
    return "glGetString";
  case OP_glReadPixels:
    return "glReadPixels";

  /* ---- misc ---- */
  case OP_glFinish:
    return "glFinish";
  case OP_glFlush:
    return "glFlush";
  case OP_glIsBuffer:
    return "glIsBuffer";
  case OP_glIsFramebuffer:
    return "glIsFramebuffer";
  case OP_glIsProgram:
    return "glIsProgram";
  case OP_glIsRenderbuffer:
    return "glIsRenderbuffer";
  case OP_glIsShader:
    return "glIsShader";
  case OP_glIsTexture:
    return "glIsTexture";

  /* ---- EGL ---- */
  case OP_eglGetError:
    return "eglGetError";
  case OP_eglGetDisplay:
    return "eglGetDisplay";
  case OP_eglInitialize:
    return "eglInitialize";
  case OP_eglTerminate:
    return "eglTerminate";
  case OP_eglQueryString:
    return "eglQueryString";
  case OP_eglGetConfigs:
    return "eglGetConfigs";
  case OP_eglChooseConfig:
    return "eglChooseConfig";
  case OP_eglGetConfigAttrib:
    return "eglGetConfigAttrib";
  case OP_eglCreateWindowSurface:
    return "eglCreateWindowSurface";
  case OP_eglCreatePbufferSurface:
    return "eglCreatePbufferSurface";
  case OP_eglCreatePixmapSurface:
    return "eglCreatePixmapSurface";
  case OP_eglDestroySurface:
    return "eglDestroySurface";
  case OP_eglQuerySurface:
    return "eglQuerySurface";
  case OP_eglSurfaceAttrib:
    return "eglSurfaceAttrib";
  case OP_eglBindTexImage:
    return "eglBindTexImage";
  case OP_eglReleaseTexImage:
    return "eglReleaseTexImage";
  case OP_eglCreateContext:
    return "eglCreateContext";
  case OP_eglDestroyContext:
    return "eglDestroyContext";
  case OP_eglMakeCurrent:
    return "eglMakeCurrent";
  case OP_eglGetCurrentContext:
    return "eglGetCurrentContext";
  case OP_eglGetCurrentSurface:
    return "eglGetCurrentSurface";
  case OP_eglGetCurrentDisplay:
    return "eglGetCurrentDisplay";
  case OP_eglQueryContext:
    return "eglQueryContext";
  case OP_eglSwapBuffers:
    return "eglSwapBuffers";
  case OP_eglSwapInterval:
    return "eglSwapInterval";
  case OP_eglWaitGL:
    return "eglWaitGL";
  case OP_eglWaitNative:
    return "eglWaitNative";
  case OP_eglWaitClient:
    return "eglWaitClient";
  case OP_eglBindAPI:
    return "eglBindAPI";
  case OP_eglQueryAPI:
    return "eglQueryAPI";
  case OP_eglReleaseThread:
    return "eglReleaseThread";
  case OP_eglGetProcAddress:
    return "eglGetProcAddress";
  case OP_eglGetPlatformDisplayEXT:
    return "eglGetPlatformDisplayEXT";
  case OP_eglCopyBuffers:
    return "eglCopyBuffers";
  case OP_eglCreateImageKHR:
    return "eglCreateImageKHR";
  case OP_eglDestroyImageKHR:
    return "eglDestroyImageKHR";

  case OP_glGenVertexArrays:
    return "glGenVertexArrays";
  case OP_glDeleteVertexArrays:
    return "glDeleteVertexArrays";
  case OP_glBindVertexArray:
    return "glBindVertexArray";
  case OP_glIsVertexArray:
    return "glIsVertexArray";

  case OP_glGenQueries:
    return "glGenQueries";
  case OP_glDeleteQueries:
    return "glDeleteQueries";
  case OP_glBeginQuery:
    return "glBeginQuery";
  case OP_glEndQuery:
    return "glEndQuery";
  case OP_glGetQueryiv:
    return "glGetQueryiv";
  case OP_glGetQueryObjectuiv:
    return "glGetQueryObjectuiv";
  case OP_glIsQuery:
    return "glIsQuery";

  case OP_glBindSampler:
    return "glBindSampler";
  case OP_glDeleteSamplers:
    return "glDeleteSamplers";
  case OP_glGenSamplers:
    return "glGenSamplers";
  case OP_glGetSamplerParameterfv:
    return "lGetSamplerParameterfv";
  case OP_glGetSamplerParameteriv:
    return "glGetSamplerParameteriv";
  case OP_glIsSampler:
    return "glIsSampler";
  case OP_glSamplerParameterf:
    return "glSamplerParameterf";
  case OP_glSamplerParameterfv:
    return "glSamplerParameterfv";
  case OP_glSamplerParameteri:
    return "glSamplerParameteri";

  case OP_glTexImage3D:
    return "glTexImage3D";
  case OP_glTexSubImage3D:
    return "glTexSubImage3D";
  case OP_glCopyTexSubImage3D:
    return "glCopyTexSubImage3D";
  case OP_glCompressedTexImage3D:
    return "glCompressedTexImage3D";
  case OP_glCompressedTexSubImage3D:
    return "glCompressedTexSubImage3D";
  case OP_glTexStorage2D:
    return "glTexStorage2D";
  case OP_glTexStorage3D:
    return "glTexStorage3D";

  case OP_glDrawArraysInstanced:
    return "glDrawArraysInstanced";
  case OP_glDrawElementsInstanced:
    return "glDrawElementsInstanced";
  case OP_glVertexAttribDivisor:
    return "glVertexAttribDivisor";

  case OP_glFenceSync:
    return "glFenceSync";
  case OP_glClientWaitSync:
    return "glClientWaitSync";
  case OP_glDeleteSync:
    return "glDeleteSync";

  case OP_glDebugMessageCallback:
    return "glDebugMessageCallback";
  case OP_glDebugMessageControl:
    return "glDebugMessageControl";

  case OP_glMapBufferRange:
    return "glMapBufferRange";
  case OP_glFlushMappedBufferRange:
    return "glFlushMappedBufferRange";
  case OP_glUnmapBuffer:
    return "glUnmapBuffer";
  case OP_glGetStringi:
    return "glGetStringi";
  case OP_glGetInteger64v:
    return "glGetInteger64v";
  case OP_glGetFragDataLocation:
    return "glGetFragDataLocation";
  case OP_glGetVertexAttribIiv:
    return "glGetVertexAttribIiv";
  case OP_glGetVertexAttribIuiv:
    return "glGetVertexAttribIuiv";

  case OP_glGetBooleani_v:
    return "glGetBooleani_v";

  case OP_glBeginTransformFeedback:
    return "glBeginTransformFeedback";
  case OP_glEndTransformFeedback:
    return "glEndTransformFeedback";
  case OP_glTransformFeedbackVaryings:
    return "glTransformFeedbackVaryings";
  case OP_glGetTransformFeedbackVarying:
    return "glGetTransformFeedbackVarying";

  case OP_glGetUniformuiv:
    return "glGetUniformuiv";
  case OP_glUniform1ui:
    return "glUniform1ui";
  case OP_glUniform1uiv:
    return "glUniform1uiv";
  case OP_glUniform2ui:
    return "glUniform2ui";
  case OP_glUniform2uiv:
    return "glUniform2uiv";
  case OP_glUniform3ui:
    return "glUniform3ui";
  case OP_glUniform3uiv:
    return "glUniform3uiv";
  case OP_glUniform4ui:
    return "glUniform4ui";
  case OP_glUniform4uiv:
    return "glUniform4uiv";

  case OP_glColorMaski:
    return "glColorMaski";
  case OP_glEnablei:
    return "glEnablei";
  case OP_glDisablei:
    return "glDisablei";
  case OP_glIsEnabledi:
    return "glIsEnabledi";
  case OP_glGetTexParameterIiv:
    return "glGetTexParameterIiv";
  case OP_glGetTexParameterIuiv:
    return "glGetTexParameterIuiv";
  case OP_glTexParameterIiv:
    return "glTexParameterIiv";
  case OP_glTexParameterIuiv:
    return "glTexParameterIuiv";
  case OP_glVertexAttribIPointer:
    return "glVertexAttribIPointer";
  case OP_glVertexAttribI4i:
    return "glVertexAttribI4i";
  case OP_glVertexAttribI4iv:
    return "glVertexAttribI4iv";
  case OP_glVertexAttribI4ui:
    return "glVertexAttribI4ui";
  case OP_glVertexAttribI4uiv:
    return "glVertexAttribI4uiv";
  case OP_glBindBufferBase:
    return "glBindBufferBase";
  case OP_glBindBufferRange:
    return "glBindBufferRange";
  /* ---- Multisample/FBO ---- */
  case OP_glBlitFramebuffer:
    return "glBlitFramebuffer";
  case OP_glFramebufferTextureLayer:
    return "glFramebufferTextureLayer";
  case OP_glRenderbufferStorageMultisample:
    return "glRenderbufferStorageMultisample";
  case OP_glBindImageTexture:
    return "glBindImageTexture";
  case OP_glDrawRangeElements:
    return "glDrawRangeElements";
  case OP_glSamplerParameterIiv:
    return "glSamplerParameterIiv";
  case OP_glGetPointerv:
    return "glGetPointerv";
  case OP_glPopDebugGroup:
    return "glPopDebugGroup";
  case OP_glGetProgramBinary:
    return "glGetProgramBinary";
  case OP_glClearBufferfi:
    return "glClearBufferfi";
  case OP_glClearBufferfv:
    return "glClearBufferfv";
  case OP_glClearBufferiv:
    return "glClearBufferiv";
  case OP_glClearBufferuiv:
    return "glClearBufferuiv";
  case OP_glCopyImageSubData:
    return "glCopyImageSubData";
  case OP_glDebugMessageInsert:
    return "glDebugMessageInsert";
  case OP_glDispatchCompute:
    return "glDispatchCompute";
  case OP_glDispatchComputeIndirect:
    return "glDispatchComputeIndirect";
  case OP_glDrawBuffers:
    return "glDrawBuffers";
  case OP_glDrawElementsBaseVertex:
    return "glDrawElementsBaseVertex";
  case OP_glDrawElementsInstancedBaseVertex:
    return "glDrawElementsInstancedBaseVertex";
  case OP_glDrawRangeElementsBaseVertex:
    return "glDrawRangeElementsBaseVertex";
  case OP_glFramebufferTexture:
    return "glFramebufferTexture";
  case OP_glGetBufferParameteri64v:
    return "glGetBufferParameteri64v";
  case OP_glGetDebugMessageLog:
    return "glGetDebugMessageLog";
  case OP_glGetInternalformativ:
    return "glGetInternalformativ";
  case OP_glGetInteger64i_v:
    return "glGetInteger64i_v";
  case OP_glGetIntegeri_v:
    return "glGetIntegeri_v";
  case OP_glGetObjectLabel:
    return "glGetObjectLabel";
  case OP_glGetObjectPtrLabel:
    return "glGetObjectPtrLabel";
  case OP_glGetSamplerParameterIiv:
    return "glGetSamplerParameterIiv";
  case OP_glGetSamplerParameterIuiv:
    return "glGetSamplerParameterIuiv";
  case OP_glMemoryBarrier:
    return "glMemoryBarrier";
  case OP_glMinSampleShading:
    return "glMinSampleShading";
  case OP_glObjectLabel:
    return "glObjectLabel";
  case OP_glObjectPtrLabel:
    return "glObjectPtrLabel";
  case OP_glProgramBinary:
    return "glProgramBinary";
  case OP_glProgramParameteri:
    return "glProgramParameteri";
  case OP_glPushDebugGroup:
    return "glPushDebugGroup";
  case OP_glReadBuffer:
    return "glReadBuffer";
  case OP_glSamplerParameterIuiv:
    return "glSamplerParameterIuiv";
  case OP_glTexBuffer:
    return "glTexBuffer";
  case OP_glTexStorage2DMultisample:
    return "glTexStorage2DMultisample";
  case OP_glTexStorage3DMultisample:
    return "glTexStorage3DMultisample";
  case OP_glPrimitiveBoundingBox:
    return "glPrimitiveBoundingBox";
  case OP_glUseProgramStages:
    return "glUseProgramStages";
  case OP_glBindProgramPipeline:
    return "glBindProgramPipeline";
  case OP_glGenProgramPipelines:
    return "glGenProgramPipelines";
  case OP_glDeleteProgramPipelines:
    return "glDeleteProgramPipelines";
  case OP_glIsProgramPipeline:
    return "glIsProgramPipeline";
  case OP_glDrawArraysIndirect:
    return "glDrawArraysIndirect";
  case OP_glUniformMatrix2x3fv:
    return "glUniformMatrix2x3fv";
  case OP_glUniformMatrix3x2fv:
    return "glUniformMatrix3x2fv";
  case OP_glUniformMatrix2x4fv:
    return "glUniformMatrix2x4fv";
  case OP_glUniformMatrix4x2fv:
    return "glUniformMatrix4x2fv";
  case OP_glUniformMatrix3x4fv:
    return "glUniformMatrix3x4fv";
  case OP_glUniformMatrix4x3fv:
    return "glUniformMatrix4x3fv";

  case OP_glGetProgramInterfaceiv:
    return "glGetProgramInterfaceiv";
  case OP_glGetProgramResourceIndex:
    return "glGetProgramResourceIndex";
  case OP_glGetProgramResourceName:
    return "glGetProgramResourceName";
  case OP_glGetProgramResourceiv:
    return "glGetProgramResourceiv";
  case OP_glGetProgramResourceLocation:
    return "glGetProgramResourceLocation";

  case OP_glActiveShaderProgram:
    return "glActiveShaderProgram";
  case OP_glCreateShaderProgramv:
    return "glCreateShaderProgramv";
  case OP_glValidateProgramPipeline:
    return "glValidateProgramPipeline";
  case OP_glGetProgramPipelineInfoLog:
    return "glGetProgramPipelineInfoLog";

  case OP_glMemoryBarrierByRegion:
    return "glMemoryBarrierByRegion";
  case OP_glBlendBarrier:
    return "glBlendBarrier";

  case OP_glGetGraphicsResetStatus:
    return "glGetGraphicsResetStatus";
  case OP_glReadnPixels:
    return "glReadnPixels";
  case OP_glGetnUniformfv:
    return "glGetnUniformfv";
  case OP_glGetnUniformiv:
    return "glGetnUniformiv";
  case OP_glGetnUniformuiv:
    return "glGetnUniformuiv";

  case OP_glPatchParameteri:
    return "glPatchParameteri";
  case OP_glTexBufferRange:
    return "glTexBufferRange";

#ifdef HAVE_OWN_WAYLAND_EGL
  /* ---- Wayland EGL bridge ---- */
  case OP_wl_egl_window_create:
    return "wl_egl_window_create";
  case OP_wl_egl_window_destroy:
    return "wl_egl_window_destroy";
  case OP_wl_egl_window_resize:
    return "wl_egl_window_resize";
#endif

  case OP_Sync:
    return "Sync";

  case OP_InvokeDynamic:
    return "InvokeDynamic";

  case OP_InvokeDynamicEGL:
    return "InvokeDynamicEGL";

  default:
    return "UNKNOWN_OPCODE";
  }
}

static inline char *wl_opcode_to_string(WLBridgeOpcode op)
{
  switch (op)
  {
  case OP_WL_NONE:
    return "OP_WL_NONE";

  case OP_wl_display_connect:
    return "wl_display_connect";
  case OP_wl_roundtrip:
    return "wl_roundtrip";
  case OP_wl_flush:
    return "wl_flush";
  case OP_wl_compositor_create_surface:
    return "wl_compositor_create_surface";
  case OP_wl_surface_commit:
    return "wl_surface_commit";
  case OP_wl_surface_destroy:
    return "wl_surface_destroy";
  case OP_wl_webos_set_property:
    return "wl_webos_set_property";
  case OP_wl_shell_get_shell_surface:
    return "wl_shell_get_shell_surface";
  case OP_wl_shell_surface_set_toplevel:
    return "wl_shell_surface_set_toplevel";
  case OP_wl_webos_shell_get_shell_surface:
    return "wl_webos_shell_get_shell_surface";
  case OP_wl_proxy_destroy:
    return "wl_proxy_destroy";
  case OP_wl_webos_shell_surface_set_state:
    return "wl_webos_shell_surface_set_state";

  default:
    return "UNKNOWN_WL_OPCODE";
  }
}

/* ── EGL handle indices ─────────────────────────────────────────────────────
 * EGLDisplay/EGLContext/EGLSurface/EGLConfig are opaque pointers.  On the
 * 32-bit proxy side they are real 32-bit pointers; on the 64-bit stub side
 * we represent them as small 1-based integer indices cast to pointer type.
 *
 *   Index 0  -  EGL_NO_DISPLAY / EGL_NO_CONTEXT / EGL_NO_SURFACE
 *   Index 1+ -  slot in the proxy's handle table
 *
 * The stub encodes:   aw_u32(&W, (uint32_t)(uintptr_t)handle)
 * The proxy decodes:  real_ptr = egl_displays[ar_u32(&r)]
 */
#define EGL_BRIDGE_MAX_DISPLAYS 4
#define EGL_BRIDGE_MAX_CONFIGS 128
#define EGL_BRIDGE_MAX_CONTEXTS 8
#define EGL_BRIDGE_MAX_SURFACES 16

#define IDX_MODE_POINTER 0
#define IDX_MODE_OFFSET 1
#define IDX_MODE_COPIED 2

/* ── Control structure (lives in /gles_bridge_ctrl) ───────────────────── */
typedef struct
{
  /* Command slot */
  uint32_t opcode;
  uint32_t needs_response; /* 1 = stub blocks on resp_sem        */

  /* Scalar arguments, packed with the helpers below */
  uint8_t args[BRIDGE_ARGS_SIZE];
  uint32_t args_len;

  /* Variable-length data in the data shm region */
  uint32_t data_offset; /* byte offset into data shm          */
  uint32_t data_size;

  /* Optional second data region (e.g. ReadPixels result buffer) */
  uint32_t data2_offset;
  uint32_t data2_size;

  /* Result from proxy */
  uint64_t result; /* scalar return value                */
  uint8_t result_buf[BRIDGE_RESULT_SIZE];
  uint32_t result_buf_len;

  /* Proxy liveness */
  int32_t proxy_pid;

  /* Client */
  int32_t client_pid;

  uint64_t data_watermark;
  uint8_t data_watermark_valid;

  uint8_t _pad[4]; /* keep struct size a multiple of 8   */
} BridgeCtrl;

#define BRIDGE_RING_SLOTS 256u
#define BRIDGE_RING_MASK (BRIDGE_RING_SLOTS - 1u)

typedef struct
{
  _Atomic uint64_t published_seq;
  _Atomic uint64_t completed_seq;
  _Atomic uint64_t data_tail;
  _Atomic uint32_t consumer_waiting;

  int32_t proxy_pid;
  int32_t client_pid;

  BridgeCtrl slots[BRIDGE_RING_SLOTS];
} BridgeRing;

#define GLES_BRIDGE_DATA_SIZE (64u * 1024u * 1024u) /* 64 MB  */
#define GLES_BRIDGE_CTRL_SIZE sizeof(BridgeRing)

/* ── Argument packing helpers (used by both stub and proxy) ─────────────── */
typedef struct
{
  uint8_t *buf;
  uint32_t pos;
  uint32_t cap;
} ArgWriter;

typedef struct
{
  const uint8_t *buf;
  uint32_t pos;
  uint32_t cap;
} ArgReader;

static inline ArgWriter aw_init(uint8_t *buf, uint32_t cap)
{
  ArgWriter w = {buf, 0, cap};
  return w;
}

static inline ArgReader ar_init(const uint8_t *buf, uint32_t len)
{
  ArgReader r = {buf, 0, len};
  return r;
}

#define _AW_WRITE(w, v, T)                                                     \
  do                                                                           \
  {                                                                            \
    memcpy((w)->buf + (w)->pos, &(v), sizeof(T));                              \
    (w)->pos += sizeof(T);                                                     \
  } while (0)
#define _AR_READ(r, T)                                                         \
  ({                                                                           \
    T _v;                                                                      \
    memcpy(&_v, (r)->buf + (r)->pos, sizeof(T));                               \
    (r)->pos += sizeof(T);                                                     \
    _v;                                                                        \
  })

static inline void aw_u32(ArgWriter *w, uint32_t v)
{
  _AW_WRITE(w, v, uint32_t);
}
static inline void aw_i32(ArgWriter *w, int32_t v)
{
  _AW_WRITE(w, v, int32_t);
}
static inline void aw_f32(ArgWriter *w, float v)
{
  _AW_WRITE(w, v, float);
}
static inline void aw_u64(ArgWriter *w, uint64_t v)
{
  _AW_WRITE(w, v, uint64_t);
}
static inline void aw_i64(ArgWriter *w, int64_t v)
{
  _AW_WRITE(w, v, int64_t);
}

static inline uint32_t ar_u32(ArgReader *r)
{
  return _AR_READ(r, uint32_t);
}
static inline int32_t ar_i32(ArgReader *r)
{
  return _AR_READ(r, int32_t);
}
static inline float ar_f32(ArgReader *r)
{
  return _AR_READ(r, float);
}
static inline uint64_t ar_u64(ArgReader *r)
{
  return _AR_READ(r, uint64_t);
}
static inline int64_t ar_i64(ArgReader *r)
{
  return _AR_READ(r, int64_t);
}

// see:
// https://gitlab.freedesktop.org/wayland/wayland/-/blob/1.22.0/egl/wayland-egl-backend.h
struct wl_egl_window
{
  const intptr_t version;

  int width;
  int height;
  int dx;
  int dy;

  int attached_width;
  int attached_height;

  void *driver_private;
  void (*resize_callback)(struct wl_egl_window *, void *);
  void (*destroy_window_callback)(void *);

  struct wl_surface *surface;

  /* additional fields */
  uint32_t slot;
};

#define INPUT_EVT_MAX 16

typedef struct
{
  BridgeCtrl *ctrl;
  uint8_t *data;

  uint32_t data_pos;
  pthread_mutex_t lock;

} WLBridge;
