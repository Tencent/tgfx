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

/// A tiled texture as the color source. FULL selects the 4-tap mipmap-repeat path (the
/// TiledTextureFill family; not yet assembled by any family). PRESENT carries the module:
/// 0 = the module is absent (a solid fill), 1 = the texture rides the fill.
inline constexpr ModuleDim TiledTextureDims[] = {{"FULL", 2}, {"PRESENT", 2}};

inline constexpr Element TiledTextureElements[] = {
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix", 1, 1},
    {Element::Kind::Param, ShaderType::Int, "ShaderModeX", 1, 1},
    {Element::Kind::Param, ShaderType::Int, "ShaderModeY", 1, 1},
    {Element::Kind::Param, ShaderType::Float4, "Subset", 1, 1},
    {Element::Kind::Param, ShaderType::Float4, "Clamp", 1, 1},
    {Element::Kind::Param, ShaderType::Float2, "Dimension", 1, 1},
    {Element::Kind::Param, ShaderType::Int, "AlphaOnly", 1, 1},
    {Element::Kind::Param, ShaderType::Int, "Strict", 1, 1},
    // The runtime texture read swizzle (texture_read_swizzle.inc); the full nine-mode path
    // decodes GRAY_8/RG_88 reads with it, the single-tap path has no use for it.
    {Element::Kind::Param, ShaderType::Int, "ReadSwizzle", 1, 0},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler", 1, 1},
    {Element::Kind::Varying, ShaderType::Float2, "Coord", 1, 1},
};

inline constexpr Module TiledTexture = {
    "TiledTexture", TiledTextureDims, 2, TiledTextureElements, 11, true, true};

}  // namespace tgfx
