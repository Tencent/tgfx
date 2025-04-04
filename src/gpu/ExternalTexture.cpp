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

#include "ExternalTexture.h"
#include "core/utils/PixelFormatUtil.h"
#include "gpu/Gpu.h"

namespace tgfx {
std::shared_ptr<Texture> Texture::MakeFrom(Context* context, const BackendTexture& backendTexture,
                                           ImageOrigin origin) {
  return ExternalTexture::MakeFrom(context, backendTexture, origin, false);
}

std::shared_ptr<Texture> Texture::MakeAdopted(Context* context,
                                              const BackendTexture& backendTexture,
                                              ImageOrigin origin) {
  return ExternalTexture::MakeFrom(context, backendTexture, origin, true);
}

std::shared_ptr<Texture> ExternalTexture::MakeFrom(Context* context,
                                                   const BackendTexture& backendTexture,
                                                   ImageOrigin origin, bool adopted) {
  if (context == nullptr || !backendTexture.isValid()) {
    return nullptr;
  }
  auto sampler = TextureSampler::MakeFrom(context, backendTexture);
  if (sampler == nullptr) {
    return nullptr;
  }
  auto texture = new ExternalTexture(std::move(sampler), backendTexture.width(),
                                     backendTexture.height(), origin, adopted);
  return Resource::AddToCache(context, texture);
}

ExternalTexture::ExternalTexture(std::unique_ptr<TextureSampler> sampler, int width, int height,
                                 ImageOrigin origin, bool adopted)
    : Texture(width, height, origin), sampler(std::move(sampler)), adopted(adopted) {
}

size_t ExternalTexture::memoryUsage() const {
  if (!adopted) {
    return 0;
  }
  auto colorSize = static_cast<size_t>(width()) * static_cast<size_t>(height()) *
                   PixelFormatBytesPerPixel(sampler->format);
  return sampler->hasMipmaps() ? colorSize * 4 / 3 : colorSize;
}

void ExternalTexture::onReleaseGPU() {
  if (adopted) {
    context->gpu()->deleteSampler(sampler.get());
  }
}
}  // namespace tgfx
