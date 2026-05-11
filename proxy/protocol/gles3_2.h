#pragma once

/* ========================================================================= */
/* GLES 3.0                                                                  */
/* ========================================================================= */

/* ── Vertex Array Objects ──────────────────────────────────────────────── */

void h_glGenVertexArrays(BridgeCtrl *C, uint8_t *D);
void h_glDeleteVertexArrays(BridgeCtrl *C, uint8_t *D);
void h_glBindVertexArray(BridgeCtrl *C, uint8_t *D);
void h_glIsVertexArray(BridgeCtrl *C, uint8_t *D);

/* ── Vertex Attributes / Instancing ───────────────────────────────────── */

void h_glVertexAttribI4i(BridgeCtrl *C, uint8_t *D);
void h_glVertexAttribI4iv(BridgeCtrl *C, uint8_t *D);
void h_glVertexAttribI4ui(BridgeCtrl *C, uint8_t *D);
void h_glVertexAttribI4uiv(BridgeCtrl *C, uint8_t *D);

void h_glVertexAttribIPointer(BridgeCtrl *C, uint8_t *D);

void h_glGetVertexAttribIiv(BridgeCtrl *C, uint8_t *D);
void h_glGetVertexAttribIuiv(BridgeCtrl *C, uint8_t *D);

void h_glDrawArraysInstanced(BridgeCtrl *C, uint8_t *D);
void h_glDrawElementsInstanced(BridgeCtrl *C, uint8_t *D);

void h_glVertexAttribDivisor(BridgeCtrl *C, uint8_t *D);

/* ── Buffer Objects / Mapping ─────────────────────────────────────────── */

void h_glMapBufferRange(BridgeCtrl *C, uint8_t *D);
void h_glFlushMappedBufferRange(BridgeCtrl *C, uint8_t *D);
void h_glUnmapBuffer(BridgeCtrl *C, uint8_t *D);

void h_glCopyBufferSubData(BridgeCtrl *C, uint8_t *D);

void h_glBindBufferBase(BridgeCtrl *C, uint8_t *D);
void h_glBindBufferRange(BridgeCtrl *C, uint8_t *D);

void h_glGetBufferPointerv(BridgeCtrl *C, uint8_t *D);
void h_glGetBufferParameteri64v(BridgeCtrl *C, uint8_t *D);

/* ── Query Objects ────────────────────────────────────────────────────── */

void h_glGenQueries(BridgeCtrl *C, uint8_t *D);
void h_glDeleteQueries(BridgeCtrl *C, uint8_t *D);

void h_glBeginQuery(BridgeCtrl *C, uint8_t *D);
void h_glEndQuery(BridgeCtrl *C, uint8_t *D);

void h_glGetQueryiv(BridgeCtrl *C, uint8_t *D);
void h_glGetQueryObjectuiv(BridgeCtrl *C, uint8_t *D);

void h_glIsQuery(BridgeCtrl *C, uint8_t *D);

/* ── Sampler Objects ──────────────────────────────────────────────────── */

void h_glGenSamplers(BridgeCtrl *C, uint8_t *D);
void h_glDeleteSamplers(BridgeCtrl *C, uint8_t *D);

void h_glBindSampler(BridgeCtrl *C, uint8_t *D);

void h_glSamplerParameteri(BridgeCtrl *C, uint8_t *D);
void h_glSamplerParameteriv(BridgeCtrl *C, uint8_t *D);
void h_glSamplerParameterf(BridgeCtrl *C, uint8_t *D);
void h_glSamplerParameterfv(BridgeCtrl *C, uint8_t *D);

void h_glSamplerParameterIiv(BridgeCtrl *C, uint8_t *D);
void h_glSamplerParameterIuiv(BridgeCtrl *C, uint8_t *D);

void h_glGetSamplerParameteriv(BridgeCtrl *C, uint8_t *D);
void h_glGetSamplerParameterfv(BridgeCtrl *C, uint8_t *D);

void h_glGetSamplerParameterIiv(BridgeCtrl *C, uint8_t *D);
void h_glGetSamplerParameterIuiv(BridgeCtrl *C, uint8_t *D);

void h_glIsSampler(BridgeCtrl *C, uint8_t *D);

/* ── Transform Feedback ───────────────────────────────────────────────── */

void h_glBeginTransformFeedback(BridgeCtrl *C, uint8_t *D);
void h_glEndTransformFeedback(BridgeCtrl *C, uint8_t *D);

void h_glTransformFeedbackVaryings(BridgeCtrl *C, uint8_t *D);
void h_glGetTransformFeedbackVarying(BridgeCtrl *C, uint8_t *D);

void h_glGenTransformFeedbacks(BridgeCtrl *C, uint8_t *D);
void h_glDeleteTransformFeedbacks(BridgeCtrl *C, uint8_t *D);

void h_glBindTransformFeedback(BridgeCtrl *C, uint8_t *D);

void h_glPauseTransformFeedback(BridgeCtrl *C, uint8_t *D);
void h_glResumeTransformFeedback(BridgeCtrl *C, uint8_t *D);

void h_glIsTransformFeedback(BridgeCtrl *C, uint8_t *D);

/* ── Uniforms / Uniform Blocks ────────────────────────────────────────── */

void h_glGetUniformuiv(BridgeCtrl *C, uint8_t *D);

void h_glUniform1ui(BridgeCtrl *C, uint8_t *D);
void h_glUniform1uiv(BridgeCtrl *C, uint8_t *D);

void h_glUniform2ui(BridgeCtrl *C, uint8_t *D);
void h_glUniform2uiv(BridgeCtrl *C, uint8_t *D);

void h_glUniform3ui(BridgeCtrl *C, uint8_t *D);
void h_glUniform3uiv(BridgeCtrl *C, uint8_t *D);

void h_glUniform4ui(BridgeCtrl *C, uint8_t *D);
void h_glUniform4uiv(BridgeCtrl *C, uint8_t *D);

void h_glGetUniformIndices(BridgeCtrl *C, uint8_t *D);
void h_glGetActiveUniformsiv(BridgeCtrl *C, uint8_t *D);

void h_glGetUniformBlockIndex(BridgeCtrl *C, uint8_t *D);
void h_glGetActiveUniformBlockiv(BridgeCtrl *C, uint8_t *D);
void h_glGetActiveUniformBlockName(BridgeCtrl *C, uint8_t *D);

void h_glUniformBlockBinding(BridgeCtrl *C, uint8_t *D);

/* ── Texture Objects / Storage ────────────────────────────────────────── */

void h_glTexImage3D(BridgeCtrl *C, uint8_t *D);
void h_glTexSubImage3D(BridgeCtrl *C, uint8_t *D);
void h_glCopyTexSubImage3D(BridgeCtrl *C, uint8_t *D);

void h_glCompressedTexImage3D(BridgeCtrl *C, uint8_t *D);
void h_glCompressedTexSubImage3D(BridgeCtrl *C, uint8_t *D);

void h_glTexStorage2D(BridgeCtrl *C, uint8_t *D);
void h_glTexStorage3D(BridgeCtrl *C, uint8_t *D);

void h_glTexBuffer(BridgeCtrl *C, uint8_t *D);
void h_glTexBufferRange(BridgeCtrl *C, uint8_t *D);

void h_glCopyImageSubData(BridgeCtrl *C, uint8_t *D);

/* ── Indexed State / State Queries ───────────────────────────────────── */

void h_glColorMaski(BridgeCtrl *C, uint8_t *D);

void h_glEnablei(BridgeCtrl *C, uint8_t *D);
void h_glDisablei(BridgeCtrl *C, uint8_t *D);
void h_glIsEnabledi(BridgeCtrl *C, uint8_t *D);

void h_glGetBooleani_v(BridgeCtrl *C, uint8_t *D);

void h_glGetTexParameterIiv(BridgeCtrl *C, uint8_t *D);
void h_glGetTexParameterIuiv(BridgeCtrl *C, uint8_t *D);

void h_glTexParameterIiv(BridgeCtrl *C, uint8_t *D);
void h_glTexParameterIuiv(BridgeCtrl *C, uint8_t *D);

/* ── Sync Objects ─────────────────────────────────────────────────────── */

void h_glFenceSync(BridgeCtrl *C, uint8_t *D);
void h_glDeleteSync(BridgeCtrl *C, uint8_t *D);

void h_glClientWaitSync(BridgeCtrl *C, uint8_t *D);
void h_glWaitSync(BridgeCtrl *C, uint8_t *D);

void h_glGetSynciv(BridgeCtrl *C, uint8_t *D);
void h_glIsSync(BridgeCtrl *C, uint8_t *D);

/* ── Framebuffers / Multisample ───────────────────────────────────────── */

void h_glRenderbufferStorageMultisample(BridgeCtrl *C, uint8_t *D);
void h_glBlitFramebuffer(BridgeCtrl *C, uint8_t *D);

void h_glFramebufferTextureLayer(BridgeCtrl *C, uint8_t *D);
void h_glFramebufferTexture(BridgeCtrl *C, uint8_t *D);

void h_glReadBuffer(BridgeCtrl *C, uint8_t *D);
void h_glDrawBuffers(BridgeCtrl *C, uint8_t *D);

void h_glClearBufferiv(BridgeCtrl *C, uint8_t *D);
void h_glClearBufferuiv(BridgeCtrl *C, uint8_t *D);
void h_glClearBufferfv(BridgeCtrl *C, uint8_t *D);
void h_glClearBufferfi(BridgeCtrl *C, uint8_t *D);

/* ── Misc State / Queries ─────────────────────────────────────────────── */

void h_glGetStringi(BridgeCtrl *C, uint8_t *D);

void h_glGetInteger64v(BridgeCtrl *C, uint8_t *D);
void h_glGetInteger64i_v(BridgeCtrl *C, uint8_t *D);
void h_glGetIntegeri_v(BridgeCtrl *C, uint8_t *D);

void h_glGetInternalformativ(BridgeCtrl *C, uint8_t *D);

void h_glGetFragDataLocation(BridgeCtrl *C, uint8_t *D);
void h_glGetPointerv(BridgeCtrl *C, uint8_t *D);

/* ========================================================================= */
/* GLES 3.1                                                                  */
/* ========================================================================= */

/* ── Compute ───────────────────────────────────────────────────────────── */

void h_glDispatchCompute(BridgeCtrl *C, uint8_t *D);
void h_glDispatchComputeIndirect(BridgeCtrl *C, uint8_t *D);

void h_glMemoryBarrier(BridgeCtrl *C, uint8_t *D);
void h_glMemoryBarrierByRegion(BridgeCtrl *C, uint8_t *D);

/* ── Program Pipelines / Binary ───────────────────────────────────────── */

void h_glUseProgramStages(BridgeCtrl *C, uint8_t *D);

void h_glBindProgramPipeline(BridgeCtrl *C, uint8_t *D);
void h_glGenProgramPipelines(BridgeCtrl *C, uint8_t *D);
void h_glDeleteProgramPipelines(BridgeCtrl *C, uint8_t *D);

void h_glIsProgramPipeline(BridgeCtrl *C, uint8_t *D);

void h_glActiveShaderProgram(BridgeCtrl *C, uint8_t *D);
void h_glCreateShaderProgramv(BridgeCtrl *C, uint8_t *D);

void h_glValidateProgramPipeline(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramPipelineInfoLog(BridgeCtrl *C, uint8_t *D);

void h_glProgramBinary(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramBinary(BridgeCtrl *C, uint8_t *D);

void h_glProgramParameteri(BridgeCtrl *C, uint8_t *D);

/* ── Program Resources / Shader Storage ──────────────────────────────── */

void h_glGetProgramInterfaceiv(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramResourceIndex(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramResourceName(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramResourceiv(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramResourceLocation(BridgeCtrl *C, uint8_t *D);

/* ── Indirect / Base Vertex Draw ──────────────────────────────────────── */

void h_glDrawArraysIndirect(BridgeCtrl *C, uint8_t *D);
void h_glDrawElementsIndirect(BridgeCtrl *C, uint8_t *D);

void h_glDrawRangeElements(BridgeCtrl *C, uint8_t *D);

void h_glDrawElementsBaseVertex(BridgeCtrl *C, uint8_t *D);
void h_glDrawElementsInstancedBaseVertex(BridgeCtrl *C, uint8_t *D);
void h_glDrawRangeElementsBaseVertex(BridgeCtrl *C, uint8_t *D);

/* ========================================================================= */
/* GLES 3.2                                                                  */
/* ========================================================================= */

/* ── Debug / Labels ───────────────────────────────────────────────────── */

void h_glDebugMessageCallback(BridgeCtrl *C, uint8_t *D);
void h_glDebugMessageControl(BridgeCtrl *C, uint8_t *D);

void h_glDebugMessageInsert(BridgeCtrl *C, uint8_t *D);
void h_glGetDebugMessageLog(BridgeCtrl *C, uint8_t *D);

void h_glPushDebugGroup(BridgeCtrl *C, uint8_t *D);
void h_glPopDebugGroup(BridgeCtrl *C, uint8_t *D);

void h_glObjectLabel(BridgeCtrl *C, uint8_t *D);
void h_glGetObjectLabel(BridgeCtrl *C, uint8_t *D);

void h_glObjectPtrLabel(BridgeCtrl *C, uint8_t *D);
void h_glGetObjectPtrLabel(BridgeCtrl *C, uint8_t *D);

/* ── Image / Geometry / Robustness / Misc ────────────────────────────── */

void h_glBindImageTexture(BridgeCtrl *C, uint8_t *D);

void h_glPrimitiveBoundingBox(BridgeCtrl *C, uint8_t *D);

void h_glTexStorage2DMultisample(BridgeCtrl *C, uint8_t *D);
void h_glTexStorage3DMultisample(BridgeCtrl *C, uint8_t *D);

void h_glMinSampleShading(BridgeCtrl *C, uint8_t *D);

void h_glPatchParameteri(BridgeCtrl *C, uint8_t *D);

void h_glBlendBarrier(BridgeCtrl *C, uint8_t *D);

void h_glGetGraphicsResetStatus(BridgeCtrl *C, uint8_t *D);

void h_glReadnPixels(BridgeCtrl *C, uint8_t *D);

void h_glGetnUniformfv(BridgeCtrl *C, uint8_t *D);
void h_glGetnUniformiv(BridgeCtrl *C, uint8_t *D);
void h_glGetnUniformuiv(BridgeCtrl *C, uint8_t *D);

/* ── Matrix Variants ──────────────────────────────────────────────────── */

void h_glUniformMatrix2x3fv(BridgeCtrl *C, uint8_t *D);
void h_glUniformMatrix3x2fv(BridgeCtrl *C, uint8_t *D);

void h_glUniformMatrix2x4fv(BridgeCtrl *C, uint8_t *D);
void h_glUniformMatrix4x2fv(BridgeCtrl *C, uint8_t *D);

void h_glUniformMatrix3x4fv(BridgeCtrl *C, uint8_t *D);
void h_glUniformMatrix4x3fv(BridgeCtrl *C, uint8_t *D);
