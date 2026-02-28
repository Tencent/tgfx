/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "MeshBase.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

/**
 * Mesh implementation constructed from Path/Shape.
 * Triangulation is performed asynchronously during GPU upload via ShapeVertexSource.
 * Shape is retained for multi-Context support.
 */
class ShapeMesh : public MeshBase {
 public:
  static std::shared_ptr<Mesh> Make(std::shared_ptr<Shape> shape, bool antiAlias);

  Type type() const override {
    return Type::Shape;
  }

  bool hasCoverage() const override {
    return antiAlias;
  }

  std::shared_ptr<Shape> shape() const {
    return _shape;
  }

  bool isAntiAlias() const {
    return antiAlias;
  }

 private:
  ShapeMesh(std::shared_ptr<Shape> shape, bool antiAlias);

  std::shared_ptr<Shape> _shape = nullptr;
  bool antiAlias = true;
};

}  // namespace tgfx
