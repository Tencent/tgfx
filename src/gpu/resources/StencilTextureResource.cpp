/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "gpu/resources/StencilTextureResource.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
ScratchKey StencilTextureResource::ComputeScratchKey(int width, int height) {
  static const uint32_t StencilTextureResourceType = UniqueID::Next();
  BytesKey bytesKey(3);
  bytesKey.write(StencilTextureResourceType);
  bytesKey.write(static_cast<uint32_t>(width));
  bytesKey.write(static_cast<uint32_t>(height));
  return bytesKey;
}

std::shared_ptr<StencilTextureResource> StencilTextureResource::FindOrCreate(Context* context,
                                                                             int width,
                                                                             int height) {
  if (context == nullptr || width < 1 || height < 1) {
    return nullptr;
  }
  auto scratchKey = ComputeScratchKey(width, height);
  auto resource = Resource::Find<StencilTextureResource>(context, scratchKey);
  if (resource != nullptr) {
    return resource;
  }
  TextureDescriptor descriptor(width, height, PixelFormat::DEPTH24_STENCIL8, false, 1,
                               TextureUsage::RENDER_ATTACHMENT);
  auto texture = context->gpu()->createTexture(descriptor);
  if (texture == nullptr) {
    return nullptr;
  }
  return Resource::AddToCache(context, new StencilTextureResource(std::move(texture)), scratchKey);
}
}  // namespace tgfx
