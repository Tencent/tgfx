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

#include "OpsTask.h"
#include "gpu/Gpu.h"
#include "gpu/RenderPass.h"

namespace tgfx {
void OpsTask::addOp(std::unique_ptr<Op> op) {
  if (!ops.empty() && ops.back()->combineIfPossible(op.get())) {
    return;
  }
  ops.emplace_back(std::move(op));
}

bool OpsTask::execute(Gpu* gpu) {
  if (ops.empty()) {
    return false;
  }
  auto renderPass = gpu->getRenderPass(renderTarget, renderTargetTexture);
  if (renderPass == nullptr) {
    return false;
  }
  std::for_each(ops.begin(), ops.end(), [gpu](auto& op) { op->prepare(gpu); });
  renderPass->begin();
  auto tempOps = std::move(ops);
  for (auto& op : tempOps) {
    op->execute(renderPass);
  }
  renderPass->end();
  gpu->submit(renderPass);
  return true;
}

void OpsTask::gatherProxies(std::vector<TextureProxy*>* proxies) const {
  if (ops.empty()) {
    return;
  }
  auto func = [proxies](TextureProxy* proxy) { proxies->emplace_back(proxy); };
  std::for_each(ops.begin(), ops.end(), [&func](auto& op) { op->visitProxies(func); });
}
}  // namespace tgfx
