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

#include "MetalDrawableProxy.h"
#import <Metal/Metal.h>
#include "gpu/metal/MetalCommandQueue.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/metal/MetalTypes.h"

namespace tgfx {
MetalDrawableProxy::MetalDrawableProxy(Context* context, int width, int height,
                                       CAMetalLayer* metalLayer, PixelFormat format,
                                       int sampleCount)
    : _context(context),
      _width(width),
      _height(height),
      _format(format),
      _sampleCount(sampleCount),
      _metalLayer(metalLayer) {
}

Context* MetalDrawableProxy::getContext() const {
  return _context;
}

int MetalDrawableProxy::width() const {
  return _width;
}

int MetalDrawableProxy::height() const {
  return _height;
}

PixelFormat MetalDrawableProxy::format() const {
  return _format;
}

int MetalDrawableProxy::sampleCount() const {
  return _sampleCount;
}

ImageOrigin MetalDrawableProxy::origin() const {
  return ImageOrigin::TopLeft;
}

bool MetalDrawableProxy::externallyOwned() const {
  return true;
}

std::shared_ptr<TextureView> MetalDrawableProxy::getTextureView() const {
  return nullptr;
}

std::shared_ptr<RenderTarget> MetalDrawableProxy::getRenderTarget() const {
  if (_renderTarget == nullptr) {
    _metalDrawable = [_metalLayer nextDrawable];
    if (_metalDrawable == nil) {
      return nullptr;
    }
    // Schedule the drawable to be presented when the command buffer is committed, so that the
    // GPU finishes rendering before the drawable is displayed on screen.
    auto metalQueue = static_cast<MetalCommandQueue*>(_context->gpu()->queue());
    metalQueue->schedulePresent(_metalDrawable);
    auto textureWidth = static_cast<int>(_metalDrawable.texture.width);
    auto textureHeight = static_cast<int>(_metalDrawable.texture.height);
    MetalTextureInfo metalInfo = {};
    metalInfo.texture = (__bridge const void*)_metalDrawable.texture;
    metalInfo.format = static_cast<unsigned>(_metalDrawable.texture.pixelFormat);
    if (_sampleCount <= 1) {
      BackendRenderTarget backendRT(metalInfo, textureWidth, textureHeight);
      _renderTarget = RenderTarget::MakeFrom(_context, backendRT, ImageOrigin::TopLeft);
    } else {
      BackendTexture backendTexture(metalInfo, textureWidth, textureHeight);
      _renderTarget =
          RenderTarget::MakeFrom(_context, backendTexture, _sampleCount, ImageOrigin::TopLeft);
    }
  }
  return _renderTarget;
}

id<CAMetalDrawable> MetalDrawableProxy::getMetalDrawable() const {
  return _metalDrawable;
}

void MetalDrawableProxy::releaseDrawable() {
  _metalDrawable = nil;
  _renderTarget = nullptr;
}
}  // namespace tgfx
