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

#include <memory>
#include "tgfx/core/Color.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Point.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

class MeshImpl;
class Shape;

/**
 * Defines how vertices are organized into triangles.
 */
enum class MeshTopology {
  /**
   * Each 3 vertices/indices form an independent triangle.
   */
  Triangles,
  /**
   * Triangle strip, consecutive triangles sharing edges.
   */
  TriangleStrip,
};

/**
 * Mesh represents an immutable collection of triangles for GPU rendering.
 * Thread-safe and immutable once created.
 */
class Mesh {
 public:
  /**
   * Creates a Mesh by copying the provided vertex data.
   * @param topology How vertices are organized into triangles.
   * @param vertexCount Number of vertices (must be > 0, and must not exceed 65536 when using
   *                    indices since indices are 16-bit).
   * @param positions Vertex positions (required).
   * @param colors Per-vertex colors (optional).
   * @param texCoords Texture coordinates in pixel space (e.g., [0, imageWidth] x [0, imageHeight]),
   *                  with origin at top-left (optional).
   * @param indexCount Number of indices (0 if not indexed).
   * @param indices Index array (optional, uint16_t).
   * @return A shared pointer to the created Mesh, or nullptr if parameters are invalid.
   */
  static std::shared_ptr<Mesh> MakeCopy(MeshTopology topology, int vertexCount,
                                        const Point* positions, const Color* colors = nullptr,
                                        const Point* texCoords = nullptr, int indexCount = 0,
                                        const uint16_t* indices = nullptr);

  /**
   * Creates a Mesh from a Path. The mesh will be triangulated when first drawn.
   * GPU resources are persistently held until the Mesh is destroyed.
   * @param path The path to triangulate.
   * @param antiAlias If true, generates anti-aliased triangles with coverage values.
   * @return A shared pointer to the created Mesh, or nullptr if the path is empty.
   */
  static std::shared_ptr<Mesh> MakeFromPath(Path path, bool antiAlias = true);

  /**
   * Creates a Mesh from a Shape. The mesh will be triangulated when first drawn.
   * GPU resources are persistently held until the Mesh is destroyed.
   * @param shape The shape to triangulate.
   * @param antiAlias If true, generates anti-aliased triangles with coverage values.
   * @return A shared pointer to the created Mesh, or nullptr if the shape is nullptr.
   */
  static std::shared_ptr<Mesh> MakeFromShape(std::shared_ptr<Shape> shape, bool antiAlias = true);

  /**
   * Returns a globally unique identifier for this mesh instance.
   */
  uint32_t uniqueID() const;

  /**
   * Returns the bounding box of the mesh positions.
   */
  Rect bounds() const;

  ~Mesh();

 private:
  explicit Mesh(std::unique_ptr<MeshImpl> impl);

  std::unique_ptr<MeshImpl> impl;

  friend class MeshImpl;
  friend class VertexMeshImpl;
  friend class ShapeMeshImpl;
};

}  // namespace tgfx
