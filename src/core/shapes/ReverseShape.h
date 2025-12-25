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

#include "core/shapes/UniqueKeyShape.h"

namespace tgfx {
class ReverseShape : public UniqueKeyShape {
 public:
  explicit ReverseShape(std::shared_ptr<Shape> shape) : shape(std::move(shape)) {
  }

  PathFillType fillType() const override {
    return shape->fillType();
  }

  Rect onGetBounds() const override {
    return shape->onGetBounds();
  }

 protected:
  Type type() const override {
    return Type::Reverse;
  }

  Path onGetPath(float resolutionScale) const override;

 private:
  std::shared_ptr<Shape> shape = nullptr;

  friend class Shape;
};
}  // namespace tgfx
