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

namespace tgfx {
/**
 * Shape that appends multiple shapes together.
 */
class AppendShape : public UniqueKeyShape {
 public:
  static std::shared_ptr<Shape> MakeFrom(std::shared_ptr<Shape> first,
                                         std::shared_ptr<Shape> second);

  PathFillType fillType() const override;

  Rect onGetBounds() const override;

 protected:
  Type type() const override {
    return Type::Append;
  }

  Path onGetPath(float resolutionScale) const override;

 private:
  std::vector<std::shared_ptr<Shape>> shapes = {};

  static void Append(std::vector<std::shared_ptr<Shape>>* shapes, std::shared_ptr<Shape> shape);

  explicit AppendShape(std::vector<std::shared_ptr<Shape>> shapes) : shapes(std::move(shapes)) {
  }

  friend class Shape;
};
}  // namespace tgfx
