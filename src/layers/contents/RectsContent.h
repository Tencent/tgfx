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

#include <vector>
#include "layers/contents/GeometryContent.h"

namespace tgfx {

class RectsContent : public GeometryContent {
 public:
  RectsContent(std::vector<Rect> rects, const LayerPaint& paint);

  Rect getTightBounds(const Matrix& matrix) const override;
  bool hitTestPoint(float localX, float localY) const override;
  std::optional<Rect> getContourOpaqueRect() const override;

  std::vector<Rect> rects = {};

 protected:
  Type type() const override {
    return Type::Rects;
  }

  Rect onGetBounds() const override;
  void onDraw(Canvas* canvas, const Paint& paint) const override;
  bool onHasSameGeometry(const GeometryContent* other) const override;

 private:
  Path getFilledPath() const;
};

}  // namespace tgfx
