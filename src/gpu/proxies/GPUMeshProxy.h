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

#include "core/MeshBase.h"
#include "gpu/proxies/GPUBufferProxy.h"
#include "tgfx/core/Mesh.h"
#include "tgfx/gpu/GPUBuffer.h"

namespace tgfx {

class Context;

/**
 * Attributes for drawing a mesh on GPU.
 */
struct GPUMeshDrawAttributes {
  MeshTopology topology = MeshTopology::Triangles;
  bool hasTexCoords = false;
  bool hasColors = false;
  bool hasCoverage = false;
  bool hasIndices = false;
  int vertexCount = 0;  // For VertexMesh: fixed; For ShapeMesh: 0 (computed from buffer size)
  int indexCount = 0;

  /**
   * Creates draw attributes from a MeshBase.
   */
  static GPUMeshDrawAttributes Make(const MeshBase& mesh);
};

/**
 * Proxy for GPU mesh buffer resources.
 * Vertex and index data are stored in separate buffers.
 */
class GPUMeshProxy {
 public:
  GPUMeshProxy(Context* context, std::shared_ptr<Mesh> mesh, const GPUMeshDrawAttributes& attrs);

  Context* getContext() const {
    return _context;
  }

  const std::shared_ptr<Mesh>& mesh() const {
    return _mesh;
  }

  Rect bounds() const;

  const GPUMeshDrawAttributes& attributes() const {
    return _attributes;
  }

  void setVertexBufferProxy(std::shared_ptr<GPUBufferProxy> proxy) {
    vertexBufferProxy = std::move(proxy);
  }

  void setIndexBufferProxy(std::shared_ptr<GPUBufferProxy> proxy) {
    indexBufferProxy = std::move(proxy);
  }

  std::shared_ptr<GPUBuffer> getVertexBuffer() const;

  std::shared_ptr<GPUBuffer> getIndexBuffer() const;

  /**
   * Returns the actual vertex count for drawing.
   * For VertexMesh, returns the stored vertexCount.
   * For ShapeMesh, computes from vertex buffer size.
   */
  int getVertexCount() const;

 private:
  Context* _context = nullptr;
  std::shared_ptr<Mesh> _mesh = nullptr;
  GPUMeshDrawAttributes _attributes = {};
  std::shared_ptr<GPUBufferProxy> vertexBufferProxy = nullptr;
  std::shared_ptr<GPUBufferProxy> indexBufferProxy = nullptr;
};

}  // namespace tgfx
