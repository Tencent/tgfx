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
#include "tgfx/core/ColorSpace.h"
#include "tgfx/gpu/Drawable.h"

namespace tgfx {

/**
 * A Drawable backed by an id<CAMetalDrawable> acquired from a CAMetalLayer. Unlike the window's
 * automatic presentation path, which schedules the presentation on the command queue and releases
 * the drawable right away, this drawable is held by the caller until it is released, so
 * readPixels() returns the rendered content even after present(). Reading back requires the
 * layer's framebufferOnly property to be NO, otherwise the drawable texture cannot be used as a
 * blit source.
 */
class MetalDrawable : public Drawable {
 public:
  /**
   * Acquires a drawable from the specified CAMetalLayer and wraps it. The call blocks until the
   * layer can provide a drawable. Returns nullptr if the layer is nil or has no drawable
   * available.
   */
  static std::shared_ptr<MetalDrawable> Make(Context* context, CAMetalLayer* metalLayer,
                                             std::shared_ptr<ColorSpace> colorSpace);

  ~MetalDrawable() override;

 protected:
  void onPresent() override;

 private:
  MetalDrawable(Context* context, std::shared_ptr<RenderTargetProxy> renderTarget,
                id<CAMetalDrawable> metalDrawable, std::shared_ptr<ColorSpace> colorSpace);

  id<CAMetalDrawable> _metalDrawable = nil;
};

}  // namespace tgfx
