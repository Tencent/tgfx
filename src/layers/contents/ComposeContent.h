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

#include <memory>
#include <vector>
#include "layers/contents/GeometryContent.h"

namespace tgfx {

/**
 * ComposeContent combines multiple GeometryContent objects into a single unit. It manages a list of
 * contents and supports separating them into default content (drawn below children) and foreground
 * content (drawn above children).
 */
class ComposeContent : public LayerContent {
 public:
  ComposeContent(std::vector<std::unique_ptr<GeometryContent>> contents,
                 size_t foregroundStartIndex, std::vector<GeometryContent*> contours);

  Rect getBounds() const override;
  Rect getTightBounds(const Matrix& matrix) const override;
  bool hitTestPoint(float localX, float localY) const override;
  void drawContour(Canvas* canvas, bool antiAlias) const override;
  bool contourEqualsOpaqueContent() const override;
  bool hasBlendMode() const override;
  bool drawDefault(Canvas* canvas, float alpha, bool antiAlias) const override;
  void drawForeground(Canvas* canvas, float alpha, bool antiAlias) const override;

 protected:
  Type type() const override {
    return Type::Compose;
  }

 private:
  std::vector<std::unique_ptr<GeometryContent>> contents = {};
  size_t foregroundStartIndex = 0;
  std::vector<GeometryContent*> contours = {};
};

}  // namespace tgfx
