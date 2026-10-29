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
#include "core/StencilCoverPathTessellator.h"
#include "gpu/proxies/StencilCoverPathProxy.h"

namespace tgfx {
/**
 * StencilCoverPathUploadTask uploads the bezier vertex stream produced by
 * StencilCoverPathTessellator into a GPU vertex buffer. When the rasterizer reports no drawable
 * geometry, the task resolves to no resource and the bound vertex proxy stays unbacked, which
 * allows the stencil-and-cover render path to gracefully fall back to the legacy pipeline.
 * The fill rule the stream carries is published to the slot the proxy reads, so the draw op that
 * waits on this task learns the rule from it.
 */
class StencilCoverPathUploadTask : public ResourceTask {
 public:
  StencilCoverPathUploadTask(std::shared_ptr<ResourceProxy> vertexBufferProxy,
                             std::shared_ptr<FillTypeSlot> fillTypeSlot,
                             std::unique_ptr<DataSource<StencilCoverVertexData>> source);

 protected:
  std::shared_ptr<Resource> onMakeResource(Context* context) override;

 private:
  std::shared_ptr<FillTypeSlot> fillTypeSlot = nullptr;
  std::unique_ptr<DataSource<StencilCoverVertexData>> source = nullptr;
};
}  // namespace tgfx
