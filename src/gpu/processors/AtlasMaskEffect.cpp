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

#include "AtlasMaskEffect.h"

namespace tgfx {

AtlasMaskEffect::AtlasMaskEffect(std::shared_ptr<TextureProxy> proxy,
                                 const SamplingOptions& sampling)
    : FragmentProcessor(classID()), textureProxy(std::move(proxy)), samplerState(sampling) {
}

void AtlasMaskEffect::onComputeProcessorKey(BytesKey* bytesKey) const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return;
  }
  uint32_t flags = 1;
  flags |= textureProxy->isAlphaOnly() ? 2 : 0;
  flags |= 4;
  bytesKey->write(flags);
}

size_t AtlasMaskEffect::onCountTextureSamplers() const {
  auto texture = getTexture();
  return texture == nullptr ? 0 : 1;
}

const TextureSampler* AtlasMaskEffect::onTextureSampler(size_t /* index*/) const {
  auto texture = getTexture();
  if (texture == nullptr) {
    return nullptr;
  }
  return texture->getSampler();
}

Texture* AtlasMaskEffect::getTexture() const {
  return textureProxy->getTexture().get();
}
}  // namespace tgfx
