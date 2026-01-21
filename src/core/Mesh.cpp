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
#include "MeshImpl.h"

namespace tgfx {

std::shared_ptr<Mesh> Mesh::MakeCopy(MeshTopology topology, int vertexCount, const Point* positions,
                                     const Color* colors, const Point* texCoords, int indexCount,
                                     const uint16_t* indices) {
  return MeshImpl::Make(topology, vertexCount, positions, colors, texCoords, indexCount, indices);
}

Mesh::Mesh(MeshImpl* impl) : impl(impl) {
}

Mesh::~Mesh() = default;

uint32_t Mesh::uniqueID() const {
  return impl->uniqueID();
}

Rect Mesh::bounds() const {
  return impl->bounds();
}

}  // namespace tgfx
