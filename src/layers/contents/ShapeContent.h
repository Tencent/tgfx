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
  // Whether the stroke needs a clip-based alignment (Inside/Outside with a non-zero stroke width).
  bool needsAlignmentClip() const;

  // Path used as clip mask for Inside/Outside alignment. Derived from `shape`: the raw path for
  // Inside, the inverse-fill version for Outside. `shape`'s Path is cached internally by Shape,
  // so this is cheap to recompute.
  Path getAlignmentClipPath() const;

  // Shape actually rasterized when the stroke is expanded via clip (Inside/Outside). It is
  // `ApplyStroke(ApplyEffect(shape, pathEffect), stroke * 2)`. Note the width is doubled so the
  // visible band after clipping matches the requested stroke width.
  std::shared_ptr<Shape> getExpandedStrokeShape() const;

  // Path used by bounds/hit-test queries. For fill or Center-align stroke it is the naturally
  // stroked path; for Inside/Outside it is the expanded-stroke path intersected with the raw
  // path region (or the outside-of-path region for Outside).
  Path getFilledPath() const;
};

}  // namespace tgfx
