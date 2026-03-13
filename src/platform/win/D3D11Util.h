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

#include "D3D11Defines.h"

namespace tgfx {

/**
 * Increments the reference count of the given D3D11 texture.
 */
void D3D11AddRef(HardwareBufferRef texture);

/**
 * Decrements the reference count of the given D3D11 texture.
 */
void D3D11Release(HardwareBufferRef texture);

/**
 * Retrieves the texture descriptor from an ID3D11Texture2D pointer via vtable call.
 * Returns false if texture is null or the dimensions are zero.
 */
bool D3D11GetTextureDesc(HardwareBufferRef texture, D3D11Texture2DDesc* outDesc);

/**
 * Retrieves the ID3D11Device that owns the given ID3D11Texture2D. The returned pointer does not
 * carry an extra reference (the reference added by GetDevice is immediately released).
 */
ID3D11Device* D3D11GetDeviceFromTexture(HardwareBufferRef texture);

}  // namespace tgfx
