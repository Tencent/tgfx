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
#include "core/utils/UniqueID.h"
#include "gpu/GPU.h"
#include "gpu/GPUTexture.h"

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
  auto texture = context->gpu()->importExternalTexture(backendTexture, adopted);
  if (texture == nullptr) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, texture->format());
  ScratchKey scratchKey = {};
  if (adopted) {
    scratchKey =
        ComputeRenderTargetScratchKey(backendTexture.width(), backendTexture.height(),
                                      texture->format(), sampleCount, texture->hasMipmaps());
  }
  return TextureRenderTarget::MakeFrom(context, std::move(texture), backendTexture.width(),
                                       backendTexture.height(), sampleCount, origin, !adopted,
                                       scratchKey);
}

std::shared_ptr<RenderTarget> RenderTarget::MakeFrom(Context* context,
                                                     HardwareBufferRef hardwareBuffer,
                                                     int sampleCount) {
  if (context == nullptr) {
    return nullptr;
  }
  auto size = HardwareBufferGetSize(hardwareBuffer);
  if (size.isEmpty()) {
    return nullptr;
  }
  auto gpu = context->gpu();
  YUVFormat yuvFormat = YUVFormat::Unknown;
  auto formats = gpu->getHardwareTextureFormats(hardwareBuffer, &yuvFormat);
  if (formats.size() != 1 || yuvFormat != YUVFormat::Unknown) {
    return nullptr;
  }
  auto textures = gpu->importHardwareTextures(hardwareBuffer);
  if (textures.size() != 1) {
    return nullptr;
  }
  sampleCount = context->caps()->getSampleCount(sampleCount, formats.front());
  return TextureRenderTarget::MakeFrom(context, std::move(textures.front()), size.width,
                                       size.height, sampleCount, ImageOrigin::TopLeft, true);
}

std::shared_ptr<RenderTarget> RenderTarget::Make(Context* context, int width, int height,
                                                 PixelFormat format, int sampleCount,
                                                 bool mipmapped, ImageOrigin origin) {
  if (!TextureView::CheckSizeAndFormat(context, width, height, format)) {
    return nullptr;
  }
  auto caps = context->caps();
  if (!caps->isFormatRenderable(format)) {
    return nullptr;
  }
  auto hasMipmaps = caps->mipmapSupport ? mipmapped : false;
  sampleCount = caps->getSampleCount(sampleCount, format);
  auto scratchKey = ComputeRenderTargetScratchKey(width, height, format, sampleCount, hasMipmaps);
  if (auto renderTarget = Resource::Find<TextureRenderTarget>(context, scratchKey)) {
    renderTarget->_origin = origin;
    return renderTarget;
  }
  auto texture = context->gpu()->createTexture(width, height, format, hasMipmaps);
  if (texture == nullptr) {
    return nullptr;
  }
  return TextureRenderTarget::MakeFrom(context, std::move(texture), width, height, sampleCount,
                                       origin, false, scratchKey);
}

std::shared_ptr<RenderTarget> TextureRenderTarget::MakeFrom(
    Context* context, std::unique_ptr<GPUTexture> texture, int width, int height, int sampleCount,
    ImageOrigin origin, bool externallyOwned, const ScratchKey& scratchKey) {
  DEBUG_ASSERT(context != nullptr);
  DEBUG_ASSERT(texture != nullptr);
  auto frameBuffer = context->gpu()->createFrameBuffer(texture.get(), width, height, sampleCount);
  if (frameBuffer == nullptr) {
    texture->release(context->gpu());
    return nullptr;
  }
  auto renderTarget = new TextureRenderTarget(std::move(texture), std::move(frameBuffer), width,
                                              height, origin, externallyOwned);
  return Resource::AddToCache(context, renderTarget, scratchKey);
}

void TextureRenderTarget::onReleaseGPU() {
  frameBuffer->release(context->gpu());
  _texture->release(context->gpu());
}
}  // namespace tgfx
