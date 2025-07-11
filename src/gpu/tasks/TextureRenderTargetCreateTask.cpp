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

#include "TextureRenderTargetCreateTask.h"

namespace tgfx {
TextureRenderTargetCreateTask::TextureRenderTargetCreateTask(std::shared_ptr<ResourceProxy> proxy,
                                                             int width, int height,
                                                             PixelFormat format, int sampleCount,
                                                             bool mipmapped, ImageOrigin origin)
    : ResourceTask(std::move(proxy)), width(width), height(height), format(format),
      sampleCount(sampleCount), mipmapped(mipmapped), origin(origin) {
}

std::shared_ptr<Resource> TextureRenderTargetCreateTask::onMakeResource(Context* context) {
  auto renderTarget =
      RenderTarget::Make(context, width, height, format, sampleCount, mipmapped, origin);
  if (renderTarget == nullptr) {
    LOGE("TextureRTCreateTask::onMakeResource() Failed to create the render target!");
  }
  return renderTarget->asTexture();
}
}  // namespace tgfx
