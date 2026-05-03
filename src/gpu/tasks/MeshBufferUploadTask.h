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

#include "ResourceTask.h"
#include "core/DataSource.h"
#include "gpu/proxies/GPUMeshProxy.h"
#include "tgfx/core/Data.h"

namespace tgfx {

/**
 * Uploads VertexMesh vertex data to GPU with interleaved layout (position + texCoord + color).
 */
class VertexMeshBufferUploadTask : public ResourceTask {
 public:
  VertexMeshBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                             std::shared_ptr<GPUMeshProxy> meshProxy,
                             std::shared_ptr<ColorSpace> dstColorSpace);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::shared_ptr<GPUMeshProxy> meshProxy = nullptr;
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;
};

/**
 * Uploads VertexMesh index data to GPU.
 */
class MeshIndexBufferUploadTask : public ResourceTask {
 public:
  MeshIndexBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                            std::shared_ptr<GPUMeshProxy> meshProxy);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::shared_ptr<GPUMeshProxy> meshProxy = nullptr;
};

/**
 * Uploads ShapeMesh triangulated vertex data to GPU.
 * Receives triangulation data from DataSource (may be computed asynchronously).
 */
class ShapeMeshBufferUploadTask : public ResourceTask {
 public:
  ShapeMeshBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                            std::unique_ptr<DataSource<Data>> dataSource,
                            std::shared_ptr<GPUMeshProxy> meshProxy);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::unique_ptr<DataSource<Data>> dataSource = nullptr;
  std::shared_ptr<GPUMeshProxy> meshProxy = nullptr;
};

}  // namespace tgfx
