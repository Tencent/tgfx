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

// The chain's rect layout without the uvCoord attribute (DefaultGP, QuadPerEdgeAAGP without a
// uvMatrix, Mesh without texCoords). The attribute order is fixed and must pair by order with every
// serving GP's compacted list while every location stays under that GP's physical-slot ceiling:
// DefaultGP's ceiling is 3, so the color sits at ordinal 2; the subset attribute follows at 3 and
// only QuadPerEdgeAA draws (ceiling 5) carry it. Names are bare (bareNames) so the legacy kernels'
// interface spellings carry over verbatim.
// The family's ordinals decode against one flat domain across the geometries, so every chain
// geometry declares the same dim pair: SUBSET belongs to the rect classes, STROKE to the complex
// class, and the family's allowed filter pins each geometry's unused dim to 0 (the variant space
// multiplies by four and filters back to the reachable set).
inline constexpr ModuleDim ChainRectGeometryDims[] = {{"SUBSET", 2}};

inline constexpr Element ChainRectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "aPosition"},
    {Element::Kind::Attribute, ShaderType::Float, "inCoverage"},
    {Element::Kind::Attribute, ShaderType::Float4, "inColor"},
    {Element::Kind::Attribute, ShaderType::Float4, "texSubset", 1, 0, 0x02},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "texSubsetMatrix", 1, 0, 0x02},
    {Element::Kind::Varying, ShaderType::Float, "vCoverage"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Varying, ShaderType::Float4, "vTexSubset", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module ChainRectGeometry = {
    "ChainRectGeometry", ChainRectGeometryDims, 1, ChainRectGeometryElements, 11, true, true,
    /*bareNames=*/true};

}  // namespace tgfx
