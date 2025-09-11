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

#include <optional>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/Stroke.h"

namespace tgfx {

class DeferredShapeInfo {
 public:
  static std::shared_ptr<DeferredShapeInfo> Make(std::shared_ptr<Shape> shape, const Stroke* stroke,
                                                 Matrix matrix);

  ~DeferredShapeInfo() = default;

  void applyMatrix(const Matrix& m);

  Matrix matrix() const;

  void setMatrix(const Matrix& m);

  std::shared_ptr<const Shape> shape() const;

  Rect getBounds() const;

  UniqueKey getUniqueKey() const;

  Path getPath() const;

 private:
  DeferredShapeInfo(std::shared_ptr<Shape> shape, const Stroke* inputStroke, Matrix matrix);

  std::shared_ptr<Shape> _shape = nullptr;
  std::optional<Stroke> stroke;
  Matrix _matrix = Matrix::I();
};

}  // namespace tgfx