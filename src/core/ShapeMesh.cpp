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

#include "ShapeMesh.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

std::shared_ptr<Mesh> ShapeMesh::Make(std::shared_ptr<Shape> shape, bool antiAlias) {
  if (shape == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ShapeMesh>(new ShapeMesh(std::move(shape), antiAlias));
}

ShapeMesh::ShapeMesh(std::shared_ptr<Shape> shape, bool antiAlias)
    : _shape(std::move(shape)), antiAlias(antiAlias) {
  _uniqueID = UniqueID::Next();
  _bounds = _shape->getBounds();
}

}  // namespace tgfx
