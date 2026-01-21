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

#include "MeshDrawOp.h"
#include "gpu/processors/MeshGeometryProcessor.h"

namespace tgfx {

PlacementPtr<MeshDrawOp> MeshDrawOp::Make(std::shared_ptr<GPUMeshProxy> meshProxy, PMColor color,
                                          const Matrix& viewMatrix, AAType aaType) {
  if (meshProxy == nullptr) {
    return nullptr;
  }
  auto allocator = meshProxy->getContext()->drawingAllocator();
  return allocator->make<MeshDrawOp>(allocator, std::move(meshProxy), color, viewMatrix, aaType);
}

MeshDrawOp::MeshDrawOp(BlockAllocator* allocator, std::shared_ptr<GPUMeshProxy> meshProxy,
                       PMColor color, const Matrix& viewMatrix, AAType aaType)
    : DrawOp(allocator, aaType), meshProxy(std::move(meshProxy)), color(color),
      viewMatrix(viewMatrix) {
}

PlacementPtr<GeometryProcessor> MeshDrawOp::onMakeGeometryProcessor(RenderTarget* renderTarget) {
  const auto& impl = meshProxy->impl();
  return MeshGeometryProcessor::Make(allocator, impl.hasTexCoords(), impl.hasColors(), color,
                                     viewMatrix);
}

void MeshDrawOp::onDraw(RenderPass* renderPass) {
  auto vertexBuffer = meshProxy->getVertexBuffer();
  if (vertexBuffer == nullptr) {
    return;
  }

  renderPass->setVertexBuffer(0, vertexBuffer);

  const auto& impl = meshProxy->impl();
  auto primitiveType = (impl.topology() == MeshTopology::Triangles) ? PrimitiveType::Triangles
                                                                    : PrimitiveType::TriangleStrip;

  if (impl.hasIndices()) {
    auto indexBuffer = meshProxy->getIndexBuffer();
    if (indexBuffer == nullptr) {
      return;
    }
    renderPass->setIndexBuffer(indexBuffer, IndexFormat::UInt16);
    renderPass->drawIndexed(primitiveType, static_cast<uint32_t>(impl.indexCount()), 1, 0);
  } else {
    renderPass->draw(primitiveType, static_cast<uint32_t>(impl.vertexCount()));
  }
}

}  // namespace tgfx
