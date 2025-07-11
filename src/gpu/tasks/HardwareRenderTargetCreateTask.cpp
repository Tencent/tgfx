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

#include "HardwareRenderTargetCreateTask.h"

namespace tgfx {
HardwareRenderTargetCreateTask::HardwareRenderTargetCreateTask(UniqueKey uniqueKey,
                                                               HardwareBufferRef hardwareBuffer,
                                                               int sampleCount)
    : ResourceTask(std::move(uniqueKey)), hardwareBuffer(hardwareBuffer), sampleCount(sampleCount) {
}

std::shared_ptr<Resource> HardwareRenderTargetCreateTask::onMakeResource(Context* context) {
  auto renderTarget = RenderTarget::MakeFrom(context, hardwareBuffer, sampleCount);
  if (renderTarget == nullptr) {
    LOGE("HardwareBufferRTCreateTask::onMakeResource() Failed to create the render target!");
  }
  return renderTarget->asTexture();
}
}  // namespace tgfx
