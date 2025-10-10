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

#define GL_CORE_FUNCTIONS_EACH(M)          \
  M(glActiveTexture)                       \
  M(glAttachShader)                        \
  M(glBindAttribLocation)                  \
  M(glBindBuffer)                          \
  M(glBindFramebuffer)                     \
  M(glBindRenderbuffer)                    \
  M(glBindTexture)                         \
  M(glBindVertexArray)                     \
  M(glBlendColor)                          \
  M(glBlendEquation)                       \
  M(glBlendEquationSeparate)               \
  M(glBlendFunc)                           \
  M(glBlendFuncSeparate)                   \
  M(glBlitFramebuffer)                     \
  M(glBufferData)                          \
  M(glBufferSubData)                       \
  M(glCheckFramebufferStatus)              \
  M(glClear)                               \
  M(glClearColor)                          \
  M(glClearDepthf)                         \
  M(glClearStencil)                        \
  M(glColorMask)                           \
  M(glCompileShader)                       \
  M(glCompressedTexImage2D)                \
  M(glCompressedTexSubImage2D)             \
  M(glCopyTexSubImage2D)                   \
  M(glCreateProgram)                       \
  M(glCreateShader)                        \
  M(glCullFace)                            \
  M(glDeleteBuffers)                       \
  M(glDeleteFramebuffers)                  \
  M(glDeleteProgram)                       \
  M(glDeleteRenderbuffers)                 \
  M(glDeleteShader)                        \
  M(glDeleteSync)                          \
  M(glDeleteTextures)                      \
  M(glDeleteVertexArrays)                  \
  M(glDepthFunc)                           \
  M(glDepthMask)                           \
  M(glDisable)                             \
  M(glDisableVertexAttribArray)            \
  M(glDrawArrays)                          \
  M(glDrawElements)                        \
  M(glEnable)                              \
  M(glIsEnabled)                           \
  M(glEnableVertexAttribArray)             \
  M(glFenceSync)                           \
  M(glFinish)                              \
  M(glFlush)                               \
  M(glFramebufferRenderbuffer)             \
  M(glFramebufferTexture2D)                \
  M(glFrontFace)                           \
  M(glGenBuffers)                          \
  M(glGenFramebuffers)                     \
  M(glGenRenderbuffers)                    \
  M(glGenTextures)                         \
  M(glGenVertexArrays)                     \
  M(glGenerateMipmap)                      \
  M(glGetBufferParameteriv)                \
  M(glGetError)                            \
  M(glGetFramebufferAttachmentParameteriv) \
  M(glGetIntegerv)                         \
  M(glGetInternalformativ)                 \
  M(glGetBooleanv)                         \
  M(glGetProgramInfoLog)                   \
  M(glGetProgramiv)                        \
  M(glGetRenderbufferParameteriv)          \
  M(glGetShaderInfoLog)                    \
  M(glGetShaderPrecisionFormat)            \
  M(glGetShaderiv)                         \
  M(glGetString)                           \
  M(glGetStringi)                          \
  M(glGetVertexAttribiv)                   \
  M(glGetVertexAttribPointerv)             \
  M(glGetAttribLocation)                   \
  M(glGetUniformLocation)                  \
  M(glGetUniformBlockIndex)                \
  M(glUniformBlockBinding)                 \
  M(glBindBufferBase)                      \
  M(glBindBufferRange)                     \
  M(glIsTexture)                           \
  M(glLineWidth)                           \
  M(glLinkProgram)                         \
  M(glPixelStorei)                         \
  M(glReadPixels)                          \
  M(glRenderbufferStorage)                 \
  M(glRenderbufferStorageMultisample)      \
  M(glScissor)                             \
  M(glShaderSource)                        \
  M(glStencilFunc)                         \
  M(glStencilFuncSeparate)                 \
  M(glStencilMask)                         \
  M(glStencilMaskSeparate)                 \
  M(glStencilOp)                           \
  M(glStencilOpSeparate)                   \
  M(glTexImage2D)                          \
  M(glTexParameterf)                       \
  M(glTexParameterfv)                      \
  M(glTexParameteri)                       \
  M(glTexParameteriv)                      \
  M(glTexSubImage2D)                       \
  M(glUniform1f)                           \
  M(glUniform1fv)                          \
  M(glUniform1i)                           \
  M(glUniform1iv)                          \
  M(glUniform2f)                           \
  M(glUniform2fv)                          \
  M(glUniform2i)                           \
  M(glUniform2iv)                          \
  M(glUniform3f)                           \
  M(glUniform3fv)                          \
  M(glUniform3i)                           \
  M(glUniform3iv)                          \
  M(glUniform4f)                           \
  M(glUniform4fv)                          \
  M(glUniform4i)                           \
  M(glUniform4iv)                          \
  M(glUniformMatrix2fv)                    \
  M(glUniformMatrix3fv)                    \
  M(glUniformMatrix4fv)                    \
  M(glUseProgram)                          \
  M(glVertexAttrib1f)                      \
  M(glVertexAttrib2fv)                     \
  M(glVertexAttrib3fv)                     \
  M(glVertexAttrib4fv)                     \
  M(glVertexAttribPointer)                 \
  M(glViewport)                            \
  M(glWaitSync)