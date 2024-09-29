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
#include "SubsetImage.h"

namespace tgfx {

/**
 * Scales the source image by the given factors.
 */
class ScaleImage : public SubsetImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, Orientation orientation,
                                         const Rect& bounds, const Point& scale);

  int width() const override;

  int height() const override;

 protected:
  std::shared_ptr<Image> onCloneWith(std::shared_ptr<Image> newSource) const override;

  std::shared_ptr<Image> onMakeOriented(Orientation orientation) const override;

  std::shared_ptr<Image> onMakeSubset(const Rect& subset) const override;

  std::shared_ptr<Image> onMakeScaled(float scaleX, float scaleY) const override;

  std::optional<Matrix> concatUVMatrix(const Matrix* uvMatrix) const override;

 private:
  float scaleX = 1.0f;
  float scaleY = 1.0f;

  ScaleImage(std::shared_ptr<Image> source, Orientation orientation, const Rect& bounds,
             float scaleX, float scaleY);
};
}  // namespace tgfx
