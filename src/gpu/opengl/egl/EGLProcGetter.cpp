/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "EGLProcGetter.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstring>

namespace tgfx {

static void* egl_get_gl_proc(void*, const char name[]) {
  // https://www.khronos.org/registry/EGL/extensions/KHR/EGL_KHR_get_all_proc_addresses.txt
  // eglGetProcAddress() is not guaranteed to support the querying of non-extension EGL functions.
#define M(X)                           \
  if (0 == strcmp(#X, name)) {         \
    return reinterpret_cast<void*>(X); \
  }
  M(eglGetCurrentDisplay)
  M(eglQueryString)
  M(glActiveTexture)
  M(glAttachShader)
  M(glBindAttribLocation)
  M(glBindBuffer)
  M(glBindFramebuffer)
  M(glBindRenderbuffer)
  M(glBindTexture)
  M(glBindVertexArray)
  M(glBlendColor)
  M(glBlendEquation)
  M(glBlendEquationSeparate)
  M(glBlendFunc)
  M(glBlendFuncSeparate)
  M(glBufferData)
  M(glBufferSubData)
  M(glCheckFramebufferStatus)
  M(glClear)
  M(glClearColor)
  M(glClearDepthf)
  M(glClearStencil)
  M(glColorMask)
  M(glCompileShader)
  M(glCompressedTexImage2D)
  M(glCompressedTexSubImage2D)
  M(glCopyTexSubImage2D)
  M(glCreateProgram)
  M(glCreateShader)
  M(glCullFace)
  M(glDeleteBuffers)
  M(glDeleteFramebuffers)
  M(glDeleteProgram)
  M(glDeleteRenderbuffers)
  M(glDeleteShader)
  M(glDeleteSync)
  M(glDeleteTextures)
  M(glDeleteVertexArrays)
  M(glDepthFunc)
  M(glDepthMask)
  M(glDisable)
  M(glDisableVertexAttribArray)
  M(glDrawArrays)
  M(glDrawElements)
  M(glEnable)
  M(glIsEnabled)
  M(glEnableVertexAttribArray)
  M(glFenceSync)
  M(glFinish)
  M(glFlush)
  M(glFramebufferRenderbuffer)
  M(glFramebufferTexture2D)
  M(glFrontFace)
  M(glGenBuffers)
  M(glGenFramebuffers)
  M(glGenRenderbuffers)
  M(glGenTextures)
  M(glGenVertexArrays)
  M(glGenerateMipmap)
  M(glGetBufferParameteriv)
  M(glGetError)
  M(glGetFramebufferAttachmentParameteriv)
  M(glGetIntegerv)
  M(glGetInternalformativ)
  M(glGetBooleanv)
  M(glGetProgramInfoLog)
  M(glGetProgramiv)
  M(glGetRenderbufferParameteriv)
  M(glGetShaderInfoLog)
  M(glGetShaderPrecisionFormat)
  M(glGetShaderiv)
  M(glGetString)
  M(glGetStringi)
  M(glGetVertexAttribiv)
  M(glGetVertexAttribPointerv)
  M(glGetAttribLocation)
  M(glGetUniformLocation)
  M(glIsTexture)
  M(glLineWidth)
  M(glLinkProgram)
  M(glPixelStorei)
  M(glReadPixels)
  M(glRenderbufferStorage)
  M(glBlitFramebuffer)
  M(glScissor)
  M(glShaderSource)
  M(glStencilFunc)
  M(glStencilFuncSeparate)
  M(glStencilMask)
  M(glStencilMaskSeparate)
  M(glStencilOp)
  M(glStencilOpSeparate)
  M(glTexImage2D)
  M(glTexParameterf)
  M(glTexParameterfv)
  M(glTexParameteri)
  M(glTexParameteriv)
  M(glTexSubImage2D)
  M(glUniform1f)
  M(glUniform1fv)
  M(glUniform1i)
  M(glUniform1iv)
  M(glUniform2f)
  M(glUniform2fv)
  M(glUniform2i)
  M(glUniform2iv)
  M(glUniform3f)
  M(glUniform3fv)
  M(glUniform3i)
  M(glUniform3iv)
  M(glUniform4f)
  M(glUniform4fv)
  M(glUniform4i)
  M(glUniform4iv)
  M(glUniformMatrix2fv)
  M(glUniformMatrix3fv)
  M(glUniformMatrix4fv)
  M(glUseProgram)
  M(glVertexAttrib1f)
  M(glVertexAttrib2fv)
  M(glVertexAttrib3fv)
  M(glVertexAttrib4fv)
  M(glVertexAttribPointer)
  M(glViewport)
  M(glWaitSync)
#undef M
  auto fun = eglGetProcAddress(name);
  return reinterpret_cast<void*>(fun);
}

void* EGLProcGetter::getProcAddress(const char* name) const {
  return egl_get_gl_proc(nullptr, name);
}

std::unique_ptr<GLProcGetter> GLProcGetter::Make() {
  return std::make_unique<EGLProcGetter>();
}
}  // namespace tgfx