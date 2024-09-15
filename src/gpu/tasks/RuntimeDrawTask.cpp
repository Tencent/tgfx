/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "RuntimeDrawTask.h"
#include "gpu/RuntimeResource.h"

namespace tgfx {
RuntimeDrawTask::RuntimeDrawTask(std::shared_ptr<RenderTargetProxy> target,
                                 std::shared_ptr<TextureProxy> source,
                                 std::shared_ptr<RuntimeEffect> effect, const Point& offset)
    : RenderTask(std::move(target)), source(std::move(source)), effect(std::move(effect)),
      offset(offset) {
}

bool RuntimeDrawTask::execute(Gpu* gpu) {
  auto texture = source->getTexture();
  if (texture == nullptr) {
    LOGE("RuntimeDrawTask::execute() Failed to get the source texture!");
    return false;
  }
  auto renderTarget = renderTargetProxy->getRenderTarget();
  if (renderTarget == nullptr) {
    LOGE("RuntimeDrawTask::execute() Failed to get the render target!");
    return false;
  }
  auto context = gpu->getContext();
  UniqueKey uniqueKey(effect->type());
  auto program = Resource::Find<RuntimeResource>(context, uniqueKey);
  if (program == nullptr) {
    program = RuntimeResource::Wrap(uniqueKey, effect->onCreateProgram(context));
    if (program == nullptr) {
      LOGE("RuntimeDrawTask::execute() Failed to create the runtime program!");
      return false;
    }
  }
  return effect->onDraw(program->getProgram(), texture->getBackendTexture(),
                        renderTarget->getBackendRenderTarget(), offset);
}
}  // namespace tgfx
