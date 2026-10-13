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
// Acquires the next drawable of the layer and returns it retained (+1); the caller takes over
// the reference. Returns nil if the layer cannot provide a drawable right now.
static id<CAMetalDrawable> AcquireMetalDrawable(CAMetalLayer* metalLayer) {
  if (metalLayer == nil) {
    return nil;
  }
  id<CAMetalDrawable> metalDrawable = nil;
  @autoreleasepool {
    // nextDrawable returns an autoreleased (+0) drawable; retain it inside the pool so the
    // reference stays valid after the pool drains.
    metalDrawable = [[metalLayer nextDrawable] retain];
  }
  return metalDrawable;
}

static BackendRenderTarget MakeBackendRenderTarget(id<CAMetalDrawable> drawable) {
  MetalTextureInfo metalInfo = {};
  metalInfo.texture = (__bridge const void*)drawable.texture;
  metalInfo.format = static_cast<unsigned>(drawable.texture.pixelFormat);
  return BackendRenderTarget(metalInfo, static_cast<int>(drawable.texture.width),
                             static_cast<int>(drawable.texture.height));
}

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
    // The drawable is acquired lazily, when the batch that draws the frame is encoded. Its
    // presentation is scheduled by onSchedulePresentation() at submission time.
    auto drawable = AcquireMetalDrawable(_metalLayer);
    if (drawable == nil) {
      return nullptr;
    }
    [_metalDrawable release];
    _metalDrawable = drawable;
    _renderTarget = RenderTarget::MakeFrom(_context, MakeBackendRenderTarget(_metalDrawable),
                                           ImageOrigin::TopLeft);
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

void MetalDrawableProxy::onSchedulePresentation(Context* context) {
  // Only schedule drawables whose render target was created successfully; scheduling a
  // drawable from a failed getRenderTarget() would present an unrendered frame.
  if (!hasRenderTarget()) {
    return;
  }
  auto drawable = getMetalDrawable();
  if (drawable == nil) {
    return;
  }
  // Attach the presentation to the upcoming command buffer, so that the presentation waits
  // for the GPU to finish rendering before the frame is displayed. The command buffer
  // retains the drawable until it completes, so the proxy can drop its reference once the
  // presentation has been scheduled (see onPresentFrame()).
  auto metalQueue = static_cast<MetalCommandQueue*>(context->gpu()->queue());
  metalQueue->schedulePresent(drawable);
}

void MetalDrawableProxy::onPresentFrame(Context*) {
  releaseDrawable();
}

void MetalDrawableProxy::releaseDrawable() {
  [_metalDrawable release];
  _metalDrawable = nil;
  _renderTarget = nullptr;
}

}  // namespace tgfx
