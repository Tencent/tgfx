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
#include "gpu/proxies/DrawableProxy.h"

namespace tgfx {
class MetalDrawableProxy : public DrawableProxy {
 public:
  MetalDrawableProxy(Context* context, Drawable* drawable, CAMetalLayer* metalLayer,
                     PixelFormat format);

  PixelFormat format() const override;
  ImageOrigin origin() const override;
  std::shared_ptr<RenderTarget> getRenderTarget() const override;

  id<CAMetalDrawable> getMetalDrawable() const;

 private:
  CAMetalLayer* _metalLayer = nil;
  PixelFormat _format = PixelFormat::RGBA_8888;
  mutable id<CAMetalDrawable> _metalDrawable = nil;
};
}  // namespace tgfx
