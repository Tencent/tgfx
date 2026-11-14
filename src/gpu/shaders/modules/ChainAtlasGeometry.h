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

#include "gpu/shaders/Module.h"

namespace tgfx {

// The chain's atlas layout: no coverage slot, the glyph mask coordinate on the second attribute
// (the atlas GP declares position, coverage (absent), maskCoord, color — physical slots 0, 2, 3, so
// ordinals 0..2 stay under its ceiling of 4 and pair by order).
// The family's ordinals decode against one flat domain across the geometries, so every chain
// geometry declares the same dim pair: SUBSET belongs to the rect classes, STROKE to the complex
// class, and the family's allowed filter pins each geometry's unused dim to 0 (the variant space
// multiplies by four and filters back to the reachable set).
inline constexpr ModuleDim ChainAtlasGeometryDims[] = {{"SUBSET", 2}};

inline constexpr Element ChainAtlasGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float2, "UVCoord"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::VertexParam, ShaderType::Int, "CoordSourceMask"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module ChainAtlasGeometry = {
    "ChainAtlasGeometry", ChainAtlasGeometryDims, 1, ChainAtlasGeometryElements, 8, true, true,
    /*bareNames=*/true};

}  // namespace tgfx
