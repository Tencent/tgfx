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

#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {
/**
 * Shape that applies stroke to another shape.
 */
class StrokeShape : public Shape {
 public:
  StrokeShape(std::shared_ptr<Shape> shape, const Stroke& stroke)
      : shape(std::move(shape)), stroke(stroke) {
  }

  PathFillType fillType() const override {
    return shape->fillType();
  }

  Rect onGetBounds() const override;

  static UniqueKey MakeUniqueKey(const UniqueKey& key, const Stroke& stroke);

  std::shared_ptr<Shape> shape = nullptr;
  Stroke stroke = {};

 protected:
  Type type() const override {
    return Type::Stroke;
  }

  UniqueKey getUniqueKey() const override;

  Path onGetPath(float resolutionScale) const override;

  friend class Shape;
};
}  // namespace tgfx
