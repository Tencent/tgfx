/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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
#include <optional>
#include <utility>
#include "tgfx/core/Image.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/layerstyles/LayerStyleInput.h"

namespace tgfx {

class SpreadUtils {
 public:
  struct SpreadResult {
    std::shared_ptr<Image> image = nullptr;
    /**
     * The offset of the image relative to the content image, scaled by contentScale.
     */
    Point offset = {};
    /**
     * Whether the shape collapsed to empty due to spread (e.g. negative spread fully consumed the
     * geometry).
     */
    bool collapsed = false;
  };

  /**
   * A shape that can be evaluated analytically, already carrying the spread expansion.
   */
  struct AnalyticShape {
    /**
     * The spread-adjusted rounded rectangle, expressed in the coordinate space a LayerStyle draws
     * in: content pixels with the origin at contentOffset. At most one distinct corner radius;
     * never Complex.
     */
    RRect rRect = {};
  };

  /**
   * Returns the outward expansion distance from the path boundary caused by the stroke.
   */
  static float StrokeOutset(float width, StrokeAlign align);

  /**
   * Peels off the nested MatrixShape wrappers from a shape, returning the innermost unwrapped shape
   * together with the accumulated matrix. The returned matrix maps the unwrapped shape's local
   * space to the original shape's space.
   */
  static std::pair<std::shared_ptr<Shape>, Matrix> UnwrapMatrixShape(std::shared_ptr<Shape> shape);

  /**
   * Applies spread to a rounded rectangle: the bounds are outset by distance and each non-zero
   * radius is adjusted by the same amount so the corners stay concentric. Returns an empty RRect
   * when the result collapses.
   */
  static RRect MakeSpreadRRect(const RRect& rRect, float distance);

  /**
   * Returns the spread-adjusted shape when the shadow can be computed in closed form, or nullopt
   * when it cannot and the caller must fall back to the filter path. Requires a fill-only shape
   * whose outline is exact and reduces to a Rect, a uniformly rounded RRect or an Oval, optionally
   * placed by a translation and axis-aligned scale.
   *
   * The returned geometry is ready to draw with in a LayerStyle's onDraw: it is measured in content
   * pixels, the same space the shadow's sigma uses, with the origin the canvas already carries.
   */
  static std::optional<AnalyticShape> MakeAnalyticShape(const LayerStyleInput& input, float spread);

  /**
   * Rasterizes the contentShape with spread applied into a tightly-sized alpha image. Positive
   * spread outsets the shape, negative spread insets it. Returns {nullptr, {}, false} when
   * contentShape is unavailable or the path is empty. When the shape collapses to empty because the
   * spread fully consumes the geometry, returns {nullptr, {}, true} with collapsed set to true.
   */
  static SpreadResult MakeSpreadShapeImage(const LayerStyleInput& input, float spread);

 private:
  static bool IsSpreadCollapsed(const Shape& shape, StyledShapeType type, float strokeWidth,
                                StrokeAlign strokeAlign, float spread);
};

}  // namespace tgfx
