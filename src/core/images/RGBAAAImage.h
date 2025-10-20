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

#include "SubsetImage.h"

namespace tgfx {
class RGBAAAImage : public SubsetImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, int displayWidth,
                                         int displayHeight, int alphaStartX, int alphaStartY);

  bool isAlphaOnly() const override {
    return false;
  }

 protected:
  Type type() const override {
    return Type::RGBAAA;
  }

  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const SamplingArgs& samplingArgs, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> dstColorSpace) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override;

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;

 private:
  Point alphaStart = {};

  RGBAAAImage(std::shared_ptr<Image> source, const Rect& bounds, const Point& alphaStart);
};
}  // namespace tgfx
