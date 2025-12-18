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

#include "layers/contents/GeometryContent.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {

class TextContent : public GeometryContent {
 public:
  TextContent(std::shared_ptr<TextBlob> textBlob, const LayerPaint& paint);

  Rect getTightBounds(const Matrix& matrix) const override;
  bool hitTestPoint(float localX, float localY) const override;

  std::shared_ptr<TextBlob> textBlob = nullptr;

 protected:
  Type type() const override {
    return Type::Text;
  }

  Rect onGetBounds() const override;
  void onDraw(Canvas* canvas, const Paint& paint) const override;
  bool onHasSameGeometry(const GeometryContent* other) const override;
  std::optional<Path> onAsClipPath() const override;
};

}  // namespace tgfx
