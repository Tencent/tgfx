/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "core/utils/PlacementArray.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/tasks/RenderTask.h"

namespace tgfx {
class OpsRenderTask : public RenderTask {
 public:
  OpsRenderTask(BlockAllocator* allocator, std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                PlacementArray<DrawOp>&& drawOps, std::optional<PMColor> clearColor)
      : OpsRenderTask(allocator, std::move(renderTargetProxy), nullptr, std::move(drawOps),
                      clearColor) {
  }

  OpsRenderTask(BlockAllocator* allocator, std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                std::shared_ptr<RenderTargetProxy> depthStencilProxy,
                PlacementArray<DrawOp>&& drawOps, std::optional<PMColor> clearColor)
      : RenderTask(allocator), renderTargetProxy(std::move(renderTargetProxy)),
        depthStencilProxy(std::move(depthStencilProxy)), drawOps(std::move(drawOps)),
        clearColor(clearColor) {
  }

  void execute(CommandEncoder* encoder) override;

 private:
  std::shared_ptr<RenderTargetProxy> renderTargetProxy = nullptr;
  std::shared_ptr<RenderTargetProxy> depthStencilProxy = nullptr;
  PlacementArray<DrawOp> drawOps = {};
  std::optional<PMColor> clearColor = std::nullopt;
};
}  // namespace tgfx
