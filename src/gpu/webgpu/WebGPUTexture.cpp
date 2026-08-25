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
#include <string>
#include "WebGPUDefines.h"
#include "WebGPUGPU.h"
#include "WebGPUUtil.h"
#include "core/utils/Log.h"

namespace tgfx {

std::shared_ptr<WebGPUTexture> WebGPUTexture::Make(WebGPUGPU* gpu,
                                                   const TextureDescriptor& descriptor) {
  if (gpu == nullptr || descriptor.width <= 0 || descriptor.height <= 0) {
    return nullptr;
  }
  auto format = gpu->getWGPUTextureFormat(descriptor.format);
  // The backend's own mechanisms (render-pass mip blits in generateMipmapsForTexture, offscreen
  // fills that render into sampling textures) can turn any created texture into a render target,
  // and WebGPU validates usages strictly. All formats this backend creates are renderable, so
  // every texture carries RENDER_ATTACHMENT regardless of what the caller declared.
  uint32_t usage = descriptor.usage | TextureUsage::RENDER_ATTACHMENT;
  WGPUTextureDescriptor textureDesc = {};
  textureDesc.dimension = WGPUTextureDimension_2D;
  textureDesc.size = {static_cast<uint32_t>(descriptor.width),
                      static_cast<uint32_t>(descriptor.height), 1};
  textureDesc.format = format;
  textureDesc.mipLevelCount = static_cast<uint32_t>(descriptor.mipLevelCount);
  textureDesc.sampleCount = static_cast<uint32_t>(descriptor.sampleCount);
  textureDesc.usage = ToWGPUTextureUsage(usage);
  std::string label = "tgfx-tex-" + std::to_string(descriptor.width) + "x" +
                      std::to_string(descriptor.height) + "-mips" +
                      std::to_string(descriptor.mipLevelCount) + "-f" +
                      std::to_string(static_cast<int>(descriptor.format));
  textureDesc.label = label.c_str();
  auto texture = wgpuDeviceCreateTexture(gpu->device(), &textureDesc);
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
  if (descriptor.mipLevelCount > 1 && (usage & TextureUsage::RENDER_ATTACHMENT)) {
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    renderView = wgpuTextureCreateView(texture, &viewDesc);
    if (renderView == nullptr) {
      wgpuTextureViewRelease(textureView);
      wgpuTextureRelease(texture);
      return nullptr;
    }
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
    if (adopted) {
      wgpuTextureRelease(texture);
    }
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
  info.format = static_cast<uint32_t>(wgpuFormat);
  return BackendTexture(info, descriptor.width, descriptor.height);
}

BackendRenderTarget WebGPUTexture::getBackendRenderTarget() const {
  if (!(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    return {};
  }
  WebGPUTextureInfo info = {};
  info.texture = texture;
  info.textureView = textureView;
  info.format = static_cast<uint32_t>(wgpuFormat);
  return BackendRenderTarget(info, descriptor.width, descriptor.height);
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
