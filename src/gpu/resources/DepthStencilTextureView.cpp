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

#include "gpu/resources/DepthStencilTextureView.h"
#include "core/utils/Log.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {

// Bytes per pixel of the DEPTH24_STENCIL8 format used by all depth/stencil attachments.
static constexpr size_t DEPTH_STENCIL_BYTES_PER_PIXEL = 4;

static UniqueKey ComputeDepthStencilUniqueKey(int width, int height, int sampleCount) {
  static const UniqueKey DepthStencilDomain = UniqueKey::Make();
  uint32_t data[] = {static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                     static_cast<uint32_t>(sampleCount)};
  return UniqueKey::Append(DepthStencilDomain, data, 3);
}

size_t DepthStencilTextureView::memoryUsage() const {
  return static_cast<size_t>(width()) * static_cast<size_t>(height()) *
         DEPTH_STENCIL_BYTES_PER_PIXEL * static_cast<size_t>(_texture->sampleCount());
}

std::shared_ptr<DepthStencilTextureView> DepthStencilTextureView::Make(Context* context, int width,
                                                                       int height,
                                                                       int sampleCount) {
  DEBUG_ASSERT(context != nullptr);
  if (context == nullptr) {
    return nullptr;
  }

  auto uniqueKey = ComputeDepthStencilUniqueKey(width, height, sampleCount);
  auto attachment = Resource::Find<DepthStencilTextureView>(context, uniqueKey);
  if (attachment != nullptr) {
    return attachment;
  }

  TextureDescriptor descriptor(width, height, PixelFormat::DEPTH24_STENCIL8, false, sampleCount,
                               TextureUsage::RENDER_ATTACHMENT);
  auto texture = context->gpu()->createTexture(descriptor);
  if (texture == nullptr) {
    return nullptr;
  }
  attachment = Resource::AddToCache(context, new DepthStencilTextureView(std::move(texture)));
  attachment->assignUniqueKey(uniqueKey);
  return attachment;
}

DepthStencilTextureView::DepthStencilTextureView(std::shared_ptr<Texture> texture)
    : DefaultTextureView(std::move(texture)) {
}

}  // namespace tgfx
