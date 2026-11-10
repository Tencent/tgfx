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

#pragma once

#include <iterator>
#include "gpu/shaders/Module.h"

namespace tgfx {

/// The glass UDF tent-blur body (the former kernels/glass_udf_tent_blur.frag): a one-directional
/// tent kernel whose weights are computed analytically from the per-field radii, one texture tap
/// per step, writing one field of the UDF (the 24-bit fine field into RGB, the coarse field into
/// A). The child read is a runtime branch, not a dimension: TiledChild selects the shared per-tap
/// tiling math, exactly as the Gaussian blur does.
inline constexpr Element GlassUDFTentBlurElements[] = {
    // The per-field radii: x is the fine field's, y the coarse one's; Field selects.
    {Element::Kind::Param, ShaderType::Float2, "Radius"},
    {Element::Kind::Param, ShaderType::Float2, "Step"},
    {Element::Kind::Param, ShaderType::Float4, "Subset"},
    {Element::Kind::Param, ShaderType::Int, "ShaderModeX"},
    {Element::Kind::Param, ShaderType::Int, "ShaderModeY"},
    {Element::Kind::Param, ShaderType::Float4, "Clamp"},
    {Element::Kind::Param, ShaderType::Float2, "Dimension"},
    // 0 = the fine field (RGB, 24-bit packed), 1 = the coarse field (A).
    {Element::Kind::Param, ShaderType::Int, "Field"},
    // 1 when the source already produces this processor's own layout, so the fine field decodes
    // its RGB lanes instead of reading alpha.
    {Element::Kind::Param, ShaderType::Int, "InputIsPacked"},
    {Element::Kind::Param, ShaderType::Int, "TiledChild"},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
};

inline constexpr Module GlassUDFTentBlur = {"GlassUDFTentBlur",
                                            nullptr,
                                            0,
                                            GlassUDFTentBlurElements,
                                            std::size(GlassUDFTentBlurElements),
                                            false,
                                            true};

}  // namespace tgfx
