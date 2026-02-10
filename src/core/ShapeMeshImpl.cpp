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

#include "ShapeMeshImpl.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

std::shared_ptr<Mesh> ShapeMeshImpl::Make(std::shared_ptr<Shape> shape, bool antiAlias) {
  if (shape == nullptr) {
    return nullptr;
  }
  auto impl = new ShapeMeshImpl(std::move(shape), antiAlias);
  return std::shared_ptr<Mesh>(new Mesh(std::unique_ptr<MeshImpl>(impl)));
}

ShapeMeshImpl::ShapeMeshImpl(std::shared_ptr<Shape> shape, bool antiAlias)
    : _shape(std::move(shape)), _uniqueID(UniqueID::Next()), antiAlias(antiAlias) {
  _bounds = _shape->getBounds();
}

UniqueKey ShapeMeshImpl::getUniqueKey() const {
  static const auto ShapeMeshDomain = UniqueKey::Make();
  return UniqueKey::Append(ShapeMeshDomain, &_uniqueID, 1);
}

}  // namespace tgfx
