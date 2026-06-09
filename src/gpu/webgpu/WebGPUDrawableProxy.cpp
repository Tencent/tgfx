/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "WebGPUDrawableProxy.h"
#include <cstdio>
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/webgpu/WebGPUTypes.h"

namespace tgfx {

WebGPUDrawableProxy::WebGPUDrawableProxy(Context* context, int width, int height,
                                         WGPUSurface surface, WGPUTextureFormat format,
                                         PixelFormat pixelFormat)
    : _context(context), _width(width), _height(height), _surface(surface), _wgpuFormat(format),
      _pixelFormat(pixelFormat) {
}

Context* WebGPUDrawableProxy::getContext() const {
  return _context;
}

int WebGPUDrawableProxy::width() const {
  return _width;
}

int WebGPUDrawableProxy::height() const {
  return _height;
}

PixelFormat WebGPUDrawableProxy::format() const {
  return _pixelFormat;
}

int WebGPUDrawableProxy::sampleCount() const {
  return 1;
}

ImageOrigin WebGPUDrawableProxy::origin() const {
  return ImageOrigin::TopLeft;
}

bool WebGPUDrawableProxy::externallyOwned() const {
  return true;
}

std::shared_ptr<TextureView> WebGPUDrawableProxy::getTextureView() const {
  return nullptr;
}

std::shared_ptr<RenderTarget> WebGPUDrawableProxy::getRenderTarget() const {
  if (_renderTarget == nullptr) {
    WGPUSurfaceTexture surfaceTexture = {};
    wgpuSurfaceGetCurrentTexture(_surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_Success ||
        surfaceTexture.texture == nullptr) {
      return nullptr;
    }
    _surfaceTexture = surfaceTexture.texture;
    _surfaceTextureView = wgpuTextureCreateView(_surfaceTexture, nullptr);
    if (_surfaceTextureView == nullptr) {
      return nullptr;
    }
    WebGPUTextureInfo textureInfo = {};
    textureInfo.texture = _surfaceTexture;
    textureInfo.textureView = _surfaceTextureView;
    textureInfo.format = static_cast<uint32_t>(_wgpuFormat);
    auto textureWidth = static_cast<int>(wgpuTextureGetWidth(_surfaceTexture));
    auto textureHeight = static_cast<int>(wgpuTextureGetHeight(_surfaceTexture));
    BackendRenderTarget backendRT(textureInfo, textureWidth, textureHeight);
    _renderTarget = RenderTarget::MakeFrom(_context, backendRT, ImageOrigin::TopLeft);
  }
  return _renderTarget;
}

void WebGPUDrawableProxy::present() {
#ifndef __EMSCRIPTEN__
  if (_surface != nullptr) {
    wgpuSurfacePresent(_surface);
  }
#endif
}

void WebGPUDrawableProxy::releaseDrawable() {
  if (_surfaceTextureView != nullptr) {
    wgpuTextureViewRelease(_surfaceTextureView);
    _surfaceTextureView = nullptr;
  }
  if (_surfaceTexture != nullptr) {
    wgpuTextureRelease(_surfaceTexture);
    _surfaceTexture = nullptr;
  }
  _renderTarget = nullptr;
}

}  // namespace tgfx
