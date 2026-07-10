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
  auto strokedPath = getFilledPath();
  strokedPath.transform(matrix);
  return strokedPath.getBounds();
}

bool ShapeContent::hitTestPoint(float localX, float localY) const {
  if (color.alpha <= 0) {
    return false;
  }
  return getFilledPath().contains(localX, localY);
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

Path ShapeContent::getFilledPath() const {
  // Boolean-op subtraction is numerically unstable when the original shape's bounds are sub-pixel
  // thin, and can produce spurious geometry that misleads hit-test. At sub-pixel scale the raw
  // shape is invisible anyway, so skipping the intersect/difference step is harmless. Matches the
  // guard the legacy StrokeStyle::applyStrokeAndAlign used before this pipeline moved into
  // ShapeContent.
  static constexpr float MinBoundExtentForBooleanOp = 0.5f;
  auto path = shape->getPath();
  if (!stroke) {
    return path;
  }
  // Ignore filterPath's boolean result and keep `path` unchanged on rejection. This matches the
  // semantics of EffectShape::onGetPath used by the GPU draw path, so hit-test and rasterization
  // agree on the same geometry.
  if (needsAlignmentClip()) {
    // Build the visible stroke band: expanded stroke intersected with the kept side of the
    // original path. For Outside we subtract the raw path region from the expanded outline.
    Path effected = path;
    if (pathEffect != nullptr) {
      pathEffect->filterPath(&effected);
    }
    Stroke doubled = *stroke;
    doubled.width *= 2;
    doubled.applyToPath(&effected);
    const auto rawBounds = path.getBounds();
    if (rawBounds.width() < MinBoundExtentForBooleanOp ||
        rawBounds.height() < MinBoundExtentForBooleanOp) {
      return effected;
    }
    if (strokeAlign == StrokeAlign::Inside) {
      effected.addPath(path, PathOp::Intersect);
    } else {
      effected.addPath(path, PathOp::Difference);
    }
    return effected;
  }
  if (pathEffect != nullptr) {
    pathEffect->filterPath(&path);
  }
  stroke->applyToPath(&path);
  return path;
}

}  // namespace tgfx
