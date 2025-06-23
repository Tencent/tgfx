/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Shape.h"
#include "tgfx/layers/LayerContent.h"

namespace tgfx {
struct ShapePaint {
  ShapePaint(std::shared_ptr<Shader> shader, float alpha, BlendMode blendMode)
      : shader(std::move(shader)), alpha(alpha), blendMode(blendMode) {
  }

  std::shared_ptr<Shader> shader = nullptr;
  float alpha = 1.0f;
  BlendMode blendMode = BlendMode::SrcOver;
};

class ShapeContent : public LayerContent {
 public:
  ShapeContent(std::shared_ptr<Shape> fill, std::shared_ptr<Shape> stroke,
               std::vector<ShapePaint> paintList, size_t fillPaintCount);

  Rect getBounds() const override {
    return bounds;
  }

  Rect getTightBounds(const Matrix& matrix) const override;

  void draw(Canvas* canvas, const Paint& paint) const override;

  bool drawFills(Canvas* canvas, const Paint& paint, bool forContour) const;

  bool drawStrokes(Canvas* canvas, const Paint& paint, bool forContour) const;

  bool hitTestPoint(float localX, float localY, bool shapeHitTest) override;

  Rect bounds = {};
  std::shared_ptr<Shape> fillShape = nullptr;
  std::shared_ptr<Shape> strokeShape = nullptr;
  std::vector<ShapePaint> paintList = {};
  size_t fillPaintCount = 0;

 protected:
  Type type() const override {
    return Type::ShapeContent;
  }
};
}  // namespace tgfx
