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

#include "gpu/processors/TentBlur1DFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {
class TentBlurImageFilter : public ImageFilter {
 public:
  TentBlurImageFilter(float radiusX, float radiusY, TileMode tileMode);

  /**
   * The maximum radius that can be passed to TentBlur() in the X and/or Y radius values. Larger
   * requested radii must manually downscale the input image and upscale the output image.
   */
  static float MaxRadius();

  float radiusX = 0.0f;
  float radiusY = 0.0f;
  TileMode tileMode = TileMode::Decal;

 protected:
  Type type() const override {
    return Type::TentBlur;
  }

  Rect onFilterBounds(const Rect& rect, MapDirection mapDirection) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(std::shared_ptr<Image> source,
                                                 const Rect& renderBounds,
                                                 const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling,
                                                      SrcRectConstraint constraint,
                                                      const Matrix* uvMatrix) const override;

  PlacementPtr<FragmentProcessor> getSourceFragmentProcessor(std::shared_ptr<Image> source,
                                                             Context* context, uint32_t renderFlags,
                                                             const Rect& drawRect,
                                                             const Point& scales) const;
};
}  // namespace tgfx
