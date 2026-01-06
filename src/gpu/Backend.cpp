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
#include "opengl/GLDefines.h"

namespace tgfx {
static PixelFormat GLSizeFormatToPixelFormat(unsigned sizeFormat) {
  switch (sizeFormat) {
    case GL_BGRA:
    case GL_BGRA8:
      return PixelFormat::BGRA_8888;
    case GL_R8:
    case GL_RED:
    case GL_ALPHA8:
    case GL_ALPHA:
      return PixelFormat::ALPHA_8;
    case GL_LUMINANCE8:
    case GL_LUMINANCE:
      return PixelFormat::GRAY_8;
    case GL_RG8:
    case GL_RG:
      return PixelFormat::RG_88;
    default:
      break;
  }
  return PixelFormat::RGBA_8888;
}

// WebGPU texture format values from wgpu::TextureFormat enum
static constexpr unsigned WEBGPU_FORMAT_R8_UNORM = 0x00000001;
static constexpr unsigned WEBGPU_FORMAT_RG8_UNORM = 0x00000008;
static constexpr unsigned WEBGPU_FORMAT_RGBA8_UNORM = 0x00000012;
static constexpr unsigned WEBGPU_FORMAT_BGRA8_UNORM = 0x00000017;

static PixelFormat WebGPUFormatToPixelFormat(unsigned format) {
  switch (format) {
    case WEBGPU_FORMAT_R8_UNORM:
      return PixelFormat::ALPHA_8;
    case WEBGPU_FORMAT_RG8_UNORM:
      return PixelFormat::RG_88;
    case WEBGPU_FORMAT_RGBA8_UNORM:
      return PixelFormat::RGBA_8888;
    case WEBGPU_FORMAT_BGRA8_UNORM:
      return PixelFormat::BGRA_8888;
    default:
      break;
  }
  return PixelFormat::Unknown;
}

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
    case Backend::WebGPU:
      webGPUInfo = that.webGPUInfo;
      break;
    default:
      break;
  }
  return *this;
}

PixelFormat BackendTexture::format() const {
  if (!isValid()) {
    return PixelFormat::Unknown;
  }
  switch (_backend) {
    case Backend::OpenGL:
      return GLSizeFormatToPixelFormat(glInfo.format);
    case Backend::Metal:
      //TODO: Add Metal format mapping.
      return PixelFormat::RGBA_8888;
    case Backend::WebGPU:
      return WebGPUFormatToPixelFormat(webGPUInfo.format);
    default:
      break;
  }
  return PixelFormat::Unknown;
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

bool BackendTexture::getWebGPUTextureInfo(WebGPUTextureInfo* webGPUTextureInfo) const {
  if (!isValid() || _backend != Backend::WebGPU) {
    return false;
  }
  *webGPUTextureInfo = webGPUInfo;
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
    case Backend::WebGPU:
      webGPUInfo = that.webGPUInfo;
      break;
    default:
      break;
  }
  return *this;
}

PixelFormat BackendRenderTarget::format() const {
  if (!isValid()) {
    return PixelFormat::Unknown;
  }
  switch (_backend) {
    case Backend::OpenGL:
      return GLSizeFormatToPixelFormat(glInfo.format);
    case Backend::Metal:
      //TODO: Add Metal format mapping.
      return PixelFormat::RGBA_8888;
    case Backend::WebGPU:
      return WebGPUFormatToPixelFormat(webGPUInfo.format);
    default:
      break;
  }
  return PixelFormat::Unknown;
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

bool BackendRenderTarget::getWebGPUTextureInfo(WebGPUTextureInfo* webGPUTextureInfo) const {
  if (!isValid() || _backend != Backend::WebGPU) {
    return false;
  }
  *webGPUTextureInfo = webGPUInfo;
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
