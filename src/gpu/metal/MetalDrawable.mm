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

#include "MetalDrawable.h"
#import <Metal/Metal.h>
#include "gpu/metal/MetalDefines.h"
#include "gpu/metal/MetalDrawableProxy.h"
#include "gpu/metal/MetalGPU.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

id<CAMetalDrawable> MetalDrawable::AcquireMetalDrawable(CAMetalLayer* metalLayer) {
  if (metalLayer == nil) {
    return nil;
  }
  id<CAMetalDrawable> metalDrawable = nil;
  @autoreleasepool {
    // nextDrawable returns an autoreleased (+0) drawable; retain it inside the pool so the
    // reference stays valid after the pool drains. The caller takes over this retain.
    metalDrawable = [[metalLayer nextDrawable] retain];
  }
  return metalDrawable;
}

BackendRenderTarget MetalDrawable::MakeBackendRenderTarget(id<CAMetalDrawable> drawable) {
  MetalTextureInfo metalInfo = {};
  metalInfo.texture = (__bridge const void*)drawable.texture;
  metalInfo.format = static_cast<unsigned>(drawable.texture.pixelFormat);
  return BackendRenderTarget(metalInfo, static_cast<int>(drawable.texture.width),
                             static_cast<int>(drawable.texture.height));
}

std::shared_ptr<MetalDrawable> MetalDrawable::Make(MetalGPU* gpu, CAMetalLayer* metalLayer,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  auto metalDrawable = AcquireMetalDrawable(metalLayer);
  if (metalDrawable == nil) {
    return nullptr;
  }
  auto width = static_cast<int>(metalDrawable.texture.width);
  auto height = static_cast<int>(metalDrawable.texture.height);
  // The drawable is registered as a Metal resource: when its last reference goes away it is
  // returned to the GPU's return queue and destroyed (releasing the CAMetalDrawable) within the
  // device's synchronized scope instead of on an arbitrary thread.
  return gpu->makeResource<MetalDrawable>(metalDrawable, width, height, std::move(colorSpace));
}

MetalDrawable::MetalDrawable(id<CAMetalDrawable> metalDrawable, int width, int height,
                             std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(width, height, std::move(colorSpace)) {
  // Takes over the single retain acquired in Make(); balanced by the destructor release.
  _metalDrawable = metalDrawable;
}

MetalDrawable::~MetalDrawable() {
  // Discard the frame if it was never delivered; the CAMetalDrawable is released later in
  // onRelease(), inside the GPU's synchronized scope.
  abandon();
}

void MetalDrawable::onRelease(MetalGPU*) {
  [_metalDrawable release];
  _metalDrawable = nil;
}

std::shared_ptr<RenderTargetProxy> MetalDrawable::onImport(Context* context) {
  // Wrap the acquired drawable in the automatic path's proxy so the presentation is carried by
  // the proxy hooks (onSchedulePresentation/onPresentFrame) exactly once for both paths. The
  // proxy takes over the (+1) drawable reference; onRelease() is a nil-safe no-op afterwards.
  auto texture = [_metalDrawable texture];
  auto format = MetalPixelFormatToPixelFormat(static_cast<unsigned>(texture.pixelFormat));
  auto proxy = MetalDrawableProxy::WrapAcquiredDrawable(context, _metalDrawable,
                                                        static_cast<int>(texture.width),
                                                        static_cast<int>(texture.height), format);
  _metalDrawable = nil;
  return proxy;
}

}  // namespace tgfx
