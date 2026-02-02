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

namespace tgfx {

class ShapeContent : public DrawContent {
 public:
  ShapeContent(std::shared_ptr<Shape> shape, const LayerPaint& paint);

  Rect getTightBounds(const Matrix& matrix, const Stroke* stroke) const override;
  bool hitTestPoint(float localX, float localY, const Stroke* stroke) const override;
  // Shape may contain sharp corners that require miter limit consideration.
  bool mayHaveSharpCorners() const override {
    return true;
  }

  std::shared_ptr<Shape> shape = nullptr;

 protected:
  Type type() const override {
    return Type::Shape;
  }

  Rect onGetBounds() const override;
  void onDraw(Canvas* canvas, const Paint& paint) const override;
  bool onHasSameGeometry(const GeometryContent* other) const override;

 private:
  Path getFilledPath(const Stroke* stroke) const;
};

}  // namespace tgfx
