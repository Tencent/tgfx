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
#include "core/utils/MathExtra.h"
#include "layers/SpreadUtils.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

// Returns the RRect the path reduces to, or nullopt when it is not one of the analytic forms.
// Follows the same oval -> rRect -> rect order used when rasterizing, so both paths agree on what a
// shape is. Path::isRRect already rejects shapes expressible as a rect or an oval, so the three
// branches are mutually exclusive.
static inline std::optional<RRect> AsAnalyticRRect(const Path& path) {
  Rect rect = {};
  RRect rRect = {};
  if (path.isOval(&rect)) {
    return RRect::MakeOval(rect);
  }
  if (path.isRRect(&rRect)) {
    // Per-corner radii would need a segmented row-span formula, which the closed form lacks.
    return rRect.isComplex() ? std::nullopt : std::make_optional(rRect);
  }
  if (path.isRect(&rect)) {
    return RRect::MakeRect(rect);
  }
  return std::nullopt;
}

std::optional<std::pair<Rect, Point>> MakeAnalyticShadowShape(const LayerStyleInput& input,
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
  // A bounding-box stand-in must not be drawn as if it were the outline: for content such as text
  // the box is far larger than the glyphs, so the shadow would change shape entirely.
  if (!styledShape.exact) {
    return std::nullopt;
  }
  // Stroked outlines are not analytic: the offset curve of an ellipse is not an ellipse, so the
  // inner and outer edges of a stroke cannot be described by two concentric RRects.
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

  // The closed form is evaluated along the shape's own axes, so only a transform that keeps them
  // parallel to the canvas axes can be folded into the geometry. Rotation by a quarter turn is
  // allowed because the supported shapes are symmetric about both center axes; skew and perspective
  // tilt the axes and must fall back.
  auto matrix = shapeMatrix;
  matrix.postScale(input.contentScale, input.contentScale);
  if (!matrix.rectStaysRect()) {
    return std::nullopt;
  }
  const auto scaleX = matrix.getScaleX();
  const auto scaleY = matrix.getScaleY();
  if (scaleX <= 0.0f || scaleY <= 0.0f) {
    return std::nullopt;
  }
  auto scaled = *rRect;
  scaled.scale(scaleX, scaleY);
  // Shift into the space the LayerStyle draws in: the canvas has already been translated by
  // contentOffset, so subtracting it here puts the geometry on the same origin the content image
  // and the spread shape image use.
  scaled.offset(matrix.getTranslateX() - input.contentOffset.x,
                matrix.getTranslateY() - input.contentOffset.y);

  // Spread is measured in the layer's local space, so it scales with the geometry. Non-uniform
  // scale would make it anisotropic, which MakeSpreadRRect cannot express.
  if (!FloatNearlyZero(spread)) {
    if (!FloatNearlyEqual(scaleX, scaleY)) {
      return std::nullopt;
    }
    scaled = SpreadUtils::MakeSpreadRRect(scaled, spread * scaleX);
    if (scaled.rect().isEmpty()) {
      return std::nullopt;
    }
  }
  // Radii are re-normalized by setRectRadii inside MakeSpreadRRect, but the no-spread path skips
  // it; scale() preserves the type, so re-check rather than assume.
  if (scaled.isComplex()) {
    return std::nullopt;
  }
  // All four corners share one radius here, so either entry describes the shape.
  return std::make_pair(scaled.rect(), scaled.radii()[0]);
}

}  // namespace tgfx
