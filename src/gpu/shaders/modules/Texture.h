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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

/// A plain RGBA texture source (the former FILL_SOURCE_TEXTURE): the coordinate rides its own
/// transform (HAS_SUBSET narrows it through the per-vertex subset bounds), and the leaf parameters
/// select the read swizzle path, the alpha-only read and the RGBAAA dual-plane alpha.
inline constexpr ModuleDim TextureDims[] = {{"HAS_SUBSET", 2}};

inline constexpr Element TextureElements[] = {
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "SubsetMatrix"},
    {Element::Kind::Varying, ShaderType::Float3, "Coord"},
    {Element::Kind::Varying, ShaderType::Float4, "TexSubset", 1, 0},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
    {Element::Kind::Param, ShaderType::Float4, "Subset"},
    {Element::Kind::Param, ShaderType::Int, "SubsetClampOn"},
    {Element::Kind::Param, ShaderType::Float2, "AlphaStart"},
    {Element::Kind::Param, ShaderType::Int, "AlphaOnly"},
    {Element::Kind::Param, ShaderType::Int, "HasRgbaaa"},
};

inline constexpr Module Texture = {
    "Texture", TextureDims, 1, TextureElements, std::size(TextureElements), true, true};

}  // namespace tgfx
