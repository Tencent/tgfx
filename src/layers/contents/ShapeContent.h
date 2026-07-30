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

#include "layers/contents/DrawContent.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/StrokeAlign.h"

namespace tgfx {

class ShapeContent : public DrawContent {
 public:
  ShapeContent(std::shared_ptr<Shape> shape, const LayerPaint& paint);

  Rect getBounds() const override;
  Rect getTightBounds(const Matrix& matrix) const override;
  bool hitTestPoint(float localX, float localY) const override;

  // Always the original user geometry, without any path effect or stroke expansion applied. The
  // path effect (e.g. dash) and stroke alignment are stored separately and applied at draw time.
  std::shared_ptr<Shape> shape = nullptr;
  std::shared_ptr<PathEffect> pathEffect = nullptr;
  StrokeAlign strokeAlign = StrokeAlign::Center;

 protected:
  Type type() const override {
    return Type::Shape;
  }

  Rect onGetBounds() const override;
  void onDraw(Canvas* canvas, const Paint& paint) const override;
  bool onHasSameGeometry(const GeometryContent* other) const override;

 private:
  // Cached `Shape::ApplyEffect(shape, pathEffect)` result when `pathEffect != nullptr`. Reused
  // across frames so the resulting EffectShape keeps a stable per-instance unique key and the GPU
  // triangulation/texture caches for the dashed geometry stay warm. Left null when `pathEffect`
  // is null (the raw `shape` is used directly).
  std::shared_ptr<Shape> effectedShape = nullptr;

  // Returns `effectedShape` when path effect is active, otherwise the raw `shape`. Used by both
  // the draw path and the expanded-stroke helper to share the same effect instance.
  const std::shared_ptr<Shape>& drawShape() const;

  // Whether the stroke needs a clip-based alignment (Inside/Outside with a non-zero stroke width).
  bool needsAlignmentClip() const;

  // Path used as clip mask for Inside/Outside alignment. Derived from `shape`: the raw path for
  // Inside, the inverse-fill version for Outside. `shape`'s Path is cached internally by Shape,
  // so this is cheap to recompute.
  Path getAlignmentClipPath() const;

  // Shape actually rasterized when the stroke is expanded via clip (Inside/Outside). It is
  // `ApplyStroke(drawShape(), stroke * 2)`. Note the width is doubled so the visible band after
  // clipping matches the requested stroke width.
  std::shared_ptr<Shape> getExpandedStrokeShape() const;

  // Path form of the stroke used by bounds/hit-test queries. For Center alignment this is the
  // natural 1x stroke; for Inside/Outside the width is doubled so that combining with the
  // original path (intersect / difference) yields a band whose visible half has the requested
  // stroke width. Path effects (e.g. dash) are applied before stroking.
  Path getStrokedPath() const;
};

}  // namespace tgfx
