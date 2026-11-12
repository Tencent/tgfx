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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

// The chain's hairline-quad layout (GP_LAYOUT 9): the Loop-Blinn quad edge data, the view
// transform and the uniforms the edge coverage reads. HairlineQuadGeometryLayout's provider carries
// no color attribute, so vColor is the white filler the body's color macro needs; the exact color
// always rides the Color uniform (the layout always has a common color).
// The family's ordinals decode against one flat domain across the geometries, so the dim pair
// matches every other chain geometry and the allowed filter pins both dims to 0.
inline constexpr ModuleDim ChainHairlineQuadGeometryDims[] = {{"SUBSET", 2}};

inline constexpr Element ChainHairlineQuadGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "aPosition"},
    {Element::Kind::Attribute, ShaderType::Float4, "aHairQuadEdge"},
    {Element::Kind::Varying, ShaderType::Float4, "vHairQuadEdge"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Float, "Coverage"},
    {Element::Kind::Param, ShaderType::Int, "AAEnabled"},
};

inline constexpr Module ChainHairlineQuadGeometry = {"ChainHairlineQuadGeometry",
                                                     ChainHairlineQuadGeometryDims,
                                                     1,
                                                     ChainHairlineQuadGeometryElements,
                                                     9,
                                                     true,
                                                     true,
                                                     /*bareNames=*/true};

}  // namespace tgfx
