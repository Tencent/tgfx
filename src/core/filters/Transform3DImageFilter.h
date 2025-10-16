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
   * Creates a filter that applies a perspective transformation to the input image.
   * @param matrix The transformation matrix that maps vertices from the local coordinate system to
   * the destination coordinate system during 3D perspective transformation. The result of
   * multiplying this matrix with the vertex coordinates will undergo perspective division; the
   * resulting x and y components are the final projected coordinates. The valid range for the z
   * component is consistent with OpenGL's definition for the clipping space, which is [-1, 1]. Any
   * content with a z component outside this range will be clipped.
   * The default transformation anchor is at the top-left origin (0,0) of the source image,
   * user-defined anchors are included in the matrix.
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

  Matrix3D matrix = Matrix3D::I();
};

}  // namespace tgfx
