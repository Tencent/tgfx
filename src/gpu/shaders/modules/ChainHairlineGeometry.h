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

// The chain's hairline-line layout: position through the Matrix uniform, the edge-distance varying
// whose per-pixel coverage the body evaluates, and the Coverage / AAEnabled uniforms the hairline
// GP writes.
// The family's ordinals decode against one flat domain across the geometries, so every chain
// geometry declares the same dim pair: SUBSET belongs to the rect classes, STROKE to the complex
// class, and the family's allowed filter pins each geometry's unused dim to 0 (the variant space
// multiplies by four and filters back to the reachable set).
inline constexpr ModuleDim ChainHairlineGeometryDims[] = {{"SUBSET", 2}};

inline constexpr Element ChainHairlineGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "aPosition"},
    {Element::Kind::Attribute, ShaderType::Float, "aEdgeDistance"},
    {Element::Kind::Attribute, ShaderType::Float4, "inColor"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::Varying, ShaderType::Float, "vEdgeDistance"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Float, "Coverage"},
    {Element::Kind::Param, ShaderType::Int, "AAEnabled"},
};

inline constexpr Module ChainHairlineGeometry = {"ChainHairlineGeometry",
                                                 ChainHairlineGeometryDims,
                                                 1,
                                                 ChainHairlineGeometryElements,
                                                 10,
                                                 true,
                                                 true,
                                                 /*bareNames=*/true};

}  // namespace tgfx
