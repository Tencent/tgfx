/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "CommandEncoder.h"
#include "RenderPass.h"
#include "core/utils/Log.h"

namespace tgfx {
std::shared_ptr<RenderPass> CommandEncoder::beginRenderPass(
    const RenderPassDescriptor& descriptor) {
  if (activeRenderPass && !activeRenderPass->isEnd) {
    LOGE("CommandEncoder::beginRenderPass() Cannot begin a new render pass while one is active!");
    return nullptr;
  }
  activeRenderPass = onBeginRenderPass(descriptor);
  return activeRenderPass;
}

std::shared_ptr<CommandBuffer> CommandEncoder::finish() {
  if (activeRenderPass && !activeRenderPass->isEnd) {
    LOGE("CommandEncoder::finish() Cannot finish command encoder while a render pass is active!");
    return nullptr;
  }
  activeRenderPass = nullptr;
  return onFinish();
}

}  // namespace tgfx