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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

/// The implicit output module every family carries last: the PorterDuff composition, the dst
/// texture (or the framebuffer-fetch dialect) and the blend mode. HAS_XP selects how the
/// destination color reaches the fragment: 0 = none, 1 = dst texture, 2 = framebuffer fetch.
/// Frame and Xfer are implicit singletons: their dims, macros and field names carry no module
/// prefix, so the assembled text spells HAS_XP and DstTextureUpperLeft exactly like the shared
/// xp contract the rest of the pipeline reads.
inline constexpr ModuleDim XferDims[] = {{"HAS_XP", 3}};

/// The fields exist whenever HAS_XP != 0. The dst-coord pair and the dst sampler are read only by
/// the HAS_XP == 1 path, but they stay declared under HAS_XP == 2 too (the shader never reads
/// them; the runtime binds the shared dummy texture) so one `when` rule covers them.
inline constexpr Element XferElements[] = {
    {Element::Kind::Param, ShaderType::Float2, "DstTextureUpperLeft", 1, 0},
    {Element::Kind::Param, ShaderType::Float2, "DstTextureCoordScale", 1, 0},
    {Element::Kind::Param, ShaderType::Int, "XPBlendMode", 1, 0},
    // The render-target write swizzle: 1 for ALPHA_8 targets (AAAA), 0 (a no-op) otherwise. It
    // stays unconditional because the HAS_XP == 0 path writes the composed color too.
    {Element::Kind::Param, ShaderType::Int, "OutputAlphaSwizzle"},
    // Only the dst-texture dialect declares the sampler: the framebuffer-fetch dialect reads the
    // destination through the subpass input, and a declared-but-unbound sampler would shift every
    // later texture binding at runtime.
    {Element::Kind::Texture, ShaderType::Float4, "DstTextureSampler", 1, 0, 1},
};

inline constexpr Module Xfer = {"Xfer", XferDims, 1, XferElements, 5, false, true};

inline const Module& XferModule() {
  return Xfer;
}

}  // namespace tgfx
