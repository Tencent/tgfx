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

#include "TextureRenderTarget.h"
#include "../../../include/tgfx/core/Log.h"
#include "core/utils/UniqueID.h"
#include "tgfx/gpu/GPU.h"
#include "tgfx/gpu/Texture.h"

namespace tgfx {
static ScratchKey ComputeRenderTargetScratchKey(int width, int height, PixelFormat format,
                                                int sampleCount, bool mipmapped) {
  static const uint32_t TextureRenderTargetType = UniqueID::Next();
  BytesKey bytesKey(5);
  bytesKey.write(TextureRenderTargetType);
  bytesKey.write(width);
  bytesKey.write(height);
  bytesKey.write(sampleCount);
  auto formatValue = static_cast<uint32_t>(format);
  auto mipmapValue = static_cast<uint32_t>(mipmapped ? 1 : 0);
  bytesKey.write(formatValue | (mipmapValue << 30));
  return bytesKey;
}

std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     const BackendTexture& backendTexture,
                                                     int sampleCount, ImageOrigin origin,
                                                     bool adopted) {
  if (context == nullptr) {
    return nullptr;
  }
  uint32_t usage = TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT;
  auto texture = context->gpu()->importBackendTexture(backendTexture, usage, adopted);
  if (texture == nullptr) {
    return nullptr;
  }
  sampleCount = context->gpu()->getSampleCount(sampleCount, texture->format());
  ScratchKey scratchKey = {};
  if (adopted) {
    scratchKey =
        ComputeRenderTargetScratchKey(backendTexture.width(), backendTexture.height(),
                                      texture->format(), sampleCount, texture->mipLevelCount() > 1);
  }
  return TextureRenderTarget::MakeFrom(context, std::move(texture), sampleCount, origin, !adopted,
                                       scratchKey);
}

std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     HardwareBufferRef hardwareBuffer,
                                                     int sampleCount) {
  if (context == nullptr) {
    return nullptr;
  }
  auto info = HardwareBufferGetInfo(hardwareBuffer);
  if (info.format == HardwareBufferFormat::Unknown ||
      info.format == HardwareBufferFormat::YCBCR_420_SP) {
    return nullptr;
  }
  auto gpu = context->gpu();
  uint32_t usage = TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT;
  auto textures = gpu->importHardwareTextures(hardwareBuffer, usage);
  if (textures.size() != 1) {
    return nullptr;
  }
  auto& texture = textures[0];
  sampleCount = context->gpu()->getSampleCount(sampleCount, texture->format());
  return TextureRenderTarget::MakeFrom(context, std::move(texture), sampleCount,
                                       ImageOrigin::TopLeft, true);
}

std::shared_ptr<RenderTarget> RenderTarget::Make(Context* context, int width, int height,
                                                 PixelFormat format, int sampleCount,
                                                 bool mipmapped, ImageOrigin origin) {
  if (!TextureView::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto gpu = context->gpu();
  sampleCount = gpu->getSampleCount(sampleCount, format);
  auto scratchKey = ComputeRenderTargetScratchKey(width, height, format, sampleCount, mipmapped);
  if (auto renderTarget = Resource::Find<TextureRenderTarget>(context, scratchKey)) {
    renderTarget->_origin = origin;
    return renderTarget;
  }
  TextureDescriptor descriptor = {
      width,     height, format,
      mipmapped, 1,      TextureUsage::TEXTURE_BINDING | TextureUsage::RENDER_ATTACHMENT};
  auto texture = gpu->createTexture(descriptor);
  if (texture == nullptr) {
    return nullptr;
  }
  return TextureRenderTarget::MakeFrom(context, std::move(texture), sampleCount, origin, false,
                                       scratchKey);
}

std::shared_ptr<RenderTarget> TextureRenderTarget::MakeFrom(Context* context,
                                                            std::shared_ptr<Texture> texture,
                                                            int sampleCount, ImageOrigin origin,
                                                            bool externallyOwned,
                                                            const ScratchKey& scratchKey) {
  DEBUG_ASSERT(context != nullptr);
  DEBUG_ASSERT(texture != nullptr);
  std::shared_ptr<Texture> renderTexture = nullptr;
  if (sampleCount > 1) {
    TextureDescriptor descriptor = {texture->width(),  texture->height(),
                                    texture->format(), false,
                                    sampleCount,       TextureUsage::RENDER_ATTACHMENT};
    renderTexture = context->gpu()->createTexture(descriptor);
    if (renderTexture == nullptr) {
      return nullptr;
    }
  }
  auto renderTarget = new TextureRenderTarget(std::move(texture), std::move(renderTexture), origin,
                                              externallyOwned);
  return Resource::AddToCache(context, renderTarget, scratchKey);
}
}  // namespace tgfx
