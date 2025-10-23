/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "gpu/processors/GaussianBlur1DFragmentProcessor.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {
class GaussianBlurImageFilter : public ImageFilter {
 public:
  GaussianBlurImageFilter(float blurrinessX, float blurrinessY, TileMode tileMode);

  /**
   * The maximum sigma that can be passed to blur() in the X and/or Y sigma values. Larger requested
   * sigmas must manually downscale the input image and upscale the output image.
   */
  static float MaxSigma();

  float blurrinessX = 0.0f;
  float blurrinessY = 0.0f;
  TileMode tileMode = TileMode::Decal;

 protected:
  Type type() const override {
    return Type::Blur;
  }

  Rect onGetOutputBounds(const Rect& inputRect) const override;

  Rect onGetInputBounds(const Rect& outputRect) const override;

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
