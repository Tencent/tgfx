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

#include "ShapeContent.h"
#include "core/utils/StrokeUtils.h"
#include "tgfx/core/Canvas.h"

namespace tgfx {

ShapeContent::ShapeContent(std::shared_ptr<Shape> shape, const LayerPaint& paint)
    : DrawContent(paint), shape(std::move(shape)), pathEffect(paint.pathEffect),
      strokeAlign(paint.strokeAlign) {
}

Rect ShapeContent::getBounds() const {
  auto bounds = onGetBounds();
  if (!stroke) {
    return bounds;
  }
  switch (strokeAlign) {
    case StrokeAlign::Center:
      // ApplyStrokeToBounds outsets the raw path by half the stroke width plus miter/cap
      // allowance for Center alignment.
      ApplyStrokeToBounds(*stroke, &bounds, Matrix::I(), true);
      break;
    case StrokeAlign::Outside: {
      // Outside alignment must match the bounds of an expanded fill shape produced with a
      // doubled stroke width (this is what StrokeShape::onGetBounds would return if we baked
      // the expansion into the shape). Apply the miter/cap-aware outset with the doubled width
      // so callers such as SpreadUtils see a footprint consistent with StyledShape::getBounds().
      Stroke doubled = *stroke;
      doubled.width *= 2;
      ApplyStrokeToBounds(doubled, &bounds, Matrix::I(), true);
      break;
    }
    case StrokeAlign::Inside: {
      ApplyStrokeToBounds(*stroke, &bounds, Matrix::I(), true);
      // Inside stroke stays within the original path's fill region.
      auto clipBounds = shape->getBounds();
      if (!bounds.intersect(clipBounds)) {
        bounds.setEmpty();
      }
      break;
    }
  }
  return bounds;
}

Rect ShapeContent::getTightBounds(const Matrix& matrix) const {
  auto originalPath = shape->getPath();
  if (!stroke) {
    originalPath.transform(matrix);
    return originalPath.getBounds();
  }
  if (strokeAlign == StrokeAlign::Inside) {
    // The Inside band lives entirely within the original path, so the transformed original
    // path's bounds are already a tight upper bound: the outward edge of the band coincides
    // with the original boundary, and the inward edge stays inside.
    originalPath.transform(matrix);
    return originalPath.getBounds();
  }
  // Center and Outside: extremum of the alignment-aware stroke path lives on the outward side
  // of the band, so its transformed bounds are tight without any further clamping.
  auto strokedPath = getStrokedPath();
  strokedPath.transform(matrix);
  return strokedPath.getBounds();
}

bool ShapeContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  auto originalPath = shape->getPath();
  if (!stroke) {
    return originalPath.contains(localX, localY);
  }
  if (!getStrokedPath().contains(localX, localY)) {
    return false;
  }
  switch (strokeAlign) {
    case StrokeAlign::Center:
      return true;
    case StrokeAlign::Inside:
      return originalPath.contains(localX, localY);
    case StrokeAlign::Outside:
      return !originalPath.contains(localX, localY);
  }
  return true;
}

Rect ShapeContent::onGetBounds() const {
  return shape->getBounds();
}

void ShapeContent::onDraw(Canvas* canvas, const Paint& paint) const {
  if (needsAlignmentClip()) {
    AutoCanvasRestore autoRestore(canvas);
    canvas->clipPath(getAlignmentClipPath(), paint.isAntiAlias());
    // Rasterize the pre-expanded stroke as a fill: alignment doubles the stroke width so the
    // half kept by the clip has the requested visible width.
    Paint fillPaint = paint;
    fillPaint.setStyle(PaintStyle::Fill);
    canvas->drawShape(getExpandedStrokeShape(), fillPaint);
    return;
  }
  // pathEffect is defined as stroke-only in LayerPaint. Guard the effect application so a Fill
  // paint that accidentally carries a pathEffect does not silently mutate the fill geometry.
  if (pathEffect != nullptr && stroke != nullptr) {
    // Center-align stroke with a path effect (e.g. dash): apply the effect to the geometry
    // before letting the canvas stroke it.
    auto effected = Shape::ApplyEffect(shape, pathEffect);
    if (effected != nullptr) {
      canvas->drawShape(std::move(effected), paint);
      return;
    }
  }
  canvas->drawShape(shape, paint);
}

bool ShapeContent::onHasSameGeometry(const GeometryContent* other) const {
  const auto* otherShape = static_cast<const ShapeContent*>(other);
  if (shape != otherShape->shape) {
    return false;
  }
  if (pathEffect != otherShape->pathEffect) {
    return false;
  }
  return strokeAlign == otherShape->strokeAlign;
}

bool ShapeContent::needsAlignmentClip() const {
  // Alignment clipping is only meaningful when there is an actual stroke to expand.
  return stroke != nullptr && strokeAlign != StrokeAlign::Center;
}

Path ShapeContent::getAlignmentClipPath() const {
  auto clipPath = shape->getPath();
  if (strokeAlign == StrokeAlign::Outside) {
    clipPath.toggleInverseFillType();
  }
  return clipPath;
}

std::shared_ptr<Shape> ShapeContent::getExpandedStrokeShape() const {
  auto expanded = shape;
  if (pathEffect != nullptr) {
    expanded = Shape::ApplyEffect(expanded, pathEffect);
    if (expanded == nullptr) {
      return nullptr;
    }
  }
  // Double the stroke width so the clip mask trims the expanded outline back to a band of the
  // requested width on the kept side.
  Stroke doubled = *stroke;
  doubled.width *= 2;
  return Shape::ApplyStroke(std::move(expanded), &doubled);
}

Path ShapeContent::getStrokedPath() const {
  auto path = shape->getPath();
  // Ignore filterPath's boolean result and keep `path` unchanged on rejection. This matches the
  // semantics of EffectShape::onGetPath used by the GPU draw path, so bounds/hit-test agree with
  // rasterization on the same geometry.
  if (pathEffect != nullptr) {
    pathEffect->filterPath(&path);
  }
  // Center: the natural 1x stroke already covers the visible band on both sides of the boundary
  // by width/2. Inside/Outside: double the width so callers combining this path with the
  // original geometry (intersect for Inside, difference for Outside) get a band whose visible
  // half has the requested width.
  Stroke strokeToApply = *stroke;
  if (strokeAlign != StrokeAlign::Center) {
    strokeToApply.width *= 2;
  }
  strokeToApply.applyToPath(&path);
  return path;
}

}  // namespace tgfx
