/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#pragma once

#include "gpu/webgpu/WebGPUTexture.h"

namespace tgfx {
class WebGPUExternalTexture : public WebGPUTexture {
 public:
  WebGPUExternalTexture(const TextureDescriptor& descriptor, wgpu::Texture texture)
      : WebGPUTexture(descriptor, texture) {
  }

  ~WebGPUExternalTexture() override {
    // External textures are not owned by TGFX, so we do not destroy them.
    // Reset the internal texture to prevent the base class destructor from destroying it.
    _texture = nullptr;
  }
};
}  // namespace tgfx
