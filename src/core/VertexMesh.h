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

namespace tgfx {

/**
 * Mesh implementation with user-provided vertex data.
 * CPU data is retained for multi-Context support.
 */
class VertexMesh : public MeshBase {
 public:
  static std::shared_ptr<Mesh> Make(MeshTopology topology, int vertexCount, const Point* positions,
                                    const Color* colors, const Point* texCoords, int indexCount,
                                    const uint16_t* indices);

  ~VertexMesh() override;

  Type type() const override {
    return Type::Vertex;
  }

  bool hasCoverage() const override {
    return false;
  }

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
    return _texCoords != nullptr;
  }

  bool hasColors() const {
    return _colors != nullptr;
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

  /**
   * Returns the stride of interleaved vertex data in bytes.
   * Layout: [position.x, position.y][texCoord.x, texCoord.y (opt)][color.rgba (opt)]
   */
  size_t getVertexStride() const;

 private:
  VertexMesh(MeshTopology topology, int vertexCount, int indexCount);

  MeshTopology _topology = MeshTopology::Triangles;
  int _vertexCount = 0;
  int _indexCount = 0;

  // CPU data, allocated as a single block, each pointer points to internal positions
  // Memory layout: [positions][texCoords (opt)][colors (opt)][indices (opt)]
  void* vertexData = nullptr;
  Point* _positions = nullptr;
  Point* _texCoords = nullptr;
  Color* _colors = nullptr;
  uint16_t* _indices = nullptr;
};

}  // namespace tgfx
