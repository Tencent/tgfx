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

#include "MeshImpl.h"
#include <new>
#include "core/utils/UniqueID.h"

namespace tgfx {

static size_t CalculateMemorySize(size_t vertexCount, size_t indexCount, bool hasTexCoords,
                                  bool hasColors) {
  size_t size = sizeof(MeshImpl);
  size += sizeof(Point) * vertexCount;  // positions
  if (hasTexCoords) {
    size += sizeof(Point) * vertexCount;  // texCoords
  }
  if (hasColors) {
    size += sizeof(Color) * vertexCount;  // colors
  }
  if (indexCount > 0) {
    size += sizeof(uint16_t) * indexCount;  // indices
  }
  return size;
}

std::shared_ptr<Mesh> MeshImpl::Make(MeshTopology topology, int vertexCount, const Point* positions,
                                     const Color* colors, const Point* texCoords, int indexCount,
                                     const uint16_t* indices) {
  if (vertexCount <= 0 || positions == nullptr) {
    return nullptr;
  }
  if (indexCount < 0 || (indexCount > 0 && indices == nullptr)) {
    return nullptr;
  }

  const auto hasTexCoords = texCoords != nullptr;
  const auto hasColors = colors != nullptr;
  const auto numVertices = static_cast<size_t>(vertexCount);
  const auto numIndices = static_cast<size_t>(indexCount);

  auto memorySize = CalculateMemorySize(numVertices, numIndices, hasTexCoords, hasColors);
  auto memory = ::operator new(memorySize, std::nothrow);
  if (memory == nullptr) {
    return nullptr;
  }

  auto ptr = static_cast<uint8_t*>(memory);
  auto impl = new (ptr) MeshImpl(topology, vertexCount, indexCount, hasTexCoords, hasColors);
  ptr += sizeof(MeshImpl);

  impl->_positions = reinterpret_cast<Point*>(ptr);
  std::memcpy(impl->_positions, positions, sizeof(Point) * numVertices);
  ptr += sizeof(Point) * numVertices;

  if (hasTexCoords) {
    impl->_texCoords = reinterpret_cast<Point*>(ptr);
    std::memcpy(impl->_texCoords, texCoords, sizeof(Point) * numVertices);
    ptr += sizeof(Point) * numVertices;
  }

  if (hasColors) {
    impl->_colors = reinterpret_cast<Color*>(ptr);
    std::memcpy(impl->_colors, colors, sizeof(Color) * numVertices);
    ptr += sizeof(Color) * numVertices;
  }

  if (indexCount > 0) {
    impl->_indices = reinterpret_cast<uint16_t*>(ptr);
    std::memcpy(impl->_indices, indices, sizeof(uint16_t) * numIndices);
  }

  impl->_bounds.setBounds(impl->_positions, impl->_vertexCount);

  auto deleter = [memory](Mesh* mesh) {
    auto meshImpl = mesh->impl;
    mesh->~Mesh();
    meshImpl->~MeshImpl();
    ::operator delete(memory);
  };

  return std::shared_ptr<Mesh>(new Mesh(impl), deleter);
}

const MeshImpl& MeshImpl::ReadAccess(const Mesh& mesh) {
  return *mesh.impl;
}

UniqueKey MeshImpl::getUniqueKey() const {
  static const auto MeshDomain = UniqueKey::Make();
  return UniqueKey::Append(MeshDomain, &_uniqueID, 1);
}

MeshImpl::MeshImpl(MeshTopology topology, int vertexCount, int indexCount, bool hasTexCoords,
                   bool hasColors)
    : _topology(topology), _vertexCount(vertexCount), _indexCount(indexCount),
      _hasTexCoords(hasTexCoords), _hasColors(hasColors), _uniqueID(UniqueID::Next()) {
}

size_t MeshImpl::getVertexStride() const {
  size_t stride = sizeof(float) * 2;  // position.xy
  if (_hasTexCoords) {
    stride += sizeof(float) * 2;  // texCoord.xy
  }
  if (_hasColors) {
    stride += sizeof(uint8_t) * 4;  // color.rgba (UByte4Normalized)
  }
  return stride;
}

}  // namespace tgfx
