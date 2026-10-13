/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "core/shapes/UniqueKeyShape.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {
/**
 * Shape that contains a TextBlob.
 */
class TextShape : public UniqueKeyShape {
 public:
  explicit TextShape(std::shared_ptr<TextBlob> textBlob);

  PathFillType fillType() const override {
    // Must agree with the fill type onGetPath() reports. Resolved once in the constructor because
    // this is queried on every fill and onGetPath() asks for it again.
    return evenOddFill ? PathFillType::EvenOdd : PathFillType::Winding;
  }

  Rect onGetBounds() const override;

 protected:
  Type type() const override {
    return Type::Text;
  }

  Path onGetPath(float resolutionScale) const override;

 private:
  std::shared_ptr<TextBlob> textBlob = nullptr;
  // The blob cannot change after construction, so the aggregated fill rule is fixed as well.
  bool evenOddFill = false;
};
}  // namespace tgfx
