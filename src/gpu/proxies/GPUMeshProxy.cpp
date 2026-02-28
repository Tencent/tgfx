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

#include "GPUMeshProxy.h"
#include "core/PathTriangulator.h"
#include "core/VertexMesh.h"

namespace tgfx {

GPUMeshDrawAttributes GPUMeshDrawAttributes::Make(const MeshBase& mesh) {
  GPUMeshDrawAttributes attrs = {};
  attrs.hasCoverage = mesh.hasCoverage();

  if (mesh.type() == MeshBase::Type::Vertex) {
    auto& vertexMesh = static_cast<const VertexMesh&>(mesh);
    attrs.topology = vertexMesh.topology();
    attrs.hasTexCoords = vertexMesh.hasTexCoords();
    attrs.hasColors = vertexMesh.hasColors();
    attrs.hasIndices = vertexMesh.hasIndices();
    attrs.vertexCount = vertexMesh.vertexCount();
    attrs.indexCount = vertexMesh.indexCount();
  } else {
    // ShapeMesh: always triangles, no texCoords/colors/indices
    // vertexCount will be determined after triangulation (computed from buffer size)
    attrs.topology = MeshTopology::Triangles;
    attrs.hasTexCoords = false;
    attrs.hasColors = false;
    attrs.hasIndices = false;
    attrs.vertexCount = 0;
    attrs.indexCount = 0;
  }

  return attrs;
}

GPUMeshProxy::GPUMeshProxy(Context* context, std::shared_ptr<Mesh> mesh,
                           const GPUMeshDrawAttributes& attrs)
    : _context(context), _mesh(std::move(mesh)), _attributes(attrs) {
}

Rect GPUMeshProxy::bounds() const {
  return _mesh ? _mesh->bounds() : Rect::MakeEmpty();
}

static std::shared_ptr<GPUBuffer> GetGPUBuffer(const std::shared_ptr<GPUBufferProxy>& proxy) {
  if (proxy == nullptr) {
    return nullptr;
  }
  auto bufferResource = proxy->getBuffer();
  if (bufferResource == nullptr) {
    return nullptr;
  }
  return bufferResource->gpuBuffer();
}

std::shared_ptr<GPUBuffer> GPUMeshProxy::getVertexBuffer() const {
  return GetGPUBuffer(vertexBufferProxy);
}

std::shared_ptr<GPUBuffer> GPUMeshProxy::getIndexBuffer() const {
  return GetGPUBuffer(indexBufferProxy);
}

int GPUMeshProxy::getVertexCount() const {
  // For VertexMesh, use stored vertex count
  if (_attributes.vertexCount > 0) {
    return _attributes.vertexCount;
  }

  // For ShapeMesh, compute from buffer size
  auto bufferResource = vertexBufferProxy ? vertexBufferProxy->getBuffer() : nullptr;
  if (bufferResource == nullptr) {
    return 0;
  }

  auto bufferSize = bufferResource->size();
  size_t vertexCount = 0;
  if (_attributes.hasCoverage) {
    vertexCount = PathTriangulator::GetAAVertexCount(bufferSize);
  } else {
    vertexCount = PathTriangulator::GetNonAAVertexCount(bufferSize);
  }
  return static_cast<int>(vertexCount);
}

}  // namespace tgfx
