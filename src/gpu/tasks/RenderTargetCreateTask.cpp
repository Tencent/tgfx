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

#include "RenderTargetCreateTask.h"
#include "core/utils/Log.h"
#include "core/utils/Profiling.h"
#include "gpu/RenderTarget.h"
#include "gpu/Texture.h"

namespace tgfx {
std::unique_ptr<RenderTargetCreateTask> RenderTargetCreateTask::MakeFrom(
    UniqueKey uniqueKey, std::shared_ptr<TextureProxy> textureProxy, PixelFormat pixelFormat,
    int sampleCount) {
  return std::unique_ptr<RenderTargetCreateTask>(new RenderTargetCreateTask(
      std::move(uniqueKey), std::move(textureProxy), pixelFormat, sampleCount));
}

RenderTargetCreateTask::RenderTargetCreateTask(UniqueKey uniqueKey,
                                               std::shared_ptr<TextureProxy> textureProxy,
                                               PixelFormat pixelFormat, int sampleCount)
    : ResourceTask(std::move(uniqueKey)), textureProxy(std::move(textureProxy)),
      pixelFormat(pixelFormat), sampleCount(sampleCount) {
}

std::shared_ptr<Resource> RenderTargetCreateTask::onMakeResource(Context*) {
  TRACE_EVENT_NAME("createRenderTarget");
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    LOGE("RenderTargetCreateTask::onMakeResource() Failed to get the associated texture!");
    return nullptr;
  }
  if (texture->getSampler()->format != pixelFormat) {
    LOGE("RenderTargetCreateTask::onMakeResource() the texture format mismatch!");
    return nullptr;
  }
  auto renderTarget = RenderTarget::MakeFrom(texture.get(), sampleCount);
  if (renderTarget == nullptr) {
    LOGE("RenderTargetCreateTask::onMakeResource() Failed to create the render target!");
  }
  return renderTarget;
}
}  // namespace tgfx
