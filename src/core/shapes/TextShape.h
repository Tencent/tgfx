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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/TextBlob.h"

namespace tgfx {
/**
 * Shape that contains a TextBlob.
 */
class TextShape : public UniqueKeyShape {
 public:
  explicit TextShape(std::shared_ptr<TextBlob> textBlob) : textBlob(std::move(textBlob)) {
  }

  PathFillType fillType() const override {
    return PathFillType::Winding;
  }

  Rect onGetBounds() const override;

 protected:
  Type type() const override {
    return Type::Text;
  }

  Path onGetPath(float resolutionScale) const override;

 private:
  std::shared_ptr<TextBlob> textBlob = nullptr;
};
}  // namespace tgfx
