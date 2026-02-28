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

#include <mutex>
#include <unordered_map>
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/Mesh.h"

namespace tgfx {

class Context;

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

  uint32_t uniqueID() const override {
    return _uniqueID;
  }

  Rect bounds() const override {
    return _bounds;
  }

  virtual Type type() const = 0;

  virtual bool hasCoverage() const = 0;

  UniqueKey getUniqueKey() const;

  void retainGpuBuffer(uint32_t contextID, const UniqueKey& bufferKey);

  UniqueKey getBufferKey(uint32_t contextID) const;

 protected:
  Rect _bounds = {};
  uint32_t _uniqueID = 0;

  mutable std::mutex bufferKeysMutex;
  mutable std::unordered_map<uint32_t, UniqueKey> retainedBufferKeys;
};

}  // namespace tgfx
