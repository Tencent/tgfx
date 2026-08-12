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

#include "MeshBufferUploadTask.h"
#include "core/ColorSpaceXformSteps.h"
#include "core/VertexMesh.h"
#include "core/utils/ColorHelper.h"
#include "core/utils/ColorSpaceHelper.h"
#include "gpu/resources/BufferResource.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

VertexMeshBufferUploadTask::VertexMeshBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                                       std::shared_ptr<GPUMeshProxy> meshProxy,
                                                       std::shared_ptr<ColorSpace> dstColorSpace)
    : ResourceTask(std::move(proxy)), meshProxy(std::move(meshProxy)),
      dstColorSpace(std::move(dstColorSpace)) {
}

std::shared_ptr<Resource> VertexMeshBufferUploadTask::onMakeResource(Context* context) {
  auto meshBase = static_cast<MeshBase*>(meshProxy->mesh().get());
  if (meshBase == nullptr || meshBase->type() != MeshBase::Type::Vertex) {
    return nullptr;
  }
  auto vertexMesh = static_cast<VertexMesh*>(meshBase);
  // VertexMesh::Make() guarantees positions != nullptr
  DEBUG_ASSERT(vertexMesh->positions() != nullptr);

  // The buffer follows the chain's rect-layout attribute order: position, coverage, texCoord,
  // color. Vertex meshes carry no real coverage data, so a constant 1.0f slot is written whenever
  // texCoords or colors are present (matching MeshGeometryProcessor's attribute registration).
  bool withCoverage = vertexMesh->hasTexCoords() || vertexMesh->hasColors();
  size_t stride = sizeof(float) * 2;  // position.xy
  if (withCoverage) {
    stride += sizeof(float);
  }
  if (vertexMesh->hasTexCoords()) {
    stride += sizeof(float) * 2;
  }
  if (vertexMesh->hasColors()) {
    stride += sizeof(uint8_t) * 4;
  }
  size_t vertexDataSize = stride * static_cast<size_t>(vertexMesh->vertexCount());

  // Create color space transform steps if needed
  std::unique_ptr<ColorSpaceXformSteps> steps = nullptr;
  if (vertexMesh->hasColors() && NeedConvertColorSpace(ColorSpace::SRGB(), dstColorSpace)) {
    steps =
        std::make_unique<ColorSpaceXformSteps>(ColorSpace::SRGB().get(), AlphaType::Premultiplied,
                                               dstColorSpace.get(), AlphaType::Premultiplied);
  }

  // Allocate temporary buffer and write interleaved vertex data
  auto buffer = std::make_unique<uint8_t[]>(vertexDataSize);
  uint8_t* ptr = buffer.get();
  for (auto i = 0; i < vertexMesh->vertexCount(); ++i) {
    // Position (float2)
    *reinterpret_cast<Point*>(ptr) = vertexMesh->positions()[i];
    ptr += sizeof(Point);

    // Coverage (float, constant 1.0 when present)
    if (withCoverage) {
      *reinterpret_cast<float*>(ptr) = 1.0f;
      ptr += sizeof(float);
    }

    // TexCoord (float2, optional)
    if (vertexMesh->hasTexCoords()) {
      *reinterpret_cast<Point*>(ptr) = vertexMesh->texCoords()[i];
      ptr += sizeof(Point);
    }

    // Color (UByte4Normalized, optional)
    if (vertexMesh->hasColors()) {
      *reinterpret_cast<uint32_t*>(ptr) = ToUintPMColor(vertexMesh->colors()[i], steps.get());
      ptr += sizeof(uint32_t);
    }
  }

  auto bufferResource =
      BufferResource::FindOrCreate(context, vertexDataSize, GPUBufferUsage::VERTEX);
  if (!bufferResource) {
    return nullptr;
  }

  context->gpu()->queue()->writeBuffer(bufferResource->gpuBuffer(), 0, buffer.get(),
                                       vertexDataSize);

  return bufferResource;
}

MeshIndexBufferUploadTask::MeshIndexBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                                     std::shared_ptr<GPUMeshProxy> meshProxy)
    : ResourceTask(std::move(proxy)), meshProxy(std::move(meshProxy)) {
}

std::shared_ptr<Resource> MeshIndexBufferUploadTask::onMakeResource(Context* context) {
  auto meshBase = static_cast<MeshBase*>(meshProxy->mesh().get());
  if (meshBase == nullptr || meshBase->type() != MeshBase::Type::Vertex) {
    return nullptr;
  }
  auto vertexMesh = static_cast<VertexMesh*>(meshBase);
  // Caller (ProxyProvider::createGPUMeshProxy) only creates this task when hasIndices == true
  DEBUG_ASSERT(vertexMesh->hasIndices());

  size_t indexDataSize = sizeof(uint16_t) * static_cast<size_t>(vertexMesh->indexCount());
  auto bufferResource = BufferResource::FindOrCreate(context, indexDataSize, GPUBufferUsage::INDEX);
  if (!bufferResource) {
    LOGE("MeshIndexBufferUploadTask::onMakeResource() Failed to create index buffer!");
    return nullptr;
  }

  context->gpu()->queue()->writeBuffer(bufferResource->gpuBuffer(), 0, vertexMesh->indices(),
                                       indexDataSize);

  return bufferResource;
}

ShapeMeshBufferUploadTask::ShapeMeshBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                                                     std::unique_ptr<DataSource<Data>> dataSource,
                                                     std::shared_ptr<GPUMeshProxy> meshProxy)
    : ResourceTask(std::move(proxy)), dataSource(std::move(dataSource)),
      meshProxy(std::move(meshProxy)) {
}

std::shared_ptr<Resource> ShapeMeshBufferUploadTask::onMakeResource(Context* context) {
  if (dataSource == nullptr) {
    return nullptr;
  }

  auto vertexData = dataSource->getData();
  if (vertexData == nullptr || vertexData->empty()) {
    return nullptr;
  }

  auto bufferResource =
      BufferResource::FindOrCreate(context, vertexData->size(), GPUBufferUsage::VERTEX);
  if (!bufferResource) {
    return nullptr;
  }

  context->gpu()->queue()->writeBuffer(bufferResource->gpuBuffer(), 0, vertexData->data(),
                                       vertexData->size());

  // Release data source to free memory (triangulation result)
  dataSource = nullptr;

  return bufferResource;
}

}  // namespace tgfx
