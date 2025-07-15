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

#include "core/utils/Log.h"
#include "layers/contents/LayerContent.h"

namespace tgfx {
class DefaultContent : public LayerContent {
 public:
  explicit DefaultContent(std::shared_ptr<Picture> content) : content(std::move(content)) {
  }

  Rect getBounds() const override {
    return content->getBounds();
  }

  Rect getTightBounds(const Matrix& matrix) const override {
    return content->getTightBounds(&matrix);
  }

  bool hitTestPoint(float localX, float localY, bool shapeHitTest) const override {
    return content->hitTestPoint(localX, localY, shapeHitTest);
  }

  void drawDefault(Canvas* canvas, const FillModifier* modifier) const override {
    content->playback(canvas, modifier);
  }

  void drawForeground(Canvas*, const FillModifier*) const override {
  }

  void drawContour(Canvas* canvas, const FillModifier* modifier) const override {
    content->playback(canvas, modifier);
  }

  std::shared_ptr<Picture> content = nullptr;

 protected:
  Type type() const override {
    return Type::Default;
  }
};
}  // namespace tgfx
