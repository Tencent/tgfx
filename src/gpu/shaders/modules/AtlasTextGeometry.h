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

#include <iterator>
#include "gpu/shaders/Module.h"

namespace tgfx {

/// AtlasTextGeometryProcessor's glyph quad: the atlas coordinate is the per-vertex one scaled by
/// the atlas size, the coverage is the optional per-vertex value, and the color is the per-vertex
/// one or the exact Color uniform. The atlas sample itself replaces the color for a color atlas
/// and contributes its alpha as the coverage (the former FILL_COVERAGE_ATLAS_TEXT), so the module
/// owns the atlas sampler, AlphaOnly and the size scale.
inline constexpr ModuleDim AtlasTextGeometryDims[] = {{"HAS_COVERAGE", 2}};

inline constexpr Element AtlasTextGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "Coverage", 1, 0, 0x02},
    {Element::Kind::Attribute, ShaderType::Float2, "MaskCoord"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Varying, ShaderType::Float2, "TexCoords"},
    {Element::Kind::Varying, ShaderType::Float, "Coverage", 1, 0, 0x02},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::VertexParam, ShaderType::Float2, "AtlasSizeInv"},
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "AlphaOnly"},
};

inline constexpr Module AtlasTextGeometry = {"AtlasTextGeometry",
                                             AtlasTextGeometryDims,
                                             1,
                                             AtlasTextGeometryElements,
                                             std::size(AtlasTextGeometryElements),
                                             true,
                                             true};

}  // namespace tgfx
