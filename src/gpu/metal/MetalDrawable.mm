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
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/metal/MetalTypes.h"

namespace tgfx {

std::shared_ptr<MetalDrawable> MetalDrawable::Make(Context* context, CAMetalLayer* metalLayer,
                                                   std::shared_ptr<ColorSpace> colorSpace) {
  if (metalLayer == nil) {
    return nullptr;
  }
  auto metalDrawable = [metalLayer nextDrawable];
  if (metalDrawable == nil) {
    return nullptr;
  }
  MetalTextureInfo metalInfo = {};
  metalInfo.texture = (__bridge const void*)metalDrawable.texture;
  metalInfo.format = static_cast<unsigned>(metalDrawable.texture.pixelFormat);
  auto width = static_cast<int>(metalDrawable.texture.width);
  auto height = static_cast<int>(metalDrawable.texture.height);
  BackendRenderTarget backendRT(metalInfo, width, height);
  auto renderTarget = RenderTargetProxy::MakeFrom(context, backendRT, ImageOrigin::TopLeft);
  if (renderTarget == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<MetalDrawable>(
      new MetalDrawable(context, std::move(renderTarget), metalDrawable, std::move(colorSpace)));
}

MetalDrawable::MetalDrawable(Context* context, std::shared_ptr<RenderTargetProxy> renderTarget,
                             id<CAMetalDrawable> metalDrawable,
                             std::shared_ptr<ColorSpace> colorSpace)
    : Drawable(context, std::move(renderTarget), std::move(colorSpace)),
      _metalDrawable([metalDrawable retain]) {
}

MetalDrawable::~MetalDrawable() {
  present();
  [_metalDrawable release];
}

id<CAMetalDrawable> MetalDrawable::getMetalDrawable() const {
  return _metalDrawable;
}

void MetalDrawable::onPresent() {
  // Presenting the drawable directly schedules the presentation after all command buffers that
  // have been enqueued so far, so the GPU finishes rendering before the drawable is displayed.
  [_metalDrawable present];
}

}  // namespace tgfx
