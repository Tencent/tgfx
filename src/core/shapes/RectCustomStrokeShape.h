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

#include "gpu/ResourceKey.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/layers/ShapeLayer.h"

namespace tgfx {
/**
 * RectCustomStrokeShape is a specialized Shape that applies a stroke to a rectangle shape.
 * It is designed to handle custom rectangle shapes with specific stroke weight. If the input path
 * is not a rectangle, the original path is returned unchanged
 */
class RectCustomStrokeShape final : public Shape {
 public:
  RectCustomStrokeShape(std::shared_ptr<Shape> rectShape, const Stroke& stroke);

  bool isInverseFillType() const override {
    return shape->isInverseFillType();
  }

  Rect getBounds() const override;

  Path getPath() const override;

  void setStrokeAlign(StrokeAlign strokeAlign) {
    _strokeAlign = strokeAlign;
  }

  void setLineDashPattern(const std::vector<float>& pattern) {
    _lineDashPattern = pattern;
  }

  void setCornerRadii(const std::array<float, 4>& radii);

  void setBorderWeights(const std::array<float, 4>& borderWeights);

 protected:
  Type type() const override {
    return Type::CustomStroke;
  }

  UniqueKey getUniqueKey() const override;

 private:
  void ApplyRectStroke(const Rect& rect, Path* path) const;

  void ApplyRRectStroke(const Rect& rect, Path* path) const;

  void ApplyRectInsideStroke(const Rect& rect, Path* path) const;

  void ApplyRectOutsideStroke(const Rect& rect, Path* path) const;

  void ApplyRectCenterStroke(const Rect& rect, Path* path) const;

  void ApplyRRectInsideStroke(const Rect& rect, Path* path) const;

  void ApplyRRectOutsideStroke(const Rect& rect, Path* path) const;

  void ApplyRRectCenterStroke(const Rect& rect, Path* path) const;

  std::shared_ptr<Shape> shape = nullptr;
  Stroke stroke = {};
  StrokeAlign _strokeAlign = StrokeAlign::Center;
  std::array<float, 4> _borderWeights = {};
  std::array<float, 4> _radii = {};
  std::vector<float> _lineDashPattern = {};

  friend class Shape;
};
}  // namespace tgfx
