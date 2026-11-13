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

#include <memory>
#include <tuple>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

class MatrixShape;

class StrokeShape;

class ShapeUtils {
 public:
  /**
   * Returns the Shape adjusted for the current resolution scale.
   * Used during rendering to decide whether to simplify the Path or apply hairline stroking,
   * depending on the resolution scale.
   */
  static Path GetShapeRenderingPath(std::shared_ptr<Shape> shape, float resolutionScale);

  /**
   * Calculates the alpha reduction factor for hairline rendering, or returns 1.0 if the shape is
   * not rendered as a hairline.
   */
  static float CalculateAlphaReduceFactorIfHairline(std::shared_ptr<Shape> shape);

  /**
   * Returns the shape as a MatrixShape pointer, or nullptr if it is not a MatrixShape.
   */
  static const MatrixShape* AsMatrixShape(const Shape* shape);

  /**
   * Decomposes a shape into its underlying StrokeShape and the associated matrix, or returns
   * nullptr if the shape is not a StrokeShape.
   */
  static std::tuple<std::shared_ptr<StrokeShape>, Matrix> DecomposeStrokeShape(
      std::shared_ptr<Shape> shape);
};

}  // namespace tgfx
