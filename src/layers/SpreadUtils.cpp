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

static inline RRect MakeSpreadRRect(const RRect& rRect, float distance) {
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
        auto spreadRRect = MakeSpreadRRect(rRect, outset);
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
      if (MakeSpreadRRect(rRect, innerOffset).rect().isEmpty()) {
        // The inner hole has vanished; the band fills the shape solid out to its outer edge.
        paint.setStyle(PaintStyle::Fill);
        canvas->drawRRect(MakeSpreadRRect(rRect, outerOffset), paint);
      } else {
        paint.setStyle(PaintStyle::Stroke);
        paint.setStroke(Stroke(effectiveWidth));
        const auto drawRRect = centerOffset == 0.0f ? rRect : MakeSpreadRRect(rRect, centerOffset);
        DEBUG_ASSERT(!drawRRect.rect().isEmpty());
        canvas->drawRRect(drawRRect, paint);
      }
      break;
    }
  }
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
  if (input.extraSource == nullptr ||
      input.extraSource->type() != StyleInputSource::Type::Contour) {
    return {nullptr, {}, false};
  }
  auto* contour = static_cast<const ContourInputSource*>(input.extraSource.get());
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
