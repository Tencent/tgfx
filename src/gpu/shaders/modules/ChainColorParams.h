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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

// The chain interpreter's geometry-facing runtime switches, owned by the chain instead of spread
// across nine geometry modules whose declared counterparts name them differently (Color vs
// CommonColor, present or absent per kind). The body reads them through this module's prefixed
// names, so the reads are stable regardless of which geometry the variant carries.
inline constexpr Element ChainColorParamsElements[] = {
    // The exact paint color, selected at runtime against the 8-bit quantized per-vertex channel.
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    // 1 when the color is the exact uniform (the quantized attribute is skipped).
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    // 1 when the geometry is the stroke form (the fill and stroke share one variant).
    {Element::Kind::Param, ShaderType::Int, "StrokeEnabled"},
    // 1 when the hairline runs its anti-aliased edge.
    {Element::Kind::Param, ShaderType::Int, "AAEnabled"},
    // The hairline's paint coverage factor.
    {Element::Kind::Param, ShaderType::Float, "Coverage"},
};

inline constexpr Module ChainColorParams = {
    "ChainColorParams",  nullptr, 0, ChainColorParamsElements, 5, false, true,
    /*bareNames=*/false,
};

}  // namespace tgfx
