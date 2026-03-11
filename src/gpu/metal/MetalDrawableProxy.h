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

#pragma once

#import <QuartzCore/QuartzCore.h>
#include "gpu/proxies/RenderTargetProxy.h"

namespace tgfx {
/**
 * MetalDrawableProxy defers the acquisition of a Metal drawable until the render target is actually
 * needed. It calls [CAMetalLayer nextDrawable] lazily in getRenderTarget() and wraps the resulting
 * texture as an ExternalRenderTarget.
 */
class MetalDrawableProxy : public RenderTargetProxy {
 public:
  MetalDrawableProxy(Context* context, CAMetalLayer* metalLayer, int width, int height,
                     PixelFormat format);

  Context* getContext() const override;
  int width() const override;
  int height() const override;
  PixelFormat format() const override;
  int sampleCount() const override;
  ImageOrigin origin() const override;
  bool externallyOwned() const override;
  std::shared_ptr<TextureView> getTextureView() const override;
  std::shared_ptr<RenderTarget> getRenderTarget() const override;

  id<CAMetalDrawable> getDrawable() const;

 private:
  Context* _context = nullptr;
  CAMetalLayer* _metalLayer = nil;
  int _width = 0;
  int _height = 0;
  PixelFormat _format = PixelFormat::RGBA_8888;
  // Mutable because getRenderTarget() is const (inherited from RenderTargetProxy) but needs to
  // lazily acquire the Metal drawable on first access.
  mutable id<CAMetalDrawable> _metalDrawable = nil;
  mutable std::shared_ptr<RenderTarget> _renderTarget = nullptr;
};
}  // namespace tgfx
