/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "BlurImageFilter.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {
class DualBlurImageFilter : public BlurImageFilter {
 public:
  DualBlurImageFilter(float blurrinessX, float blurrinessY, TileMode tileMode);

  Point blurOffset;
  float downScaling;
  int iteration;
  float scaleFactor = 1.0f;

 protected:
  Rect onFilterBounds(const Rect& srcRect) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(std::shared_ptr<Image> source,
                                                 const Rect& clipBounds,
                                                 const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling,
                                                      SrcRectConstraint constraint,
                                                      const Matrix* uvMatrix) const override;

  void draw(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags,
            PlacementPtr<FragmentProcessor> imageProcessor, float scaleFactor, bool isDown) const;
};
}  // namespace tgfx
