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

#include "tgfx/core/Mesh.h"
#include "ShapeMeshImpl.h"
#include "VertexMeshImpl.h"
#include "tgfx/core/Shape.h"

namespace tgfx {

std::shared_ptr<Mesh> Mesh::MakeCopy(MeshTopology topology, int vertexCount, const Point* positions,
                                     const Color* colors, const Point* texCoords, int indexCount,
                                     const uint16_t* indices) {
  return VertexMeshImpl::Make(topology, vertexCount, positions, colors, texCoords, indexCount,
                              indices);
}

std::shared_ptr<Mesh> Mesh::MakeFromPath(Path path, bool antiAlias) {
  if (path.isEmpty()) {
    return nullptr;
  }
  auto shape = Shape::MakeFrom(std::move(path));
  return MakeFromShape(std::move(shape), antiAlias);
}

std::shared_ptr<Mesh> Mesh::MakeFromShape(std::shared_ptr<Shape> shape, bool antiAlias) {
  return ShapeMeshImpl::Make(std::move(shape), antiAlias);
}

Mesh::Mesh(MeshImpl* impl) : impl(impl) {
}

uint32_t Mesh::uniqueID() const {
  return impl->uniqueID();
}

Rect Mesh::bounds() const {
  return impl->bounds();
}

}  // namespace tgfx
