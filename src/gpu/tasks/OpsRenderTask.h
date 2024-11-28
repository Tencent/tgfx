/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "gpu/ops/Op.h"
#include "gpu/tasks/RenderTask.h"
#include "tgfx/core/Surface.h"

namespace tgfx {
class OpsRenderTask : public RenderTask {
 public:
  OpsRenderTask(std::shared_ptr<RenderTargetProxy> renderTargetProxy, uint32_t renderFlags)
      : RenderTask(std::move(renderTargetProxy)), renderFlags(renderFlags) {
  }

  void addOp(std::unique_ptr<Op> op);

  void prepare(Context* context) override;

  bool execute(Gpu* gpu) override;

  void makeClosed() {
    closed = true;
  }

  bool isClosed() const {
    return closed;
  }

 private:
  bool closed = false;
  uint32_t renderFlags = 0;
  std::shared_ptr<RenderPass> renderPass = nullptr;
  std::vector<std::unique_ptr<Op>> ops = {};
};
}  // namespace tgfx
