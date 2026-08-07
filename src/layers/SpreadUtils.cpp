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

#include "layers/SpreadUtils.h"
#include <algorithm>
#include "core/shapes/MatrixShape.h"
#include "core/utils/Log.h"
#include "core/utils/ShapeUtils.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {

RRect SpreadUtils::MakeSpreadRRect(const RRect& rRect, float distance) {
  auto bounds = rRect.rect();
  bounds.outset(distance, distance);
  if (bounds.width() <= 0.0f || bounds.height() <= 0.0f) {
    return {};
  }
  // Adjust radii by the same amount so the expanded/contracted corners stay concentric with the
  // original. Corners that are already sharp (zero radius) stay sharp.
  auto radii = rRect.radii();
  for (auto& corner : radii) {
    if (corner.x > 0.0f) {
      corner.x = std::max(0.0f, corner.x + distance);
    }
    if (corner.y > 0.0f) {
      corner.y = std::max(0.0f, corner.y + distance);
    }
  }
  RRect result = {};
  result.setRectRadii(bounds, radii);
  return result;
}

float SpreadUtils::StrokeOutset(float width, StrokeAlign align) {
  switch (align) {
    case StrokeAlign::Center:
      return width * 0.5f;
    case StrokeAlign::Outside:
      return width;
    case StrokeAlign::Inside:
      return 0.0f;
  }
  return 0.0f;
}

std::pair<std::shared_ptr<Shape>, Matrix> SpreadUtils::UnwrapMatrixShape(
    std::shared_ptr<Shape> shape) {
  auto matrix = Matrix::I();
  while (auto* ms = ShapeUtils::AsMatrixShape(shape.get())) {
    matrix.preConcat(ms->matrix);
    shape = ms->shape;
  }
  return {std::move(shape), matrix};
}

static inline void DrawSpreadRRect(Canvas* canvas, const RRect& rRect, StyledShapeType type,
                                   StrokeAlign strokeAlign, float strokeWidth, float spread) {
  Paint paint = {};
  paint.setColor(Color::White());
  paint.setAntiAlias(true);
  switch (type) {
    case StyledShapeType::Fill:
    case StyledShapeType::FillStroke: {
      auto outset = spread;
      if (type == StyledShapeType::FillStroke) {
        outset += SpreadUtils::StrokeOutset(strokeWidth, strokeAlign);
      }
      if (outset != 0) {
        auto spreadRRect = SpreadUtils::MakeSpreadRRect(rRect, outset);
        DEBUG_ASSERT(!spreadRRect.rect().isEmpty());
        canvas->drawRRect(spreadRRect, paint);
      } else {
        DEBUG_ASSERT(!rRect.rect().isEmpty());
        canvas->drawRRect(rRect, paint);
      }
      break;
    }
    case StyledShapeType::Stroke: {
      const auto effectiveWidth = strokeWidth + 2.0f * spread;
      const auto halfWidth = effectiveWidth * 0.5f;
      // Displacement of the original stroke centerline relative to the outline. The underlying
      // stroker only draws a centered band, so non-center alignments are emulated by moving the
      // stroked outline.
      auto centerOffset = 0.0f;
      switch (strokeAlign) {
        case StrokeAlign::Outside:
          centerOffset = strokeWidth * 0.5f;
          break;
        case StrokeAlign::Inside:
          centerOffset = -strokeWidth * 0.5f;
          break;
        case StrokeAlign::Center:
          centerOffset = 0.0f;
          break;
      }
      const auto innerOffset = centerOffset - halfWidth;
      const auto outerOffset = centerOffset + halfWidth;
      if (SpreadUtils::MakeSpreadRRect(rRect, innerOffset).rect().isEmpty()) {
        // The inner hole has vanished; the band fills the shape solid out to its outer edge.
        paint.setStyle(PaintStyle::Fill);
        canvas->drawRRect(SpreadUtils::MakeSpreadRRect(rRect, outerOffset), paint);
      } else {
        paint.setStyle(PaintStyle::Stroke);
        paint.setStroke(Stroke(effectiveWidth));
        const auto drawRRect =
            centerOffset == 0.0f ? rRect : SpreadUtils::MakeSpreadRRect(rRect, centerOffset);
        DEBUG_ASSERT(!drawRRect.rect().isEmpty());
        canvas->drawRRect(drawRRect, paint);
      }
      break;
    }
  }
}

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

std::optional<SpreadUtils::AnalyticShape> SpreadUtils::MakeAnalyticShape(
    const LayerStyleInput& input, float spread) {
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

  auto [shape, shapeMatrix] = UnwrapMatrixShape(styledShape.shape);
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

  // The closed form assumes an axis-aligned shape, so only translation and axis-aligned scale can
  // be folded into the geometry. Rotation, skew and perspective must fall back.
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
    scaled = MakeSpreadRRect(scaled, spread * scaleX);
    if (scaled.rect().isEmpty()) {
      return std::nullopt;
    }
  }
  // Radii are re-normalized by setRectRadii inside MakeSpreadRRect, but the no-spread path skips
  // it; scale() preserves the type, so re-check rather than assume.
  if (scaled.isComplex()) {
    return std::nullopt;
  }
  return AnalyticShape{scaled};
}

bool SpreadUtils::IsSpreadCollapsed(const Shape& shape, StyledShapeType type, float strokeWidth,
                                    StrokeAlign strokeAlign, float spread) {
  switch (type) {
    case StyledShapeType::Fill: {
      auto bounds = shape.getPath().getBounds();
      return bounds.width() + 2.0f * spread <= 0.0f || bounds.height() + 2.0f * spread <= 0.0f;
    }
    case StyledShapeType::Stroke: {
      return strokeWidth + 2.0f * spread <= 0.0f;
    }
    case StyledShapeType::FillStroke: {
      auto bounds = shape.getPath().getBounds();
      auto outset = spread + StrokeOutset(strokeWidth, strokeAlign);
      return bounds.width() + 2.0f * outset <= 0.0f || bounds.height() + 2.0f * outset <= 0.0f;
    }
  }
  return false;
}

SpreadUtils::SpreadResult SpreadUtils::MakeSpreadShapeImage(const LayerStyleInput& input,
                                                            float spread) {
  auto* source = input.findExtraSource(StyleInputSource::Type::Contour);
  if (source == nullptr) {
    return {nullptr, {}, false};
  }
  auto contour = static_cast<const ContourInputSource*>(source);
  if (!contour->shape().has_value()) {
    return {nullptr, {}, false};
  }
  auto& styledShape = *contour->shape();
  DEBUG_ASSERT(styledShape.shape != nullptr);
  if (styledShape.shape == nullptr || styledShape.shape->getPath().isEmpty()) {
    return {nullptr, {}, false};
  }
  // The contentShape footprint must stay within the content image; a shape exceeding it cannot be
  // represented in the output and is treated as unavailable.
  auto shapeBounds = styledShape.getBounds();
  shapeBounds.scale(input.contentScale, input.contentScale);
  auto contentBounds = Rect::MakeXYWH(input.contentOffset.x, input.contentOffset.y,
                                      static_cast<float>(input.content->width()),
                                      static_cast<float>(input.content->height()));
  DEBUG_ASSERT(contentBounds.contains(shapeBounds));
  if (!contentBounds.contains(shapeBounds)) {
    return {nullptr, {}, false};
  }
  auto [shape, shapeMatrix] = UnwrapMatrixShape(styledShape.shape);
  DEBUG_ASSERT(shape != nullptr);
  if (shape == nullptr) {
    return {nullptr, {}, false};
  }
  if (IsSpreadCollapsed(*shape, styledShape.type, styledShape.strokeWidth, styledShape.strokeAlign,
                        spread)) {
    return {nullptr, {}, true};
  }

  const auto path = shape->getPath();
  PictureRecorder recorder;
  auto* recordCanvas = recorder.beginRecording();
  recordCanvas->scale(input.contentScale, input.contentScale);
  recordCanvas->concat(shapeMatrix);

  Rect rect = {};
  RRect rRect = {};
  auto type = styledShape.type;
  const auto strokeWidth = styledShape.strokeWidth;
  const auto strokeAlign = styledShape.strokeAlign;
  if (path.isOval(&rect)) {
    DrawSpreadRRect(recordCanvas, RRect::MakeOval(rect), type, strokeAlign, strokeWidth, spread);
  } else if (path.isRRect(&rRect)) {
    DrawSpreadRRect(recordCanvas, rRect, type, strokeAlign, strokeWidth, spread);
  } else {
    if (!path.isRect(&rect)) {
      // Complex paths use their bounding rect as a fill approximation for the shadow source. A
      // collapsed stroke is already rejected by IsSpreadCollapsed above, so any stroke reaching
      // here is non-collapsed and safe to approximate as a fill.
      rect = path.getBounds();
      if (type == StyledShapeType::Stroke || type == StyledShapeType::FillStroke) {
        auto outset = SpreadUtils::StrokeOutset(strokeWidth, styledShape.strokeAlign);
        rect.outset(outset, outset);
      }
      type = StyledShapeType::Fill;
    }
    DrawSpreadRRect(recordCanvas, RRect::MakeRectXY(rect, 0, 0), type, strokeAlign, strokeWidth,
                    spread);
  }

  auto picture = recorder.finishRecordingAsPicture();
  Point offset = {};
  auto image = ToImageWithOffset(std::move(picture), &offset);
  DEBUG_ASSERT(image != nullptr);
  if (image == nullptr) {
    return {nullptr, {}, false};
  }
  return {std::move(image),
          {offset.x - input.contentOffset.x, offset.y - input.contentOffset.y},
          false};
}

}  // namespace tgfx
