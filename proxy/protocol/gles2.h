#pragma once

void h_glActiveTexture(BridgeCtrl *C, uint8_t *D);
void h_glBindTexture(BridgeCtrl *C, uint8_t *D);
void h_glGenTextures(BridgeCtrl *C, uint8_t *D);
void h_glDeleteTextures(BridgeCtrl *C, uint8_t *D);
void h_glTexImage2D(BridgeCtrl *C, uint8_t *D);
void h_glTexSubImage2D(BridgeCtrl *C, uint8_t *D);
void h_glCompressedTexImage2D(BridgeCtrl *C, uint8_t *D);
void h_glCompressedTexSubImage2D(BridgeCtrl *C, uint8_t *D);
void h_glCopyTexImage2D(BridgeCtrl *C, uint8_t *D);
void h_glCopyTexSubImage2D(BridgeCtrl *C, uint8_t *D);
void h_glTexParameterf(BridgeCtrl *C, uint8_t *D);
void h_glTexParameteri(BridgeCtrl *C, uint8_t *D);
void h_glTexParameterfv(BridgeCtrl *C, uint8_t *D);
void h_glTexParameteriv(BridgeCtrl *C, uint8_t *D);
void h_glGetTexParameterfv(BridgeCtrl *C, uint8_t *D);
void h_glGetTexParameteriv(BridgeCtrl *C, uint8_t *D);
void h_glGenerateMipmap(BridgeCtrl *C, uint8_t *D);
void h_glPixelStorei(BridgeCtrl *C, uint8_t *D);

/* ── Buffers ─────────────────────────────────────────────────────────────── */
void h_glGenBuffers(BridgeCtrl *C, uint8_t *D);
void h_glDeleteBuffers(BridgeCtrl *C, uint8_t *D);
void h_glBindBuffer(BridgeCtrl *C, uint8_t *D);
void h_glBufferData(BridgeCtrl *C, uint8_t *D);
void h_glBufferSubData(BridgeCtrl *C, uint8_t *D);
void h_glGetBufferParameteriv(BridgeCtrl *C, uint8_t *D);

/* ── Framebuffers / Renderbuffers ────────────────────────────────────────── */
void h_glGenFramebuffers(BridgeCtrl *C, uint8_t *D);
void h_glDeleteFramebuffers(BridgeCtrl *C, uint8_t *D);
void h_glBindFramebuffer(BridgeCtrl *C, uint8_t *D);
void h_glFramebufferTexture2D(BridgeCtrl *C, uint8_t *D);
void h_glFramebufferRenderbuffer(BridgeCtrl *C, uint8_t *D);
void h_glCheckFramebufferStatus(BridgeCtrl *C, uint8_t *D);
void h_glGetFramebufferAttachmentParameteriv(BridgeCtrl *C, uint8_t *D);
void h_glGenRenderbuffers(BridgeCtrl *C, uint8_t *D);
void h_glDeleteRenderbuffers(BridgeCtrl *C, uint8_t *D);
void h_glBindRenderbuffer(BridgeCtrl *C, uint8_t *D);
void h_glRenderbufferStorage(BridgeCtrl *C, uint8_t *D);
void h_glGetRenderbufferParameteriv(BridgeCtrl *C, uint8_t *D);

/* ── Shaders ─────────────────────────────────────────────────────────────── */
void h_glCreateShader(BridgeCtrl *C, uint8_t *D);
void h_glDeleteShader(BridgeCtrl *C, uint8_t *D);
void h_glShaderSource(BridgeCtrl *C, uint8_t *D);
void h_glCompileShader(BridgeCtrl *C, uint8_t *D);
void h_glGetShaderiv(BridgeCtrl *C, uint8_t *D);
void h_glGetShaderInfoLog(BridgeCtrl *C, uint8_t *D);
void h_glGetShaderSource(BridgeCtrl *C, uint8_t *D);
void h_glShaderBinary(BridgeCtrl *C, uint8_t *D);
void h_glReleaseShaderCompiler(BridgeCtrl *C, uint8_t *D);
void h_glGetShaderPrecisionFormat(BridgeCtrl *C, uint8_t *D);

/* ── Programs ────────────────────────────────────────────────────────────── */
void h_glCreateProgram(BridgeCtrl *C, uint8_t *D);
void h_glDeleteProgram(BridgeCtrl *C, uint8_t *D);
void h_glAttachShader(BridgeCtrl *C, uint8_t *D);
void h_glDetachShader(BridgeCtrl *C, uint8_t *D);
void h_glLinkProgram(BridgeCtrl *C, uint8_t *D);
void h_glUseProgram(BridgeCtrl *C, uint8_t *D);
void h_glValidateProgram(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramiv(BridgeCtrl *C, uint8_t *D);
void h_glGetProgramInfoLog(BridgeCtrl *C, uint8_t *D);
void h_glGetAttachedShaders(BridgeCtrl *C, uint8_t *D);

/* ── Uniforms ────────────────────────────────────────────────────────────── */
void h_glGetUniformLocation(BridgeCtrl *C, uint8_t *D);
void h_glGetActiveUniform(BridgeCtrl *C, uint8_t *D);
void h_glGetUniformfv(BridgeCtrl *C, uint8_t *D);
void h_glGetUniformiv(BridgeCtrl *C, uint8_t *D);

/* Scalar uniform setters */
#define DECL_H_U1(s, GL_T, ar_fn, glfn)                                        \
  void h_glUniform1##s(BridgeCtrl *C, uint8_t *D);
#define DECL_H_U2(s, GL_T, ar_fn, glfn)                                        \
  void h_glUniform2##s(BridgeCtrl *C, uint8_t *D);
#define DECL_H_U3(s, GL_T, ar_fn, glfn)                                        \
  void h_glUniform3##s(BridgeCtrl *C, uint8_t *D);
#define DECL_H_U4(s, GL_T, ar_fn, glfn)                                        \
  void h_glUniform4##s(BridgeCtrl *C, uint8_t *D);
#define DECL_H_UV(s, GL_T, comp, glfn)                                         \
  void h_glUniform##comp##s##v(BridgeCtrl *C, uint8_t *D);
DECL_H_U1(f, GLfloat, ar_f32, glUniform1f)
DECL_H_U1(i, GLint, ar_i32, glUniform1i)
DECL_H_U2(f, GLfloat, ar_f32, glUniform2f)
DECL_H_U2(i, GLint, ar_i32, glUniform2i)
DECL_H_U3(f, GLfloat, ar_f32, glUniform3f)
DECL_H_U3(i, GLint, ar_i32, glUniform3i)
DECL_H_U4(f, GLfloat, ar_f32, glUniform4f)
DECL_H_U4(i, GLint, ar_i32, glUniform4i)

DECL_H_UV(f, GLfloat, 1, glUniform1fv)
DECL_H_UV(i, GLint, 1, glUniform1iv)
DECL_H_UV(f, GLfloat, 2, glUniform2fv)
DECL_H_UV(i, GLint, 2, glUniform2iv)
DECL_H_UV(f, GLfloat, 3, glUniform3fv)
DECL_H_UV(i, GLint, 3, glUniform3iv)
DECL_H_UV(f, GLfloat, 4, glUniform4fv)
DECL_H_UV(i, GLint, 4, glUniform4iv)

#define DECL_H_UMAT(dim)                                                       \
  void h_glUniformMatrix##dim##fv(BridgeCtrl *C, uint8_t *D);
DECL_H_UMAT(2)
DECL_H_UMAT(3)
DECL_H_UMAT(4)

/* ── Attributes ───────────────────────────────────────────────────────────
 */
void h_glGetAttribLocation(BridgeCtrl *C, uint8_t *D);
void h_glGetActiveAttrib(BridgeCtrl *C, uint8_t *D);
void h_glBindAttribLocation(BridgeCtrl *C, uint8_t *D);
void h_glVertexAttribPointer(BridgeCtrl *C, uint8_t *D);
void h_glEnableVertexAttribArray(BridgeCtrl *C, uint8_t *D);
void h_glDisableVertexAttribArray(BridgeCtrl *C, uint8_t *D);
void h_glGetVertexAttribfv(BridgeCtrl *C, uint8_t *D);
void h_glGetVertexAttribiv(BridgeCtrl *C, uint8_t *D);
void h_glGetVertexAttribPointerv(BridgeCtrl *C, uint8_t *D);

/* VertexAttrib constant setters */
#define H_VA1(s, GL_T, ar_fn, fn)                                              \
  void h_glVertexAttrib1##s(BridgeCtrl *C, uint8_t *D);
#define H_VAFV(comp, fn)                                                       \
  void h_glVertexAttrib##comp##fv(BridgeCtrl *C, uint8_t *D);

H_VA1(f, GLfloat, ar_f32, glVertexAttrib1f)
H_VAFV(1, glVertexAttrib1fv)
void h_glVertexAttrib2f(BridgeCtrl *C, uint8_t *D);
H_VAFV(2, glVertexAttrib2fv)
void h_glVertexAttrib3f(BridgeCtrl *C, uint8_t *D);
H_VAFV(3, glVertexAttrib3fv)
void h_glVertexAttrib4f(BridgeCtrl *C, uint8_t *D);
H_VAFV(4, glVertexAttrib4fv)

/* ── Draw ────────────────────────────────────────────────────────────────── */
void h_glDrawArrays(BridgeCtrl *C, uint8_t *D);
void h_glDrawElements(BridgeCtrl *C, uint8_t *D);

/* ── Rasterisation state ─────────────────────────────────────────────────── */
void h_glViewport(BridgeCtrl *C, uint8_t *D);
void h_glScissor(BridgeCtrl *C, uint8_t *D);
void h_glEnable(BridgeCtrl *C, uint8_t *D);
void h_glDisable(BridgeCtrl *C, uint8_t *D);
void h_glIsEnabled(BridgeCtrl *C, uint8_t *D);
void h_glCullFace(BridgeCtrl *C, uint8_t *D);
void h_glFrontFace(BridgeCtrl *C, uint8_t *D);
void h_glLineWidth(BridgeCtrl *C, uint8_t *D);
void h_glPolygonOffset(BridgeCtrl *C, uint8_t *D);
void h_glSampleCoverage(BridgeCtrl *C, uint8_t *D);
void h_glHint(BridgeCtrl *C, uint8_t *D);

/* ── Blend / Depth / Stencil / Clear ────────────────────────────────────── */
void h_glBlendColor(BridgeCtrl *C, uint8_t *D);
void h_glBlendEquation(BridgeCtrl *C, uint8_t *D);
void h_glBlendEquationSeparate(BridgeCtrl *C, uint8_t *D);
void h_glBlendFunc(BridgeCtrl *C, uint8_t *D);
void h_glBlendFuncSeparate(BridgeCtrl *C, uint8_t *D);
void h_glDepthFunc(BridgeCtrl *C, uint8_t *D);
void h_glDepthMask(BridgeCtrl *C, uint8_t *D);
void h_glDepthRangef(BridgeCtrl *C, uint8_t *D);
void h_glColorMask(BridgeCtrl *C, uint8_t *D);
void h_glStencilFunc(BridgeCtrl *C, uint8_t *D);
void h_glStencilFuncSeparate(BridgeCtrl *C, uint8_t *D);
void h_glStencilMask(BridgeCtrl *C, uint8_t *D);
void h_glStencilMaskSeparate(BridgeCtrl *C, uint8_t *D);
void h_glStencilOp(BridgeCtrl *C, uint8_t *D);
void h_glStencilOpSeparate(BridgeCtrl *C, uint8_t *D);
void h_glClear(BridgeCtrl *C, uint8_t *D);
void h_glClearColor(BridgeCtrl *C, uint8_t *D);
void h_glClearDepthf(BridgeCtrl *C, uint8_t *D);
void h_glClearStencil(BridgeCtrl *C, uint8_t *D);

/* ── Query ───────────────────────────────────────────────────────────────── */
void h_glGetError(BridgeCtrl *C, uint8_t *D);
void h_glGetBooleanv(BridgeCtrl *C, uint8_t *D);
void h_glGetFloatv(BridgeCtrl *C, uint8_t *D);
void h_glGetIntegerv(BridgeCtrl *C, uint8_t *D);
void h_glGetString(BridgeCtrl *C, uint8_t *D);
void h_glReadPixels(BridgeCtrl *C, uint8_t *D);

/* ── Misc ────────────────────────────────────────────────────────────────── */
void h_glFinish(BridgeCtrl *C, uint8_t *D);
void h_glFlush(BridgeCtrl *C, uint8_t *D);
void h_eglGetError(BridgeCtrl *C, uint8_t *D);
void h_glIsBuffer(BridgeCtrl *C, uint8_t *D);
void h_glIsFramebuffer(BridgeCtrl *C, uint8_t *D);
void h_glIsProgram(BridgeCtrl *C, uint8_t *D);
void h_glIsRenderbuffer(BridgeCtrl *C, uint8_t *D);
void h_glIsShader(BridgeCtrl *C, uint8_t *D);
void h_glIsTexture(BridgeCtrl *C, uint8_t *D);
