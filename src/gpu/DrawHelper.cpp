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

#include "DrawHelper.h"
#include "GlobalCache.h"

namespace tgfx {
AddressMode ToAddressMode(TileMode tileMode) {
  switch (tileMode) {
    case TileMode::Clamp:
      return AddressMode::ClampToEdge;
    case TileMode::Repeat:
      return AddressMode::Repeat;
    case TileMode::Mirror:
      return AddressMode::MirrorRepeat;
    case TileMode::Decal:
      return AddressMode::ClampToBorder;
  }
}

std::pair<std::shared_ptr<GPUBuffer>, size_t> SetupUniformBuffer(const Context* context,
                                                                 UniformData* uniformData) {
  if (uniformData == nullptr || uniformData->size() == 0) {
    return {nullptr, 0};
  }

  size_t offset = 0;
  auto buffer = context->globalCache()->findOrCreateUniformBuffer(uniformData->size(), &offset);
  if (buffer != nullptr) {
    uniformData->setBuffer(static_cast<uint8_t*>(buffer->map()) + offset);
  }
  return {buffer, offset};
}

void SetUniformBuffer(RenderPass* renderPass, std::shared_ptr<GPUBuffer> buffer, size_t offset,
                      size_t size, unsigned bindingPoint) {
  if (buffer != nullptr) {
    buffer->unmap();
    renderPass->setUniformBuffer(bindingPoint, std::move(buffer), offset, size);
  }
}

void SetupTextures(RenderPass* renderPass, GPU* gpu, const ProgramInfo& programInfo) {
  auto samplers = programInfo.getSamplers();
  unsigned textureBinding = TEXTURE_BINDING_POINT_START;

  for (const auto& samplerInfo : samplers) {
    const auto& texture = samplerInfo.texture;
    const auto& state = samplerInfo.state;

    GPUSamplerDescriptor descriptor(ToAddressMode(state.tileModeX), ToAddressMode(state.tileModeY),
                                    state.filterMode, state.filterMode, state.mipmapMode);
    auto sampler = gpu->createSampler(descriptor);
    renderPass->setTexture(textureBinding++, texture, sampler);
  }
}
}  // namespace tgfx
