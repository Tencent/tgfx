/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
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

#include "gpu/shaders/FillKernel.h"

namespace tgfx {

/**
 * Precompiled shader for TiledTextureEffect fills: texture sampling with per-axis tiling modes
 * (repeat, mirror, clamp-to-border, and the mipmap-repeat seam blend) applied through runtime
 * uniform branches. ShaderModeX/Y, AlphaOnly, Strict, and perspective rejection are runtime or
 * acceptance facts, not dimensions. The tiled leaf's recipe comes off the carried IR, so the
 * acceptance is defined in the family's .cpp.
 */
class TiledTextureFillShader : public FillKernel<TiledTextureFillShader> {
 public:
  static constexpr const char* Name = "TiledTextureFillShader";
  static constexpr const char* VertFile = "kernels/tiled_texture_fill.vert";
  static constexpr const char* FragFile = "kernels/tiled_texture_fill.frag";
  static constexpr bool ServesAlphaOnly = false;

  // The kernel implements the GRAY_8 / RG_88 runtime read swizzle (texture_read_swizzle.inc), so
  // the matcher may route such texture reads here instead of refusing the draw.
  static constexpr uint32_t TextureReads = ShaderTextureReadSwizzle;

  /// Fragment dimensions: HAS_XP (0=passthrough, 1=dst texture, 2=framebuffer fetch).
  static constexpr Dim fragDims[] = {{XP_DEFINE_NAME, XP_VALUE_COUNT}};

  static bool Accept(const DrawShape& shape);
  static Variants Select(const DrawShape& shape);
};

}  // namespace tgfx
