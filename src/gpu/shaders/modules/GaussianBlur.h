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

inline constexpr Element GaussianBlurElements[] = {
    {Element::Kind::Param, ShaderType::Float4, "Kernel", 17},
    {Element::Kind::Param, ShaderType::Int, "Radius"},
    {Element::Kind::Param, ShaderType::Float2, "Step"},
    {Element::Kind::Param, ShaderType::Float4, "Subset"},
    {Element::Kind::Param, ShaderType::Int, "ShaderModeX"},
    {Element::Kind::Param, ShaderType::Int, "ShaderModeY"},
    {Element::Kind::Param, ShaderType::Float4, "Clamp"},
    {Element::Kind::Param, ShaderType::Float2, "Dimension"},
    {Element::Kind::Param, ShaderType::Int, "TiledChild"},
    {Element::Kind::Param, ShaderType::Int, "AlphaChild"},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
};

inline constexpr Module GaussianBlur = {
    "GaussianBlur", nullptr, 0, GaussianBlurElements, std::size(GaussianBlurElements), false, true};

}  // namespace tgfx
