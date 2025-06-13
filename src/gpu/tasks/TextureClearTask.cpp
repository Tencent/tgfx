/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextureClearTask.h"
#include "core/PixelRef.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/Gpu.h"
#include "gpu/Texture.h"

namespace tgfx {
TextureClearTask::TextureClearTask(UniqueKey uniqueKey, std::shared_ptr<TextureProxy> proxy)
    : ResourceTask(std::move(uniqueKey)), textureProxy(std::move(proxy)) {
}

bool TextureClearTask::execute(Context* context) {
  if (textureProxy == nullptr) {
    LOGE("TextureClearTask::execute() textureProxy is nullptr!");
    return false;
  }
  auto texture = textureProxy->getTexture();
  if (texture == nullptr) {
    LOGE("TextureClearTask::execute() texture is nullptr!");
    return false;
  }
  auto sampler = texture->getSampler();
  if (sampler == nullptr) {
    LOGE("TextureClearTask::execute() sampler is nullptr!");
    return false;
  }
  auto width = texture->width();
  auto height = texture->height();
  auto bytesPerPixel = PixelFormatBytesPerPixel(sampler->format);
  auto pixelRef = PixelRef::Make(width, height, bytesPerPixel == 1);
  pixelRef->clear();
  auto pixels = pixelRef->lockPixels();
  if (pixels == nullptr) {
    return false;
  }
  auto rowBytes = static_cast<size_t>(width) * bytesPerPixel;
  auto rect = Rect::MakeWH(width, height);
  context->gpu()->writePixels(sampler, rect, pixels, rowBytes);
  pixelRef->unlockPixels();
  return true;
}
}  // namespace tgfx
