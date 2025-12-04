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
class ForegroundContent : public LayerContent {
 public:
  ForegroundContent(std::shared_ptr<Picture> background, std::shared_ptr<Picture> foreground)
      : background(std::move(background)), foreground(std::move(foreground)) {
  }

  Rect getBounds() const override;

  Rect getTightBounds(const Matrix& matrix) const override;

  bool hitTestPoint(float localX, float localY, bool shapeHitTest) const override;

  void drawDefault(Canvas* canvas, const BrushModifier* modifier) const override;

  void drawForeground(Canvas* canvas, const BrushModifier* modifier) const override;

  void drawContour(Canvas*, const BrushModifier* modifier) const override;

  std::shared_ptr<Picture> background = nullptr;
  std::shared_ptr<Picture> foreground = nullptr;

 protected:
  Type type() const override {
    return Type::Foreground;
  }
};
}  // namespace tgfx
