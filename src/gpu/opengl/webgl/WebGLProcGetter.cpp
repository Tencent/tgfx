/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "WebGLProcGetter.h"
#include <webgl/webgl1.h>
#include <webgl/webgl2.h>
#include <cstring>

namespace tgfx {
static void emscripten_glWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
  auto timeoutLo = static_cast<uint32_t>(timeout);
  uint32_t timeoutHi = timeout >> 32;
  emscripten_glWaitSync(sync, flags, timeoutLo, timeoutHi);
}

void* WebGLProcGetter::getProcAddress(const char* name) const {
#define N(X)                                        \
  if (0 == strcmp(#X, name)) {                      \
    return reinterpret_cast<void*>(emscripten_##X); \
  }
  N(glActiveTexture)
  N(glAttachShader)
  N(glIsEnabled)
  N(glBindAttribLocation)
  N(glBindBuffer)
  N(glBindFramebuffer)
  N(glBindRenderbuffer)
  N(glBindTexture)
  N(glBlendColor)
  N(glBlendEquation)
  N(glBlendFunc)
  N(glBlendFuncSeparate)
  N(glBufferData)
  N(glBufferSubData)
  N(glCheckFramebufferStatus)
  N(glClear)
  N(glClearColor)
  N(glClearDepthf)
  N(glClearStencil)
  N(glColorMask)
  N(glCompileShader)
  N(glCompressedTexImage2D)
  N(glCompressedTexSubImage2D)
  N(glCopyTexSubImage2D)
  N(glCreateProgram)
  N(glCreateShader)
  N(glCullFace)
  N(glDeleteBuffers)
  N(glDeleteFramebuffers)
  N(glDeleteProgram)
  N(glDeleteRenderbuffers)
  N(glDeleteShader)
  N(glDeleteTextures)
  N(glDeleteVertexArrays)
  N(glDepthFunc)
  N(glDepthMask)
  N(glDisable)
  N(glDisableVertexAttribArray)
  N(glDrawArrays)
  N(glDrawElements)
  N(glEnable)
  N(glEnableVertexAttribArray)
  N(glFinish)
  N(glFlush)
  N(glFramebufferRenderbuffer)
  N(glFramebufferTexture2D)
  N(glFrontFace)
  N(glGenBuffers)
  N(glGenFramebuffers)
  N(glGenerateMipmap)
  N(glGenRenderbuffers)
  N(glGenTextures)
  N(glGetBufferParameteriv)
  N(glGetError)
  N(glGetFramebufferAttachmentParameteriv)
  N(glGetIntegerv)
  N(glGetInternalformativ)
  N(glGetBooleanv)
  N(glGetProgramInfoLog)
  N(glGetProgramiv)
  N(glGetRenderbufferParameteriv)
  N(glGetShaderInfoLog)
  N(glGetShaderPrecisionFormat)
  N(glGetShaderiv)
  N(glGetString)
  N(glGetStringi)
  N(glGetVertexAttribiv)
  N(glGetVertexAttribPointerv)
  N(glGetUniformLocation)
  N(glIsTexture)
  N(glLineWidth)
  N(glLinkProgram)
  N(glPixelStorei)
  N(glReadPixels)
  N(glRenderbufferStorage)
  N(glScissor)
  N(glShaderSource)
  N(glStencilFunc)
  N(glStencilFuncSeparate)
  N(glStencilMask)
  N(glStencilMaskSeparate)
  N(glStencilOp)
  N(glStencilOpSeparate)
  N(glTexImage2D)
  N(glTexParameterf)
  N(glTexParameterfv)
  N(glTexParameteri)
  N(glTexParameteriv)
  N(glTexSubImage2D)
  N(glUniform1f)
  N(glUniform1fv)
  N(glUniform1i)
  N(glUniform1iv)
  N(glUniform2f)
  N(glUniform2fv)
  N(glUniform2i)
  N(glUniform2iv)
  N(glUniform3f)
  N(glUniform3fv)
  N(glUniform3i)
  N(glUniform3iv)
  N(glUniform4f)
  N(glUniform4fv)
  N(glUniform4i)
  N(glUniform4iv)
  N(glUniformMatrix2fv)
  N(glUniformMatrix3fv)
  N(glUniformMatrix4fv)
  N(glUseProgram)
  N(glVertexAttrib1f)
  N(glVertexAttrib2fv)
  N(glVertexAttrib3fv)
  N(glVertexAttrib4fv)
  N(glVertexAttribPointer)
  N(glViewport)
  N(glGetAttribLocation)
  N(glBlendEquationSeparate)
  N(glBindVertexArray)
  N(glDeleteVertexArrays)
  N(glGenVertexArrays)
  N(glBindVertexArrayOES)
  N(glDeleteVertexArraysOES)
  N(glGenVertexArraysOES)
  N(glFenceSync)
  N(glWaitSync)
  N(glDeleteSync)
  N(glBlitFramebuffer)
  N(glRenderbufferStorageMultisample)
#undef N

  // We explicitly do not use GetProcAddress or something similar because its code size is quite
  // large. We shouldn't need GetProcAddress because emscripten provides us all the valid function
  // pointers for WebGL via the included headers.
  // https://github.com/emscripten-core/emscripten/blob/7ba7700902c46734987585409502f3c63beb650f/system/include/emscripten/html5_webgl.h#L93
  return nullptr;
}

std::unique_ptr<GLProcGetter> GLProcGetter::Make() {
  return std::make_unique<WebGLProcGetter>();
}
}  // namespace tgfx
