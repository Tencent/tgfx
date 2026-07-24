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
#include "core/utils/UniqueID.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/GPU.h"

namespace tgfx {
size_t DepthStencilTextureView::memoryUsage() const {
  // PixelFormatBytesPerPixel() reports 0 for depth/stencil formats, so the footprint is computed
  // directly: DEPTH24_STENCIL8 occupies 4 bytes per pixel, multiplied by the sample count.
  return static_cast<size_t>(width()) * static_cast<size_t>(height()) * 4 *
         static_cast<size_t>(_sampleCount);
}

std::shared_ptr<DepthStencilTextureView> DepthStencilTextureView::MakeFrom(
    Context* context, const DepthStencilSpec& spec, const UniqueKey& uniqueKey) {
  DEBUG_ASSERT(context != nullptr);
  TextureDescriptor descriptor(spec.width, spec.height, spec.format, false, spec.sampleCount,
                               TextureUsage::RENDER_ATTACHMENT);
  auto texture = context->gpu()->createTexture(descriptor);
  if (texture == nullptr) {
    return nullptr;
  }
  auto attachment = Resource::AddToCache(
      context, new DepthStencilTextureView(std::move(texture), spec.sampleCount),
      ComputeDepthStencilScratchKey(spec));
  attachment->assignUniqueKey(uniqueKey);
  return attachment;
}

UniqueKey DepthStencilTextureView::ComputeSharedAttachmentUniqueKey(const DepthStencilSpec& spec) {
  static const UniqueKey domain = UniqueKey::Make();
  uint32_t data[] = {static_cast<uint32_t>(spec.width), static_cast<uint32_t>(spec.height),
                     static_cast<uint32_t>(spec.sampleCount), static_cast<uint32_t>(spec.format)};
  return UniqueKey::Append(domain, data, 4);
}

ScratchKey DepthStencilTextureView::ComputeDepthStencilScratchKey(const DepthStencilSpec& spec) {
  static const uint32_t DepthStencilType = UniqueID::Next();
  BytesKey bytesKey(5);
  bytesKey.write(DepthStencilType);
  bytesKey.write(static_cast<uint32_t>(spec.width));
  bytesKey.write(static_cast<uint32_t>(spec.height));
  bytesKey.write(static_cast<uint32_t>(spec.sampleCount));
  bytesKey.write(static_cast<uint32_t>(spec.format));
  return bytesKey;
}

DepthStencilTextureView::DepthStencilTextureView(std::shared_ptr<Texture> texture, int sampleCount)
    : DefaultTextureView(std::move(texture)), _sampleCount(sampleCount) {
}
}  // namespace tgfx
