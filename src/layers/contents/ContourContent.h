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

#include "layers/contents/LayerContent.h"

namespace tgfx {
class ContourContent : public LayerContent {
 public:
  ContourContent(std::unique_ptr<LayerContent> content, std::shared_ptr<Picture> contour)
      : content(std::move(content)), contour(std::move(contour)) {
  }

  Rect getBounds() const override {
    return content->getBounds();
  }

  Rect getTightBounds(const Matrix& matrix) const override {
    return content->getTightBounds(matrix);
  }

  bool hitTestPoint(float localX, float localY, bool shapeHitTest) const override {
    return content->hitTestPoint(localX, localY, shapeHitTest);
  }

  void drawDefault(Canvas* canvas, const BrushModifier* modifier) const override {
    content->drawDefault(canvas, modifier);
  }

  void drawForeground(Canvas* canvas, const BrushModifier* modifier) const override {
    content->drawForeground(canvas, modifier);
  }

  void drawContour(Canvas* canvas, const BrushModifier* modifier) const override {
    contour->playback(canvas, modifier);
  }

  std::unique_ptr<LayerContent> content = nullptr;
  std::shared_ptr<Picture> contour = nullptr;

 protected:
  Type type() const override {
    return Type::Contour;
  }
};
}  // namespace tgfx
