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
#include <memory>
#include "tgfx/gpu/Backend.h"
#include "tgfx/gpu/Drawable.h"

namespace tgfx {

/**
 * A Drawable backed by an id<CAMetalDrawable> acquired from a CAMetalLayer. The drawable is
 * acquired at nextDrawable() time and the frame is imported into a Context later; the frame
 * stays readable until it is presented through Context::present(). Reading back requires the
 * layer's framebufferOnly property to be NO, otherwise the drawable texture cannot be used as
 * a blit source.
 */
class MetalDrawable : public Drawable {
 public:
  /**
   * Acquires a drawable from the specified CAMetalLayer. The call blocks until the layer can
   * provide a drawable. Returns nullptr if the layer is nil or has no drawable available.
   */
  static std::shared_ptr<MetalDrawable> Make(CAMetalLayer* metalLayer,
                                             std::shared_ptr<ColorSpace> colorSpace);

  /**
   * Acquires the next drawable from the layer, retaining it inside an autorelease pool so the
   * reference stays valid after the pool drains. The caller takes over the returned (+1)
   * reference. Shared by MetalDrawable::Make() and MetalDrawableProxy (the automatic path's
   * stable proxy), so drawable acquisition exists exactly once.
   */
  static id<CAMetalDrawable> AcquireMetalDrawable(CAMetalLayer* metalLayer);

  /**
   * Describes the drawable's texture as a BackendRenderTarget. Shared by
   * MetalDrawable::onImport() and MetalDrawableProxy so the texture wrapping exists exactly
   * once.
   */
  static BackendRenderTarget MakeBackendRenderTarget(id<CAMetalDrawable> drawable);

  ~MetalDrawable() override;

 protected:
  std::shared_ptr<RenderTargetProxy> onImport(Context* context) override;
  bool onSchedulePresent(Context* context) override;
  void onPresent(Context* context) override;

 private:
  MetalDrawable(id<CAMetalDrawable> metalDrawable, int width, int height,
                std::shared_ptr<ColorSpace> colorSpace);

  id<CAMetalDrawable> _metalDrawable = nil;
};

}  // namespace tgfx
