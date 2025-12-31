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

#pragma once

#include <webgpu/webgpu_cpp.h>
#include "tgfx/gpu/Texture.h"

namespace tgfx {

class WebGPUTexture : public Texture {
 public:
  WebGPUTexture(const TextureDescriptor& descriptor, wgpu::Texture texture);

  ~WebGPUTexture() override;

  wgpu::Texture wgpuTexture() const {
    return _texture;
  }

  wgpu::TextureView createTextureView(
      wgpu::TextureViewDimension dimension = wgpu::TextureViewDimension::e2D,
      uint32_t baseMipLevel = 0, uint32_t mipLevelCount = 1, uint32_t baseArrayLayer = 0,
      uint32_t arrayLayerCount = 1, wgpu::TextureAspect aspect = wgpu::TextureAspect::All) const;

  BackendTexture getBackendTexture() const override;

  BackendRenderTarget getBackendRenderTarget() const override;

 protected:
  wgpu::Texture _texture = nullptr;
};
}  // namespace tgfx
