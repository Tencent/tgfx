/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gpu/proxies/RenderTargetProxy.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {
class BlurImageFilter : public ImageFilter {
 public:
  BlurImageFilter(Point blurOffset, float downScaling, int iteration, TileMode tileMode,
                  float scaleFactor);

  Point blurOffset;
  float downScaling;
  int iteration;
  TileMode tileMode;
  float scaleFactor = 1.0f;

 protected:
  Type type() const override {
    return Type::Blur;
  }

  Rect onFilterBounds(const Rect& srcRect) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(std::shared_ptr<Image> source,
                                                 const Rect& clipBounds,
                                                 const TPArgs& args) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                         const FPArgs& args,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;

  void draw(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags,
            std::unique_ptr<FragmentProcessor> imageProcessor, const Size& imageSize,
            bool isDown) const;
};
}  // namespace tgfx
