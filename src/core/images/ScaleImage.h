/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "core/images/TransformImage.h"

namespace tgfx {

/**
 * Scales the source image by the given factors.
 */
class ScaleImage : public TransformImage {
 public:
  static int GetSize(int size, float scale);

  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, const Point& scale);

  int width() const override;

  int height() const override;

  bool isFlat() const override {
    return false;
  }

 protected:
  Point scale = Point::Make(1.0f, 1.0f);

  ScaleImage(std::shared_ptr<Image> source, const Point& scale);

  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::shared_ptr<Image> onMakeScaled(float scaleX, float scaleY) const override;

  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args, TileMode tileModeX,
                                                         TileMode tileModeY,
                                                         const SamplingOptions& sampling,
                                                         const Matrix* uvMatrix) const override;
};
}  // namespace tgfx
