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
   * Creates a Transform3DImageFilter with the specified PerspectiveInfo.
   */
  explicit Transform3DImageFilter(const Matrix3D& matrix, const Size& viewportSize);

  /**
   * Sets the anchor point for the layer's 3D transformation, as well as the center point observed
   * by the camera in the projection model. Valid value range is [0, 1], values outside this range
   * will be clamped.
   * Point (0, 0) represents the top-left corner of the layer, point (1, 1) represents the
   * bottom-right corner of the layer. Default value is (0.5, 0.5), which is the center point of the
   * layer.
   * Affine transformations such as rotation implied in the matrix3D are performed based on this
   * anchor point. The X and Y coordinates of the camera's position in 3D space during layer
   * projection are also determined by this origin point.
   */
  void setOrigin(const Point& origin);

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

  Point _origin = {0.5, 0.5};

  /**
   * View port size, used to map NDC coordinates to window coordinates.
   */
  Size _viewportSize = {0, 0};
};

}  // namespace tgfx
