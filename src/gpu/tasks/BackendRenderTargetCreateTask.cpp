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

#include "BackendRenderTargetCreateTask.h"

namespace tgfx {
BackendRenderTargetCreateTask::BackendRenderTargetCreateTask(std::shared_ptr<ResourceProxy> proxy,
                                                             const BackendTexture& backendTexture,
                                                             int sampleCount, ImageOrigin origin,
                                                             bool adopted)
    : ResourceTask(std::move(proxy)), backendTexture(backendTexture), sampleCount(sampleCount),
      origin(origin), adopted(adopted) {
}

std::shared_ptr<Resource> BackendRenderTargetCreateTask::onMakeResource(Context* context) {
  auto renderTarget = RenderTarget::MakeFrom(context, backendTexture, sampleCount, origin, adopted);
  if (renderTarget == nullptr) {
    LOGE("BackendTextureRTCreateTask::onMakeResource() Failed to create the render target!");
  }
  return renderTarget->asTexture();
}
}  // namespace tgfx
