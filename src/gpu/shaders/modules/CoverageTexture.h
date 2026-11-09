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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <iterator>
#include "gpu/shaders/Module.h"

namespace tgfx {

/// A texture read as the draw's coverage: the sample replaces the running coverage (the geometry's
/// per-vertex coverage slot is a stride filler in the instanced batch, so the texture IS the
/// coverage), narrowed to the leaf's subset rectangle in normalized texture space, with an
/// alpha-only read broadcasting the red channel. The coordinate is the geometry's sampling
/// coordinate — its coord matrix already put it in texture space — so this module carries no
/// transform of its own and pairs with a geometry that provides one.
inline constexpr Element CoverageTextureElements[] = {
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
    {Element::Kind::Param, ShaderType::Float4, "Subset"},
    {Element::Kind::Param, ShaderType::Int, "AlphaOnly"},
};

inline constexpr Module CoverageTexture = {"CoverageTexture",
                                           nullptr,
                                           0,
                                           CoverageTextureElements,
                                           std::size(CoverageTextureElements),
                                           false,
                                           true};

}  // namespace tgfx
