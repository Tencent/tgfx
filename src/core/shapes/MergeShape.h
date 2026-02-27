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

namespace tgfx {
/**
 * Shape that merges multiple shapes together.
 */
class MergeShape : public UniqueKeyShape {
 public:
  MergeShape(std::shared_ptr<Shape> first, std::shared_ptr<Shape> second, PathOp pathOp)
      : first(std::move(first)), second(std::move(second)), pathOp(pathOp) {
  }

  PathFillType fillType() const override;

  Rect onGetBounds() const override;

  std::shared_ptr<Shape> first = nullptr;
  std::shared_ptr<Shape> second = nullptr;
  PathOp pathOp = PathOp::Append;

 protected:
  Type type() const override {
    return Type::Merge;
  }

  Path onGetPath(float resolutionScale) const override;
};
}  // namespace tgfx
