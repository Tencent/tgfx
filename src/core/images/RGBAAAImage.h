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
  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args, TileMode tileModeX,
                                                         TileMode tileModeY,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override;

 private:
  Point alphaStart = Point::Zero();

  RGBAAAImage(std::shared_ptr<Image> source, const Rect& bounds, const Point& alphaStart);
};
}  // namespace tgfx
