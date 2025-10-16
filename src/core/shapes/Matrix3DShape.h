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

#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Matrix3D.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

/**
 * Shape that applies a 3d matrix transformation to another Shape.
 */
class Matrix3DShape : public Shape {
 public:
  Matrix3DShape(std::shared_ptr<Shape> shape, const Matrix3D& matrix)
      : shape(std::move(shape)), matrix(matrix) {
  }

  bool isInverseFillType() const override {
    return shape->isInverseFillType();
  }

  Rect getBounds() const override;

  static UniqueKey MakeUniqueKey(const UniqueKey& key, const Matrix3D& matrix);

  std::shared_ptr<Shape> shape = nullptr;

  Matrix3D matrix = {};

 protected:
  Type type() const override {
    return Type::Matrix3D;
  }

  Path onGetPath(float resolutionScale) const override;

  UniqueKey getUniqueKey() const override;
};

}  // namespace tgfx