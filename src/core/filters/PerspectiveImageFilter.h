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

#include "core/Matrix3D.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

/**
 * PerspectiveImageFilter is an image filter that applies a perspective transformation to the input
 * image.
 */
class PerspectiveImageFilter final : public ImageFilter {
 public:
  /**
   * Creates a PerspectiveImageFilter with the specified PerspectiveInfo.
   */
  explicit PerspectiveImageFilter(const PerspectiveInfo& info);

 private:
  Type type() const override {
    return Type::Perspective;
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

  static Matrix3D MakeProjectMatrix(PerspectiveType projectType, const Rect& rect);

  PerspectiveInfo info = {};

  /**
   * The projection matrix for the unit rectangle LTRB(-1, -1, 1, 1).
   */
  Matrix3D normalProjectMatrix;

  Matrix3D modelRotateMatrix;
};

}  // namespace tgfx
