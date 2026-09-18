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

#include "layers/layerstyles/AnalyticShadowUtils.h"
#include "core/RRectUtils.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/SpreadUtils.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

// Returns the path as an RRect when the closed-form shadow supports it; nullopt otherwise.
static inline std::optional<RRect> AsAnalyticRRect(const Path& path) {
  // Path::isRect, Path::isRRect and Path::isOval each recognize only their own shape kind, so
  // all three must be checked.
  Rect rect = {};
  if (path.isRect(&rect)) {
    return RRect::MakeRect(rect);
  }
  RRect rRect = {};
  if (path.isRRect(&rRect)) {
    // The closed form assumes all four corners share the same radii. Supporting per-corner
    // radii would require a piecewise row-span formula in the shader, whose benefit has not
    // been validated.
    return rRect.isComplex() ? std::nullopt : std::make_optional(rRect);
  }
  if (path.isOval(&rect)) {
    return RRect::MakeOval(rect);
  }
  return std::nullopt;
}

std::optional<RRect> AnalyticShadowUtils::MakeShadowShape(const LayerStyleInput& input,
                                                          float spread) {
  auto* source = input.findExtraSource(StyleInputSource::Type::Contour);
  if (source == nullptr) {
    return std::nullopt;
  }
  auto* contour = static_cast<const ContourInputSource*>(source);
  if (!contour->shape().has_value()) {
    return std::nullopt;
  }
  auto& styledShape = *contour->shape();
  if (styledShape.shape == nullptr) {
    return std::nullopt;
  }
  // Stroked outlines are not analytic: the offset curve of an ellipse is not an ellipse, so the
  // inner and outer edges of a stroke cannot be described exactly by two concentric RRects. The
  // RRect draw op still approximates strokes that way when the corner ellipse is not too
  // elongated relative to the stroke width; accepting the same approximation here is left as a
  // possible future optimization.
  if (styledShape.type != StyledShapeType::Fill) {
    return std::nullopt;
  }

  auto [shape, shapeMatrix] = SpreadUtils::UnwrapMatrixShape(styledShape.shape);
  if (shape == nullptr) {
    return std::nullopt;
  }
  const auto path = shape->getPath();
  if (path.isEmpty()) {
    return std::nullopt;
  }
  auto rRect = AsAnalyticRRect(path);
  if (!rRect.has_value()) {
    return std::nullopt;
  }

  auto matrix = shapeMatrix;
  matrix.postScale(input.contentScale, input.contentScale);
  // The closed form assumes axis-aligned shapes. Supporting arbitrary transforms would require
  // a more complex row-span formula in the shader, whose benefit has not been validated.
  auto result = RRectUtils::TryAxisAlignedTransform(*rRect, matrix);
  if (!result.has_value()) {
    return std::nullopt;
  }
  // Shift the origin to the content image's top-left corner.
  result->offset(-input.contentOffset.x, -input.contentOffset.y);

  if (!FloatNearlyZero(spread)) {
    const auto scales = matrix.getAxisScales();
    // Spread is measured in the layer's local space, so it scales with the geometry.
    result = SpreadUtils::MakeSpreadRRect(*result, {spread * scales.x, spread * scales.y});
    DEBUG_ASSERT(!result->isComplex());
  }
  return result;
}

}  // namespace tgfx
