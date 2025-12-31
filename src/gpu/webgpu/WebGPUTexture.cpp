/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "WebGPUTexture.h"
#include "WebGPUUtil.h"

namespace tgfx {

WebGPUTexture::WebGPUTexture(const TextureDescriptor& descriptor, wgpu::Texture texture)
    : Texture(descriptor), _texture(texture) {
}

WebGPUTexture::~WebGPUTexture() {
  if (_texture != nullptr) {
    _texture.Destroy();
  }
}

wgpu::TextureView WebGPUTexture::createTextureView(wgpu::TextureViewDimension dimension,
                                                   uint32_t baseMipLevel, uint32_t mipLevelCount,
                                                   uint32_t baseArrayLayer,
                                                   uint32_t arrayLayerCount,
                                                   wgpu::TextureAspect aspect) const {
  if (_texture == nullptr) {
    return nullptr;
  }
  wgpu::TextureViewDescriptor viewDesc = {};
  viewDesc.format = ToWGPUTextureFormat(descriptor.format);
  viewDesc.dimension = dimension;
  viewDesc.baseMipLevel = baseMipLevel;
  viewDesc.mipLevelCount = mipLevelCount;
  viewDesc.baseArrayLayer = baseArrayLayer;
  viewDesc.arrayLayerCount = arrayLayerCount;
  viewDesc.aspect = aspect;
  return _texture.CreateView(&viewDesc);
}

BackendTexture WebGPUTexture::getBackendTexture() const {
  if (_texture == nullptr || !(descriptor.usage & TextureUsage::TEXTURE_BINDING)) {
    return {};
  }
  WebGPUTextureInfo info = {};
  info.texture = _texture.Get();
  info.format = static_cast<unsigned>(_texture.GetFormat());
  return {info, descriptor.width, descriptor.height};
}

BackendRenderTarget WebGPUTexture::getBackendRenderTarget() const {
  if (_texture == nullptr || !(descriptor.usage & TextureUsage::RENDER_ATTACHMENT)) {
    return {};
  }
  WebGPUTextureInfo info = {};
  info.texture = _texture.Get();
  info.format = static_cast<unsigned>(_texture.GetFormat());
  return {info, descriptor.width, descriptor.height};
}

}  // namespace tgfx
