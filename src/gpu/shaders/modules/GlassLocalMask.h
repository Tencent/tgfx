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

#include <iterator>
#include "gpu/shaders/Module.h"

namespace tgfx {

/// The glass quad's local shape mask (the former quad branch's HasLocalMask runtime gate): the
/// SrcIn ShaderMaskFilter's coverage, applied as an always-declared runtime branch — the module
/// exists in every GlassRefractionQuad variant and HasMask turns the sampling on, so the mask
/// never widens the variant space (a draw without a mask binds the shared dummy texture on the
/// never-sampled slot). The mask transform is affine by contract (the matcher rejects a
/// projective mask), so the varying interpolates a plain vec2 — dividing an interpolated z is NOT
/// bit-identical for an affine matrix, and this family must keep the legacy kernel's pixels.
inline constexpr Element GlassLocalMaskElements[] = {
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
    {Element::Kind::Varying, ShaderType::Float2, "Coord"},
    {Element::Kind::Param, ShaderType::Int, "HasMask"},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
};

inline constexpr Module GlassLocalMask = {"GlassLocalMask",
                                          nullptr,
                                          0,
                                          GlassLocalMaskElements,
                                          std::size(GlassLocalMaskElements),
                                          true,
                                          true};

}  // namespace tgfx
