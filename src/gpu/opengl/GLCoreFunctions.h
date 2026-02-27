/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#pragma once

#define GL_CORE_FUNCTIONS_EACH(M)     \
  M(glActiveTexture)                  \
  M(glAttachShader)                   \
  M(glBindBuffer)                     \
  M(glBindFramebuffer)                \
  M(glBindRenderbuffer)               \
  M(glBindTexture)                    \
  M(glBindVertexArray)                \
  M(glBlendEquation)                  \
  M(glBlendEquationSeparate)          \
  M(glBlendFunc)                      \
  M(glBlendFuncSeparate)              \
  M(glBlitFramebuffer)                \
  M(glBufferData)                     \
  M(glBufferSubData)                  \
  M(glCheckFramebufferStatus)         \
  M(glClear)                          \
  M(glClearColor)                     \
  M(glClearDepthf)                    \
  M(glClearStencil)                   \
  M(glClientWaitSync)                 \
  M(glColorMask)                      \
  M(glCompileShader)                  \
  M(glCopyTexSubImage2D)              \
  M(glCreateProgram)                  \
  M(glCreateShader)                   \
  M(glDeleteBuffers)                  \
  M(glDeleteFramebuffers)             \
  M(glDeleteProgram)                  \
  M(glDeleteRenderbuffers)            \
  M(glDeleteShader)                   \
  M(glDeleteSync)                     \
  M(glDeleteTextures)                 \
  M(glDeleteVertexArrays)             \
  M(glDepthFunc)                      \
  M(glDepthMask)                      \
  M(glDisable)                        \
  M(glDrawArrays)                     \
  M(glDrawElements)                   \
  M(glDrawArraysInstanced)            \
  M(glDrawElementsInstanced)          \
  M(glEnable)                         \
  M(glEnableVertexAttribArray)        \
  M(glFenceSync)                      \
  M(glFinish)                         \
  M(glFlush)                          \
  M(glFramebufferRenderbuffer)        \
  M(glFramebufferTexture2D)           \
  M(glGenBuffers)                     \
  M(glGenFramebuffers)                \
  M(glGenRenderbuffers)               \
  M(glGenTextures)                    \
  M(glGenVertexArrays)                \
  M(glGenerateMipmap)                 \
  M(glGetError)                       \
  M(glGetIntegerv)                    \
  M(glGetInternalformativ)            \
  M(glGetProgramInfoLog)              \
  M(glGetProgramiv)                   \
  M(glGetShaderInfoLog)               \
  M(glGetShaderPrecisionFormat)       \
  M(glGetShaderiv)                    \
  M(glGetString)                      \
  M(glGetStringi)                     \
  M(glGetAttribLocation)              \
  M(glGetUniformLocation)             \
  M(glGetUniformBlockIndex)           \
  M(glUniformBlockBinding)            \
  M(glBindBufferRange)                \
  M(glLinkProgram)                    \
  M(glPixelStorei)                    \
  M(glReadPixels)                     \
  M(glRenderbufferStorage)            \
  M(glRenderbufferStorageMultisample) \
  M(glScissor)                        \
  M(glShaderSource)                   \
  M(glStencilFunc)                    \
  M(glStencilFuncSeparate)            \
  M(glStencilMask)                    \
  M(glStencilMaskSeparate)            \
  M(glStencilOp)                      \
  M(glStencilOpSeparate)              \
  M(glTexImage2D)                     \
  M(glTexParameteri)                  \
  M(glTexSubImage2D)                  \
  M(glUniform1i)                      \
  M(glUseProgram)                     \
  M(glVertexAttribPointer)            \
  M(glVertexAttribDivisor)            \
  M(glViewport)                       \
  M(glWaitSync)
