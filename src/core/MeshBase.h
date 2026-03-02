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
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Mesh.h"

namespace tgfx {

/**
 * Internal base class for Mesh implementations.
 * Provides shared functionality for VertexMesh and ShapeMesh.
 */
class MeshBase : public Mesh {
 public:
  enum class Type {
    Vertex,  // User-provided vertex data
    Shape,   // Constructed from Path/Shape
  };

  Rect bounds() const override {
    return _bounds;
  }

  virtual Type type() const = 0;

  virtual bool hasCoverage() const = 0;

  virtual bool hitTestPoint(float x, float y) const {
    return _bounds.contains(x, y);
  }

  virtual Rect getTightBounds(const Matrix& matrix) const {
    return matrix.mapRect(_bounds);
  }

  /**
   * Returns the unique key for this mesh. All GPU buffers derived from this mesh share the same
   * UniqueKey domain, so holding this key protects all related buffers from LRU eviction.
   */
  const UniqueKey& getUniqueKey() const {
    return uniqueKey;
  }

 protected:
  Rect _bounds = {};
  UniqueKey uniqueKey = UniqueKey::Make();
};

}  // namespace tgfx
