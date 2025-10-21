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
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Log.h"
#include "core/utils/UniqueID.h"
#include "gpu/GPU.h"
#include "gpu/GPUTexture.h"

namespace tgfx {
static ScratchKey ComputeRenderTargetScratchKey(int width, int height, PixelFormat format,
                                                int sampleCount, bool mipmapped) {
  static const uint32_t TextureRenderTargetType = UniqueID::Next();
  BytesKey bytesKey(7);
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
                                                     bool adopted,
                                                     std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  uint32_t usage = GPUTextureUsage::TEXTURE_BINDING | GPUTextureUsage::RENDER_ATTACHMENT;
  auto texture = context->gpu()->importExternalTexture(backendTexture, usage, adopted);
  if (texture == nullptr) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, texture->format());
  ScratchKey scratchKey = {};
  if (adopted) {
    scratchKey = ComputeRenderTargetScratchKey(backendTexture.width(), backendTexture.height(),
                                               texture->format(), sampleCount,
                                               texture->mipLevelCount() > 1);
  }
  return TextureRenderTarget::MakeFrom(context, std::move(texture), sampleCount, origin, !adopted,
                                       scratchKey, std::move(colorSpace));
}

std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     HardwareBufferRef hardwareBuffer,
                                                     int sampleCount,
                                                     std::shared_ptr<ColorSpace> colorSpace) {
  if (context == nullptr) {
    return nullptr;
  }
  auto gpu = context->gpu();
  YUVFormat yuvFormat = YUVFormat::Unknown;
  auto formats = gpu->getHardwareTextureFormats(hardwareBuffer, &yuvFormat);
  if (formats.size() != 1 || yuvFormat != YUVFormat::Unknown) {
    return nullptr;
  }
  uint32_t usage = GPUTextureUsage::TEXTURE_BINDING | GPUTextureUsage::RENDER_ATTACHMENT;
  auto textures = gpu->importHardwareTextures(hardwareBuffer, usage);
  if (textures.size() != 1) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, formats.front());
  return TextureRenderTarget::MakeFrom(context, std::move(textures.front()), sampleCount,
                                       ImageOrigin::TopLeft, true, {}, std::move(colorSpace));
}

std::shared_ptr<RenderTarget> RenderTarget::Make(Context* context, int width, int height,
                                                 PixelFormat format, int sampleCount,
                                                 bool mipmapped, ImageOrigin origin,
                                                 std::shared_ptr<ColorSpace> colorSpace) {
  if (!TextureView::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto caps = context->caps();
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto scratchKey =
      ComputeRenderTargetScratchKey(width, height, format, sampleCount, mipmapped);
  if (auto renderTarget = Resource::Find<TextureRenderTarget>(context, scratchKey)) {
    renderTarget->_origin = origin;
    renderTarget->setColorSpace(colorSpace);
    return renderTarget;
  }
  GPUTextureDescriptor descriptor = {
      width,     height, format,
      mipmapped, 1,      GPUTextureUsage::TEXTURE_BINDING | GPUTextureUsage::RENDER_ATTACHMENT};
  auto texture = context->gpu()->createTexture(descriptor);
  if (texture == nullptr) {
    return nullptr;
  }
  return TextureRenderTarget::MakeFrom(context, std::move(texture), sampleCount, origin, false,
                                       scratchKey, std::move(colorSpace));
}

std::shared_ptr<RenderTarget> TextureRenderTarget::MakeFrom(
    Context* context, std::shared_ptr<GPUTexture> texture, int sampleCount, ImageOrigin origin,
    bool externallyOwned, const ScratchKey& scratchKey, std::shared_ptr<ColorSpace> colorSpace) {
  DEBUG_ASSERT(context != nullptr);
  DEBUG_ASSERT(texture != nullptr);
  std::shared_ptr<GPUTexture> renderTexture = nullptr;
  if (sampleCount > 1) {
    GPUTextureDescriptor descriptor = {texture->width(),  texture->height(),
                                       texture->format(), false,
                                       sampleCount,       GPUTextureUsage::RENDER_ATTACHMENT};
    renderTexture = context->gpu()->createTexture(descriptor);
    if (renderTexture == nullptr) {
      return nullptr;
    }
  }
  auto renderTarget = new TextureRenderTarget(std::move(texture), std::move(renderTexture), origin,
                                              externallyOwned, std::move(colorSpace));
  return Resource::AddToCache(context, renderTarget, scratchKey);
}
}  // namespace tgfx
