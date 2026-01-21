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
#include "gpu/proxies/GPUMeshProxy.h"

namespace tgfx {

/**
 * Uploads mesh vertex data to GPU.
 * Buffer layout: [interleaved vertex data (position + texCoord + color)]
 */
class MeshVertexBufferUploadTask : public ResourceTask {
 public:
  MeshVertexBufferUploadTask(std::shared_ptr<ResourceProxy> proxy,
                             std::shared_ptr<GPUMeshProxy> meshProxy);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::shared_ptr<GPUMeshProxy> meshProxy = nullptr;
};

/**
 * Uploads mesh index data to GPU.
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

}  // namespace tgfx
