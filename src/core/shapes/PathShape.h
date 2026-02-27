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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * Shape that contains a single path.
 */
class PathShape : public Shape {
 public:
  explicit PathShape(Path path) : path(std::move(path)) {
  }

  bool isSimplePath() const override {
    return true;
  }

  PathFillType fillType() const override {
    return path.getFillType();
  }

  Rect onGetBounds() const override {
    return path.getBounds();
  }

  Path path = {};

 protected:
  Type type() const override {
    return Type::Path;
  }

  UniqueKey getUniqueKey() const override;

  Path onGetPath(float /*resolutionScale*/) const override {
    return path;
  }
};
}  // namespace tgfx
