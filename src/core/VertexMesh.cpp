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

#include "VertexMesh.h"
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

std::shared_ptr<Mesh> VertexMesh::Make(MeshTopology topology, int vertexCount,
                                       const Point* positions, const Color* colors,
                                       const Point* texCoords, int indexCount,
                                       const uint16_t* indices) {
  if (vertexCount <= 0 || positions == nullptr) {
    return nullptr;
  }
  // Indices are 16-bit, so vertexCount must not exceed 65536 when using indices.
  if (indexCount < 0 || (indexCount > 0 && (indices == nullptr || vertexCount > 65536))) {
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

  auto mesh = std::shared_ptr<VertexMesh>(new VertexMesh(topology, vertexCount, indexCount));
  mesh->vertexData = vertexData;

  auto ptr = static_cast<uint8_t*>(vertexData);
  mesh->_positions = reinterpret_cast<Point*>(ptr);
  std::memcpy(mesh->_positions, positions, sizeof(Point) * numVertices);
  ptr += sizeof(Point) * numVertices;

  if (texCoords != nullptr) {
    mesh->_texCoords = reinterpret_cast<Point*>(ptr);
    std::memcpy(mesh->_texCoords, texCoords, sizeof(Point) * numVertices);
    ptr += sizeof(Point) * numVertices;
  }

  if (colors != nullptr) {
    mesh->_colors = reinterpret_cast<Color*>(ptr);
    std::memcpy(mesh->_colors, colors, sizeof(Color) * numVertices);
    ptr += sizeof(Color) * numVertices;
  }

  if (indexCount > 0) {
    mesh->_indices = reinterpret_cast<uint16_t*>(ptr);
    std::memcpy(mesh->_indices, indices, sizeof(uint16_t) * numIndices);
  }

  mesh->_bounds.setBounds(mesh->_positions, mesh->_vertexCount);

  return mesh;
}

VertexMesh::VertexMesh(MeshTopology topology, int vertexCount, int indexCount)
    : _topology(topology), _vertexCount(vertexCount), _indexCount(indexCount) {
  _uniqueID = UniqueID::Next();
}

VertexMesh::~VertexMesh() {
  if (vertexData != nullptr) {
    ::operator delete(vertexData);
  }
}

size_t VertexMesh::getVertexStride() const {
  size_t stride = sizeof(float) * 2;  // position.xy
  if (_texCoords != nullptr) {
    stride += sizeof(float) * 2;  // texCoord.xy
  }
  if (_colors != nullptr) {
    stride += sizeof(uint8_t) * 4;  // color.rgba (UByte4Normalized)
  }
  return stride;
}

}  // namespace tgfx
