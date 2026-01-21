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

#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Mesh.h"

namespace tgfx {

/**
 * Internal implementation of Mesh.
 * Memory layout: [MeshImpl class][positions][texCoords (optional)][colors (optional)][indices (optional)]
 */
class MeshImpl {
 public:
  /**
   * Creates a Mesh with the given vertex data. All data is copied into a single contiguous buffer.
   */
  static std::shared_ptr<Mesh> Make(MeshTopology topology, int vertexCount, const Point* positions,
                                    const Color* colors, const Point* texCoords, int indexCount,
                                    const uint16_t* indices);

  /**
   * Returns the MeshImpl from a Mesh for internal read access.
   */
  static const MeshImpl& ReadAccess(const Mesh& mesh);

  MeshTopology topology() const {
    return _topology;
  }

  int vertexCount() const {
    return _vertexCount;
  }

  int indexCount() const {
    return _indexCount;
  }

  bool hasTexCoords() const {
    return _hasTexCoords;
  }

  bool hasColors() const {
    return _hasColors;
  }

  bool hasIndices() const {
    return _indexCount > 0;
  }

  const Point* positions() const {
    return _positions;
  }

  const Point* texCoords() const {
    return _texCoords;
  }

  const Color* colors() const {
    return _colors;
  }

  const uint16_t* indices() const {
    return _indices;
  }

  const Rect& bounds() const {
    return _bounds;
  }

  uint32_t uniqueID() const {
    return _uniqueID;
  }

  /**
   * Returns the unique key for GPU resource caching.
   */
  UniqueKey getUniqueKey() const;

  /**
   * Returns the stride of interleaved vertex data in bytes.
   * Layout: [position.x, position.y][texCoord.x, texCoord.y (opt)][color.rgba (opt)]
   */
  size_t getVertexStride() const;

 private:
  MeshImpl(MeshTopology topology, int vertexCount, int indexCount, bool hasTexCoords,
           bool hasColors);

  MeshTopology _topology = MeshTopology::Triangles;
  int _vertexCount = 0;
  int _indexCount = 0;
  bool _hasTexCoords = false;
  bool _hasColors = false;
  uint32_t _uniqueID = 0;
  Rect _bounds = {};

  Point* _positions = nullptr;
  Point* _texCoords = nullptr;
  Color* _colors = nullptr;
  uint16_t* _indices = nullptr;
};

}  // namespace tgfx
