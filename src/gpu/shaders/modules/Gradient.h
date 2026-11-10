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

/// The clamped single-interval gradient over the geometry's color: the coordinate rides the
/// gradient layout's own transform (its FP carries one, the geometry's uv matrix stays on the
/// vertex side), the four layout maths share one branch selected by LayoutType, and the clamped
/// endpoints carry the single-interval colorizer's colors. The input color's alpha modulates the
/// result, which is how the paint alpha reaches the gradient. Only the clamped single-interval
/// form is expressible, which is exactly the form the families accept.
inline constexpr ModuleDim GradientDims[] = {{"PRESENT", 2}};

inline constexpr Element GradientElements[] = {
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix", 1, 0, 0x02},
    {Element::Kind::Varying, ShaderType::Float2, "Coord", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "LeftBorderColor", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "RightBorderColor", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Int, "LayoutType", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float, "Bias", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float, "Scale", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "Start", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "End", 1, 0, 0x02},
};

// Module suffix: tgfx::Gradient is a shader class; the declared name stays "Gradient".
inline constexpr Module GradientModule = {
    "Gradient", GradientDims, 1, GradientElements, std::size(GradientElements), true, true};

}  // namespace tgfx
