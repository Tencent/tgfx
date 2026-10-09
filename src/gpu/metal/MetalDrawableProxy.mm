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
#include "gpu/metal/MetalDrawable.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/metal/MetalTypes.h"

namespace tgfx {
MetalDrawableProxy::MetalDrawableProxy(Context* context, int width, int height,
                                       CAMetalLayer* metalLayer, PixelFormat format)
    : _context(context), _width(width), _height(height), _format(format), _metalLayer(metalLayer) {
}

MetalDrawableProxy::~MetalDrawableProxy() {
  [_metalDrawable release];
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
  return 1;
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
    // Frame acquisition and texture wrapping are shared with MetalDrawable (the explicit
    // single-frame path) so the acquire/render/present mechanism exists exactly once. The
    // presentation is scheduled by MetalWindow::onSchedulePresentation() at submission time,
    // together with the explicit path.
    auto drawable = MetalDrawable::AcquireMetalDrawable(_metalLayer);
    if (drawable == nil) {
      return nullptr;
    }
    [_metalDrawable release];
    _metalDrawable = drawable;
    _renderTarget = RenderTarget::MakeFrom(
        _context, MetalDrawable::MakeBackendRenderTarget(_metalDrawable), ImageOrigin::TopLeft);
    if (_renderTarget == nullptr) {
      // The render target creation failed; do not schedule a present for this unusable frame.
      // A later call retries with a fresh drawable.
      return nullptr;
    }
  }
  return _renderTarget;
}

id<CAMetalDrawable> MetalDrawableProxy::getMetalDrawable() const {
  return _metalDrawable;
}

void MetalDrawableProxy::releaseDrawable() {
  [_metalDrawable release];
  _metalDrawable = nil;
  _renderTarget = nullptr;
}

}  // namespace tgfx
