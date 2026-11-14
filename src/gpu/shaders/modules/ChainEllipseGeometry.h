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

// The chain's ellipse layout: device-space vertices (no Matrix), the ellipse edge varyings the body
// evaluates per pixel, and the StrokeEnabled uniform the ellipse GP writes.
// The family's ordinals decode against one flat domain across the geometries, so every chain
// geometry declares the same dim pair: SUBSET belongs to the rect classes, STROKE to the complex
// class, and the family's allowed filter pins each geometry's unused dim to 0 (the variant space
// multiplies by four and filters back to the reachable set).
inline constexpr ModuleDim ChainEllipseGeometryDims[] = {{"SUBSET", 2}};

inline constexpr Element ChainEllipseGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEllipseOffset"},
    {Element::Kind::Attribute, ShaderType::Float4, "InEllipseRadii"},
    {Element::Kind::Varying, ShaderType::Float2, "vOffsets"},
    {Element::Kind::Varying, ShaderType::Float4, "vRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "StrokeEnabled"},
};

inline constexpr Module ChainEllipseGeometry = {"ChainEllipseGeometry",
                                                ChainEllipseGeometryDims,
                                                1,
                                                ChainEllipseGeometryElements,
                                                10,
                                                true,
                                                true,
                                                /*bareNames=*/true};

}  // namespace tgfx
