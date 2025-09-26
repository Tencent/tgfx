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

#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Matrix3D.h"

namespace tgfx {

/**
 * Transform3DImageFilter is an image filter that applies a perspective transformation to the input
 * image.
 */
class Transform3DImageFilter final : public ImageFilter {
 public:
  /**
   * Creates a Transform3DImageFilter with the specified transformation matrix.
   * The transformation matrix transforms 3D model coordinates to destination coordinates for x and
   * y before perspective division. The z value is mapped to the [-1, 1] range before perspective
   * division; content outside this z range will be clipped.
   */
  explicit Transform3DImageFilter(const Matrix3D& matrix);

 private:
  Type type() const override {
    return Type::Transform3D;
  }

  Rect onFilterBounds(const Rect& srcRect) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(std::shared_ptr<Image> source,
                                                 const Rect& renderBounds,
                                                 const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(std::shared_ptr<Image> source,
                                                      const FPArgs& args,
                                                      const SamplingOptions& sampling,
                                                      SrcRectConstraint constraint,
                                                      const Matrix* uvMatrix) const override;

  /**
   * 3D transformation matrix used to convert model coordinates to clip space.
   */
  Matrix3D _matrix = Matrix3D::I();
};

}  // namespace tgfx
