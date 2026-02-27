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
 * Base class for Mesh internal implementations.
 * Subclasses include VertexMeshImpl (user-provided vertex data) and ShapeMeshImpl (from
 * Path/Shape).
 */
class MeshImpl {
 public:
  enum class Type {
    Vertex,  // User-provided vertex data
    Shape,   // Constructed from Path/Shape
  };

  virtual ~MeshImpl() = default;

  /**
   * Returns the type of this mesh implementation.
   */
  virtual Type type() const = 0;

  /**
   * Returns true if this mesh has per-vertex coverage values for anti-aliasing.
   */
  virtual bool hasCoverage() const = 0;

  /**
   * Returns the bounding box of the mesh positions.
   */
  Rect bounds() const {
    return _bounds;
  }

  /**
   * Returns the globally unique identifier for this mesh instance.
   */
  uint32_t uniqueID() const {
    return _uniqueID;
  }

  /**
   * Returns the unique key for GPU resource caching.
   */
  UniqueKey getUniqueKey() const;

  /**
   * Returns the MeshImpl from a Mesh for internal read access.
   */
  static MeshImpl& ReadAccess(const Mesh& mesh);

 protected:
  Rect _bounds = {};
  uint32_t _uniqueID = 0;
};

}  // namespace tgfx
