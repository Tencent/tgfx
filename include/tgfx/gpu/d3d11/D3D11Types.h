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

#pragma once

#include <cstdint>

namespace tgfx {
/**
 * D3D11 texture info for importing a D3D11 shared texture into tgfx.
 * The texture must be created with D3D11_RESOURCE_MISC_SHARED flag and
 * DXGI_FORMAT_B8G8R8A8_UNORM format. tgfx will use WGL_NV_DX_interop or
 * GL_EXT_memory_object to share it with OpenGL.
 */
struct D3D11TextureInfo {
  /**
   * The ID3D11Device* that owns the texture. Must remain valid while the
   * BackendTexture is in use.
   */
  void* device = nullptr;

  /**
   * The ID3D11Texture2D* to share with OpenGL. Must be created with
   * D3D11_RESOURCE_MISC_SHARED and DXGI_FORMAT_B8G8R8A8_UNORM.
   */
  void* texture = nullptr;
};
}  // namespace tgfx
