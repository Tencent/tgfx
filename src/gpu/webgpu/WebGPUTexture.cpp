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

#include "WebGPUTexture.h"
#include "WebGPUDefines.h"
#include "WebGPUGPU.h"
#include "WebGPUUtil.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/console.h>
#endif

namespace tgfx {

std::shared_ptr<WebGPUTexture> WebGPUTexture::Make(WebGPUGPU* gpu,
                                                   const TextureDescriptor& descriptor) {
  if (gpu == nullptr || descriptor.width <= 0 || descriptor.height <= 0) {
    return nullptr;
  }
  auto format = gpu->getWGPUTextureFormat(descriptor.format);
  WGPUTextureDescriptor textureDesc = {};
  textureDesc.dimension = WGPUTextureDimension_2D;
  textureDesc.size = {static_cast<uint32_t>(descriptor.width),
                      static_cast<uint32_t>(descriptor.height), 1};
  textureDesc.format = format;
  textureDesc.mipLevelCount = static_cast<uint32_t>(descriptor.mipLevelCount);
  textureDesc.sampleCount = static_cast<uint32_t>(descriptor.sampleCount);
  textureDesc.usage = ToWGPUTextureUsage(descriptor.usage);
  auto texture = wgpuDeviceCreateTexture(gpu->device(), &textureDesc);
  emscripten_console_logf("[WebGPU Texture] Make: %dx%d format=%u usage=%u mip=%u sample=%u => %p",
                          descriptor.width, descriptor.height, static_cast<unsigned>(format),
                          static_cast<unsigned>(textureDesc.usage), textureDesc.mipLevelCount,
                          textureDesc.sampleCount, static_cast<void*>(texture));
  if (texture == nullptr) {
    return nullptr;
  }
  // Default view includes all mip levels (used for sampling).
  auto textureView = wgpuTextureCreateView(texture, nullptr);
  if (textureView == nullptr) {
    wgpuTextureRelease(texture);
    return nullptr;
  }
  // When a texture has mipmaps and is used as a render attachment, WebGPU requires the view
  // to reference exactly one mip level. Create a separate single-level view for rendering.
  WGPUTextureView renderView = nullptr;
  if (descriptor.mipLevelCount > 1 && (descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    renderView = wgpuTextureCreateView(texture, &viewDesc);
  }
  auto result = gpu->makeResource<WebGPUTexture>(texture, textureView, format, descriptor, true);
  result->renderView = renderView;
  return result;
}

std::shared_ptr<WebGPUTexture> WebGPUTexture::MakeFrom(WebGPUGPU* gpu, WGPUTexture texture,
                                                       uint32_t usage, bool adopted) {
  if (gpu == nullptr || texture == nullptr) {
    return nullptr;
  }
  auto format = wgpuTextureGetFormat(texture);
  auto width = static_cast<int>(wgpuTextureGetWidth(texture));
  auto height = static_cast<int>(wgpuTextureGetHeight(texture));
  auto mipLevelCount = static_cast<int>(wgpuTextureGetMipLevelCount(texture));
  auto sampleCount = static_cast<int>(wgpuTextureGetSampleCount(texture));
  TextureDescriptor descriptor = {};
  descriptor.width = width;
  descriptor.height = height;
  descriptor.format = WebGPUTextureFormatToPixelFormat(format);
  descriptor.mipLevelCount = mipLevelCount;
  descriptor.sampleCount = sampleCount;
  descriptor.usage = usage;
  auto textureView = wgpuTextureCreateView(texture, nullptr);
  if (textureView == nullptr) {
    return nullptr;
  }
  return gpu->makeResource<WebGPUTexture>(texture, textureView, format, descriptor, adopted);
}

WebGPUTexture::WebGPUTexture(WGPUTexture texture, WGPUTextureView textureView,
                             WGPUTextureFormat format, const TextureDescriptor& descriptor,
                             bool adopted)
    : Texture(descriptor), texture(texture), textureView(textureView), wgpuFormat(format),
      adopted(adopted) {
}

BackendTexture WebGPUTexture::getBackendTexture() const {
  if (!(descriptor.usage & TextureUsage::TEXTURE_BINDING)) {
    return {};
  }
  WebGPUTextureInfo info = {};
  info.texture = texture;
  info.textureView = textureView;
  info.format = static_cast<unsigned>(wgpuFormat);
  return {info, descriptor.width, descriptor.height};
}

BackendRenderTarget WebGPUTexture::getBackendRenderTarget() const {
  if (!(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    return {};
  }
  WebGPUTextureInfo info = {};
  info.texture = texture;
  info.textureView = textureView;
  info.format = static_cast<unsigned>(wgpuFormat);
  return {info, descriptor.width, descriptor.height};
}

void WebGPUTexture::onRelease(WebGPUGPU*) {
  if (renderView != nullptr) {
    wgpuTextureViewRelease(renderView);
    renderView = nullptr;
  }
  if (textureView != nullptr) {
    wgpuTextureViewRelease(textureView);
    textureView = nullptr;
  }
  if (adopted && texture != nullptr) {
    wgpuTextureRelease(texture);
  }
  texture = nullptr;
}

}  // namespace tgfx
