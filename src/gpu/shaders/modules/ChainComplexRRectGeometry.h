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

// The chain's per-corner-radius non-AA rounded-rect layout (GP_LAYOUT 7 fill / 8 stroke):
// device-space vertices, the per-corner x/y radii and the bounds the per-pixel SDF reads, and the
// half-stroke-width attribute only the stroke form carries. The attributes pair by ordinal with
// RRectsVertexProvider's non-AA complex buffer.
// The family's ordinals decode against one flat domain across the geometries, so the dim pair
// matches every other chain geometry and the allowed filter pins this geometry's unused dim.
inline constexpr ModuleDim ChainComplexRRectGeometryDims[] = {{"SUBSET", 2}, {"STROKE", 2}};

inline constexpr Element ChainComplexRRectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "inPosition"},
    {Element::Kind::Attribute, ShaderType::Float2, "inLocalCoord"},
    {Element::Kind::Attribute, ShaderType::Float4, "inXRadii"},
    {Element::Kind::Attribute, ShaderType::Float4, "inYRadii"},
    {Element::Kind::Attribute, ShaderType::Float4, "inRectBounds"},
    {Element::Kind::Attribute, ShaderType::Float4, "inColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "inStrokeWidth", 1, 1, 0x02},
    {Element::Kind::Varying, ShaderType::Float2, "vLocalCoord"},
    {Element::Kind::Varying, ShaderType::Float4, "vXRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "vYRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "vRectBounds"},
    {Element::Kind::Varying, ShaderType::Float2, "vStrokeWidth", 1, 1, 0x02},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module ChainComplexRRectGeometry = {"ChainComplexRRectGeometry",
                                                     ChainComplexRRectGeometryDims,
                                                     2,
                                                     ChainComplexRRectGeometryElements,
                                                     15,
                                                     true,
                                                     true,
                                                     /*bareNames=*/true};

}  // namespace tgfx
