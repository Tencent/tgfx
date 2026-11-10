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

// The chain's rect layout with the uvCoord attribute (QuadPerEdgeAAGP with a uvMatrix, Mesh with
// texCoords). Ordinals 0..4 stand under the quad layout's five physical slots; a Mesh draw declares
// 0..3 and stays under that GP's ceiling of 4. CoordSourceMask selects the leaf coordinate source
// per leaf at runtime, exactly as the legacy kernel did.
// The family's ordinals decode against one flat domain across the geometries, so every chain
// geometry declares the same dim pair: SUBSET belongs to the rect classes, STROKE to the complex
// class, and the family's allowed filter pins each geometry's unused dim to 0 (the variant space
// multiplies by four and filters back to the reachable set).
inline constexpr ModuleDim ChainRectUVGeometryDims[] = {{"SUBSET", 2}, {"STROKE", 2}};

inline constexpr Element ChainRectUVGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "aPosition"},
    {Element::Kind::Attribute, ShaderType::Float, "inCoverage"},
    {Element::Kind::Attribute, ShaderType::Float2, "uvCoord"},
    {Element::Kind::Attribute, ShaderType::Float4, "inColor"},
    {Element::Kind::Attribute, ShaderType::Float4, "texSubset", 1, 0, 0x02},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::VertexParam, ShaderType::Int, "CoordSourceMask"},
    {Element::Kind::Varying, ShaderType::Float, "vCoverage"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Varying, ShaderType::Float4, "vTexSubset", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module ChainRectUVGeometry = {
    "ChainRectUVGeometry", ChainRectUVGeometryDims, 2, ChainRectUVGeometryElements, 12, true, true,
    /*bareNames=*/true};

}  // namespace tgfx
