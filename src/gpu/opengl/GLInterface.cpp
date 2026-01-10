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

#include "GLInterface.h"
#include <mutex>
#include <unordered_map>
#include "GLUtil.h"

namespace tgfx {
static std::mutex interfaceLocker = {};
static std::unordered_map<int, std::shared_ptr<GLInterface>> glInterfaceMap = {};

static int GetGLVersion(const GLProcGetter* getter) {
  auto glGetString = reinterpret_cast<GLGetString*>(getter->getProcAddress("glGetString"));
  if (glGetString == nullptr) {
    return -1;
  }
  auto versionString = (const char*)glGetString(GL_VERSION);
  return GetGLVersion(versionString).majorVersion;
}

std::shared_ptr<GLInterface> GLInterface::GetNative() {
  auto getter = GLProcGetter::Make();
  if (getter == nullptr) {
    return nullptr;
  }
  auto version = GetGLVersion(getter.get());
  if (version <= 0) {
    return nullptr;
  }
  std::lock_guard<std::mutex> autoLock(interfaceLocker);
  auto result = glInterfaceMap.find(version);
  if (result != glInterfaceMap.end()) {
    return result->second;
  }
  auto interface = MakeNativeInterface(getter.get());
  glInterfaceMap[version] = interface;
  return interface;
}

std::shared_ptr<GLInterface> GLInterface::MakeNativeInterface(const GLProcGetter* getter) {
  auto getString = reinterpret_cast<GLGetString*>(getter->getProcAddress("glGetString"));
  if (getString == nullptr) {
    return nullptr;
  }
  auto getIntegerv = reinterpret_cast<GLGetIntegerv*>(getter->getProcAddress("glGetIntegerv"));
  if (getIntegerv == nullptr) {
    return nullptr;
  }
  auto getShaderPrecisionFormat = reinterpret_cast<GLGetShaderPrecisionFormat*>(
      getter->getProcAddress("glGetShaderPrecisionFormat"));
  auto getStringi = reinterpret_cast<GLGetStringi*>(getter->getProcAddress("glGetStringi"));
  auto getInternalformativ =
      reinterpret_cast<GLGetInternalformativ*>(getter->getProcAddress("glGetInternalformativ"));
  GLInfo info(getString, getStringi, getIntegerv, getInternalformativ, getShaderPrecisionFormat);
  auto functions = std::make_unique<GLFunctions>();
  functions->activeTexture =
      reinterpret_cast<GLActiveTexture*>(getter->getProcAddress("glActiveTexture"));
  functions->attachShader =
      reinterpret_cast<GLAttachShader*>(getter->getProcAddress("glAttachShader"));
  functions->bindBuffer = reinterpret_cast<GLBindBuffer*>(getter->getProcAddress("glBindBuffer"));
  functions->bindFramebuffer =
      reinterpret_cast<GLBindFramebuffer*>(getter->getProcAddress("glBindFramebuffer"));
  functions->bindRenderbuffer =
      reinterpret_cast<GLBindRenderbuffer*>(getter->getProcAddress("glBindRenderbuffer"));
  functions->bindTexture =
      reinterpret_cast<GLBindTexture*>(getter->getProcAddress("glBindTexture"));
  functions->bindVertexArray =
      reinterpret_cast<GLBindVertexArray*>(getter->getProcAddress("glBindVertexArray"));
  functions->blendEquation =
      reinterpret_cast<GLBlendEquation*>(getter->getProcAddress("glBlendEquation"));
  functions->blendEquationSeparate =
      reinterpret_cast<GLBlendEquationSeparate*>(getter->getProcAddress("glBlendEquationSeparate"));
  functions->blendFunc = reinterpret_cast<GLBlendFunc*>(getter->getProcAddress("glBlendFunc"));
  functions->blendFuncSeparate =
      reinterpret_cast<GLBlendFuncSeparate*>(getter->getProcAddress("glBlendFuncSeparate"));
  functions->bufferData = reinterpret_cast<GLBufferData*>(getter->getProcAddress("glBufferData"));
  functions->bufferSubData =
      reinterpret_cast<GLBufferSubData*>(getter->getProcAddress("glBufferSubData"));
  functions->checkFramebufferStatus = reinterpret_cast<GLCheckFramebufferStatus*>(
      getter->getProcAddress("glCheckFramebufferStatus"));
  functions->clear = reinterpret_cast<GLClear*>(getter->getProcAddress("glClear"));
  functions->clearColor = reinterpret_cast<GLClearColor*>(getter->getProcAddress("glClearColor"));
  functions->clearDepthf =
      reinterpret_cast<GLClearDepthf*>(getter->getProcAddress("glClearDepthf"));
  functions->clearStencil =
      reinterpret_cast<GLClearStencil*>(getter->getProcAddress("glClearStencil"));
  functions->colorMask = reinterpret_cast<GLColorMask*>(getter->getProcAddress("glColorMask"));
  functions->compileShader =
      reinterpret_cast<GLCompileShader*>(getter->getProcAddress("glCompileShader"));
  functions->copyTexSubImage2D =
      reinterpret_cast<GLCopyTexSubImage2D*>(getter->getProcAddress("glCopyTexSubImage2D"));
  functions->createProgram =
      reinterpret_cast<GLCreateProgram*>(getter->getProcAddress("glCreateProgram"));
  functions->createShader =
      reinterpret_cast<GLCreateShader*>(getter->getProcAddress("glCreateShader"));
  functions->deleteBuffers =
      reinterpret_cast<GLDeleteBuffers*>(getter->getProcAddress("glDeleteBuffers"));
  functions->deleteFramebuffers =
      reinterpret_cast<GLDeleteFramebuffers*>(getter->getProcAddress("glDeleteFramebuffers"));
  functions->deleteProgram =
      reinterpret_cast<GLDeleteProgram*>(getter->getProcAddress("glDeleteProgram"));
  functions->deleteRenderbuffers =
      reinterpret_cast<GLDeleteRenderbuffers*>(getter->getProcAddress("glDeleteRenderbuffers"));
  functions->deleteShader =
      reinterpret_cast<GLDeleteShader*>(getter->getProcAddress("glDeleteShader"));
  functions->deleteSync = reinterpret_cast<GLDeleteSync*>(getter->getProcAddress("glDeleteSync"));
  functions->deleteTextures =
      reinterpret_cast<GLDeleteTextures*>(getter->getProcAddress("glDeleteTextures"));
  functions->deleteVertexArrays =
      reinterpret_cast<GLDeleteVertexArrays*>(getter->getProcAddress("glDeleteVertexArrays"));
  functions->depthFunc = reinterpret_cast<GLDepthFunc*>(getter->getProcAddress("glDepthFunc"));
  functions->depthMask = reinterpret_cast<GLDepthMask*>(getter->getProcAddress("glDepthMask"));
  functions->disable = reinterpret_cast<GLDisable*>(getter->getProcAddress("glDisable"));
  functions->drawArrays = reinterpret_cast<GLDrawArrays*>(getter->getProcAddress("glDrawArrays"));
  functions->drawElements =
      reinterpret_cast<GLDrawElements*>(getter->getProcAddress("glDrawElements"));
  functions->drawArraysInstanced =
      reinterpret_cast<GLDrawArraysInstanced*>(getter->getProcAddress("glDrawArraysInstanced"));
  functions->drawElementsInstanced =
      reinterpret_cast<GLDrawElementsInstanced*>(getter->getProcAddress("glDrawElementsInstanced"));
  functions->enable = reinterpret_cast<GLEnable*>(getter->getProcAddress("glEnable"));
  functions->enableVertexAttribArray = reinterpret_cast<GLEnableVertexAttribArray*>(
      getter->getProcAddress("glEnableVertexAttribArray"));
  functions->fenceSync = reinterpret_cast<GLFenceSync*>(getter->getProcAddress("glFenceSync"));
  functions->finish = reinterpret_cast<GLFinish*>(getter->getProcAddress("glFinish"));
  functions->flush = reinterpret_cast<GLFlush*>(getter->getProcAddress("glFlush"));
  functions->framebufferRenderbuffer = reinterpret_cast<GLFramebufferRenderbuffer*>(
      getter->getProcAddress("glFramebufferRenderbuffer"));
  functions->framebufferTexture2D =
      reinterpret_cast<GLFramebufferTexture2D*>(getter->getProcAddress("glFramebufferTexture2D"));
  functions->genBuffers = reinterpret_cast<GLGenBuffers*>(getter->getProcAddress("glGenBuffers"));
  functions->genFramebuffers =
      reinterpret_cast<GLGenFramebuffers*>(getter->getProcAddress("glGenFramebuffers"));
  functions->generateMipmap =
      reinterpret_cast<GLGenerateMipmap*>(getter->getProcAddress("glGenerateMipmap"));
  functions->genRenderbuffers =
      reinterpret_cast<GLGenRenderbuffers*>(getter->getProcAddress("glGenRenderbuffers"));
  functions->genTextures =
      reinterpret_cast<GLGenTextures*>(getter->getProcAddress("glGenTextures"));
  functions->genVertexArrays =
      reinterpret_cast<GLGenVertexArrays*>(getter->getProcAddress("glGenVertexArrays"));
  functions->getError = reinterpret_cast<GLGetError*>(getter->getProcAddress("glGetError"));
  functions->getIntegerv =
      reinterpret_cast<GLGetIntegerv*>(getter->getProcAddress("glGetIntegerv"));
  functions->getInternalformativ =
      reinterpret_cast<GLGetInternalformativ*>(getter->getProcAddress("glGetInternalformativ"));
  functions->getProgramInfoLog =
      reinterpret_cast<GLGetProgramInfoLog*>(getter->getProcAddress("glGetProgramInfoLog"));
  functions->getProgramiv =
      reinterpret_cast<GLGetProgramiv*>(getter->getProcAddress("glGetProgramiv"));
  functions->getShaderInfoLog =
      reinterpret_cast<GLGetShaderInfoLog*>(getter->getProcAddress("glGetShaderInfoLog"));
  functions->getShaderiv =
      reinterpret_cast<GLGetShaderiv*>(getter->getProcAddress("glGetShaderiv"));
  functions->getShaderPrecisionFormat = reinterpret_cast<GLGetShaderPrecisionFormat*>(
      getter->getProcAddress("glGetShaderPrecisionFormat"));
  functions->getString = reinterpret_cast<GLGetString*>(getter->getProcAddress("glGetString"));
  functions->getStringi = reinterpret_cast<GLGetStringi*>(getter->getProcAddress("glGetStringi"));
  functions->getAttribLocation =
      reinterpret_cast<GLGetAttribLocation*>(getter->getProcAddress("glGetAttribLocation"));
  functions->getUniformLocation =
      reinterpret_cast<GLGetUniformLocation*>(getter->getProcAddress("glGetUniformLocation"));
  functions->getUniformBlockIndex =
      reinterpret_cast<GLGetUniformBlockIndex*>(getter->getProcAddress("glGetUniformBlockIndex"));
  functions->uniformBlockBinding =
      reinterpret_cast<GLUniformBlockBinding*>(getter->getProcAddress("glUniformBlockBinding"));
  functions->bindBufferRange =
      reinterpret_cast<GLBindBufferRange*>(getter->getProcAddress("glBindBufferRange"));
  functions->mapBufferRange =
      reinterpret_cast<GLMapBufferRange*>(getter->getProcAddress("glMapBufferRange"));
  functions->unmapBuffer =
      reinterpret_cast<GLUnmapBuffer*>(getter->getProcAddress("glUnmapBuffer"));
  functions->linkProgram =
      reinterpret_cast<GLLinkProgram*>(getter->getProcAddress("glLinkProgram"));
  functions->pixelStorei =
      reinterpret_cast<GLPixelStorei*>(getter->getProcAddress("glPixelStorei"));
  functions->readPixels = reinterpret_cast<GLReadPixels*>(getter->getProcAddress("glReadPixels"));
  functions->renderbufferStorage =
      reinterpret_cast<GLRenderbufferStorage*>(getter->getProcAddress("glRenderbufferStorage"));
  functions->renderbufferStorageMultisample = reinterpret_cast<GLRenderbufferStorageMultisample*>(
      getter->getProcAddress("glRenderbufferStorageMultisample"));
  functions->blitFramebuffer =
      reinterpret_cast<GLBlitFramebuffer*>(getter->getProcAddress("glBlitFramebuffer"));
  functions->scissor = reinterpret_cast<GLScissor*>(getter->getProcAddress("glScissor"));
  functions->shaderSource =
      reinterpret_cast<GLShaderSource*>(getter->getProcAddress("glShaderSource"));
  functions->stencilFunc =
      reinterpret_cast<GLStencilFunc*>(getter->getProcAddress("glStencilFunc"));
  functions->stencilFuncSeparate =
      reinterpret_cast<GLStencilFuncSeparate*>(getter->getProcAddress("glStencilFuncSeparate"));
  functions->stencilMask =
      reinterpret_cast<GLStencilMask*>(getter->getProcAddress("glStencilMask"));
  functions->stencilMaskSeparate =
      reinterpret_cast<GLStencilMaskSeparate*>(getter->getProcAddress("glStencilMaskSeparate"));
  functions->stencilOp = reinterpret_cast<GLStencilOp*>(getter->getProcAddress("glStencilOp"));
  functions->stencilOpSeparate =
      reinterpret_cast<GLStencilOpSeparate*>(getter->getProcAddress("glStencilOpSeparate"));
  functions->texImage2D = reinterpret_cast<GLTexImage2D*>(getter->getProcAddress("glTexImage2D"));
  functions->texParameteri =
      reinterpret_cast<GLTexParameteri*>(getter->getProcAddress("glTexParameteri"));
  functions->texSubImage2D =
      reinterpret_cast<GLTexSubImage2D*>(getter->getProcAddress("glTexSubImage2D"));
  functions->uniform1i = reinterpret_cast<GLUniform1i*>(getter->getProcAddress("glUniform1i"));
  functions->useProgram = reinterpret_cast<GLUseProgram*>(getter->getProcAddress("glUseProgram"));
  functions->vertexAttribPointer =
      reinterpret_cast<GLVertexAttribPointer*>(getter->getProcAddress("glVertexAttribPointer"));
  functions->vertexAttribDivisor =
      reinterpret_cast<GLVertexAttribDivisor*>(getter->getProcAddress("glVertexAttribDivisor"));
  functions->viewport = reinterpret_cast<GLViewport*>(getter->getProcAddress("glViewport"));
  functions->clientWaitSync =
      reinterpret_cast<GLClientWaitSync*>(getter->getProcAddress("glClientWaitSync"));
  functions->waitSync = reinterpret_cast<GLWaitSync*>(getter->getProcAddress("glWaitSync"));

  switch (info.standard) {
    case GLStandard::GL:
      if (info.version >= GL_VER(4, 5) || info.hasExtension("GL_ARB_texture_barrier")) {
        functions->textureBarrier =
            reinterpret_cast<GLTextureBarrier*>(getter->getProcAddress("glTextureBarrier"));
      } else if (info.hasExtension("GL_NV_texture_barrier")) {
        functions->textureBarrier =
            reinterpret_cast<GLTextureBarrier*>(getter->getProcAddress("glTextureBarrierNV"));
      }
      break;
    case GLStandard::GLES:
      if (info.hasExtension("GL_NV_texture_barrier")) {
        functions->textureBarrier =
            reinterpret_cast<GLTextureBarrier*>(getter->getProcAddress("glTextureBarrierNV"));
      }
      break;
    case GLStandard::WebGL:
      // No special initialization required for WebGL.
      break;
    default:
      break;
  }
  auto caps = std::make_unique<GLCaps>(info);
  return std::shared_ptr<GLInterface>(new GLInterface(std::move(caps), std::move(functions)));
}
}  // namespace tgfx
