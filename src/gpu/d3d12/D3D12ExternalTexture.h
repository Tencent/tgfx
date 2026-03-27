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

#pragma once

#include "D3D12Texture.h"

namespace tgfx {

class D3D12ExternalTexture : public D3D12Texture {
 public:
  D3D12ExternalTexture(const TextureDescriptor& descriptor, ComPtr<ID3D12Resource> resource,
                       unsigned dxgiFormat)
      : D3D12Texture(descriptor, std::move(resource), dxgiFormat) {
  }

 protected:
  void onReleaseTexture() override {
    // External textures are not owned by TGFX, so we do not release them.
    // Clear the reference to avoid preventing COM Release from the real owner.
    resource = nullptr;
  }
};

}  // namespace tgfx
