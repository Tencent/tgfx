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

#include "OpsRenderTask.h"
#include "gpu/Gpu.h"
#include "gpu/RenderPass.h"
#include "profileClient/Profile.h"

namespace tgfx {
void OpsRenderTask::addOp(std::unique_ptr<Op> op) {
  if (!ops.empty() && ops.back()->combineIfPossible(op.get())) {
    return;
  }
  ops.emplace_back(std::move(op));
}

void OpsRenderTask::prepare(Context* context) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME_COLOR("OpsRenderTask::prepare", tracy::Color::ColorType::Green);
  renderPass = context->gpu()->getRenderPass();
  for (auto& op : ops) {
    op->prepare(context, renderFlags);
  }
}

bool OpsRenderTask::execute(Gpu* gpu) {
  TGFX_PROFILE_ZONE_SCOPPE_NAME("OpsRenderTask::execute");
  if (ops.empty()) {
    return false;
  }
  if (!renderPass->begin(renderTargetProxy)) {
    LOGE("OpsTask::execute() Failed to initialize the render pass!");
    return false;
  }
  auto tempOps = std::move(ops);
  for (auto& op : tempOps) {
    op->execute(renderPass.get());
  }
  gpu->submit(renderPass.get());
  renderPass->end();
  return true;
}
}  // namespace tgfx
