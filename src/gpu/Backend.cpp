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

#include "tgfx/gpu/Backend.h"

namespace tgfx {
BackendTexture& BackendTexture::operator=(const BackendTexture& that) {
  if (!that.isValid()) {
    _width = _height = 0;
    return *this;
  }
  _width = that._width;
  _height = that._height;
  _backend = that._backend;
  switch (that._backend) {
    case Backend::OpenGL:
      glInfo = that.glInfo;
      break;
    case Backend::Metal:
      mtlInfo = that.mtlInfo;
      break;
    default:
      break;
  }
  return *this;
}

bool BackendTexture::getGLTextureInfo(GLTextureInfo* glTextureInfo) const {
  if (!isValid() || _backend != Backend::OpenGL) {
    return false;
  }
  *glTextureInfo = glInfo;
  return true;
}

bool BackendTexture::getMtlTextureInfo(MtlTextureInfo* mtlTextureInfo) const {
  if (!isValid() || _backend != Backend::Metal) {
    return false;
  }
  *mtlTextureInfo = mtlInfo;
  return true;
}

BackendRenderTarget& BackendRenderTarget::operator=(const BackendRenderTarget& that) {
  if (!that.isValid()) {
    _width = _height = 0;
    return *this;
  }
  _width = that._width;
  _height = that._height;
  _backend = that._backend;
  switch (that._backend) {
    case Backend::OpenGL:
      glInfo = that.glInfo;
      break;
    case Backend::Metal:
      mtlInfo = that.mtlInfo;
      break;
    default:
      break;
  }
  return *this;
}

bool BackendRenderTarget::getGLFramebufferInfo(GLFrameBufferInfo* glFrameBufferInfo) const {
  if (!isValid() || _backend != Backend::OpenGL) {
    return false;
  }
  *glFrameBufferInfo = glInfo;
  return true;
}

bool BackendRenderTarget::getMtlTextureInfo(MtlTextureInfo* mtlTextureInfo) const {
  if (!isValid() || _backend != Backend::Metal) {
    return false;
  }
  *mtlTextureInfo = mtlInfo;
  return true;
}

BackendSemaphore& BackendSemaphore::operator=(const BackendSemaphore& that) {
  _backend = that._backend;
  switch (that._backend) {
    case Backend::OpenGL:
      glSyncInfo = that.glSyncInfo;
      break;
    default:
      break;
  }
  return *this;
}

bool BackendSemaphore::isInitialized() const {
  switch (_backend) {
    case Backend::OpenGL:
      return glSyncInfo.sync != nullptr;
    default:
      break;
  }
  return false;
}

bool BackendSemaphore::getGLSync(GLSyncInfo* syncInfo) const {
  if (_backend != Backend::OpenGL || glSyncInfo.sync == nullptr) {
    return false;
  }
  *syncInfo = glSyncInfo;
  return true;
}
}  // namespace tgfx