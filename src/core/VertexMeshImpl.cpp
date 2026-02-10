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

#include "VertexMeshImpl.h"
#include "core/utils/UniqueID.h"

namespace tgfx {

static size_t CalculateDataSize(size_t vertexCount, size_t indexCount, bool hasTexCoords,
                                bool hasColors) {
  size_t size = sizeof(Point) * vertexCount;  // positions
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

std::shared_ptr<Mesh> VertexMeshImpl::Make(MeshTopology topology, int vertexCount,
                                           const Point* positions, const Color* colors,
                                           const Point* texCoords, int indexCount,
                                           const uint16_t* indices) {
  if (vertexCount <= 0 || positions == nullptr) {
    return nullptr;
  }
  if (indexCount < 0 || (indexCount > 0 && indices == nullptr)) {
    return nullptr;
  }

  const auto numVertices = static_cast<size_t>(vertexCount);
  const auto numIndices = static_cast<size_t>(indexCount);

  const auto dataSize =
      CalculateDataSize(numVertices, numIndices, texCoords != nullptr, colors != nullptr);
  const auto vertexData = ::operator new(dataSize, std::nothrow);
  if (vertexData == nullptr) {
    return nullptr;
  }

  auto impl = new VertexMeshImpl(topology, vertexCount, indexCount);
  impl->vertexData = vertexData;

  auto ptr = static_cast<uint8_t*>(vertexData);
  impl->_positions = reinterpret_cast<Point*>(ptr);
  std::memcpy(impl->_positions, positions, sizeof(Point) * numVertices);
  ptr += sizeof(Point) * numVertices;

  if (texCoords != nullptr) {
    impl->_texCoords = reinterpret_cast<Point*>(ptr);
    std::memcpy(impl->_texCoords, texCoords, sizeof(Point) * numVertices);
    ptr += sizeof(Point) * numVertices;
  }

  if (colors != nullptr) {
    impl->_colors = reinterpret_cast<Color*>(ptr);
    std::memcpy(impl->_colors, colors, sizeof(Color) * numVertices);
    ptr += sizeof(Color) * numVertices;
  }

  if (indexCount > 0) {
    impl->_indices = reinterpret_cast<uint16_t*>(ptr);
    std::memcpy(impl->_indices, indices, sizeof(uint16_t) * numIndices);
  }

  impl->_bounds.setBounds(impl->_positions, impl->_vertexCount);

  return std::shared_ptr<Mesh>(new Mesh(std::unique_ptr<MeshImpl>(impl)));
}

VertexMeshImpl::VertexMeshImpl(MeshTopology topology, int vertexCount, int indexCount)
    : _topology(topology), _vertexCount(vertexCount), _indexCount(indexCount),
      _uniqueID(UniqueID::Next()) {
}

VertexMeshImpl::~VertexMeshImpl() {
  releaseVertexData();
}

UniqueKey VertexMeshImpl::getUniqueKey() const {
  static const auto MeshDomain = UniqueKey::Make();
  return UniqueKey::Append(MeshDomain, &_uniqueID, 1);
}

size_t VertexMeshImpl::getVertexStride() const {
  size_t stride = sizeof(float) * 2;  // position.xy
  if (_texCoords != nullptr) {
    stride += sizeof(float) * 2;  // texCoord.xy
  }
  if (_colors != nullptr) {
    stride += sizeof(uint8_t) * 4;  // color.rgba (UByte4Normalized)
  }
  return stride;
}

void VertexMeshImpl::releaseVertexData() {
  if (vertexData != nullptr) {
    ::operator delete(vertexData);
    vertexData = nullptr;
    _positions = nullptr;
    _texCoords = nullptr;
    _colors = nullptr;
    _indices = nullptr;
  }
}

}  // namespace tgfx
