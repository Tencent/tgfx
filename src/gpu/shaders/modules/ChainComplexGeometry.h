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

// The chain's complex rounded-rect layout (GP_LAYOUT 3): device-space vertices like the ellipse,
// the per-corner edge distances, and the half-stroke-width attribute. The attribute is
// unconditional (the fill buffers write zeros) and the StrokeEnabled uniform selects the coverage
// form at runtime, so one variant serves both forms.
// The family's ordinals decode against one flat domain across the geometries, so every chain
// geometry declares the same dim: SUBSET belongs to the rect classes, and the family's allowed
// filter pins each geometry's unused value to 0.
inline constexpr ModuleDim ChainComplexGeometryDims[] = {{"SUBSET", 2}};

inline constexpr Element ChainComplexGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEllipseOffset"},
    {Element::Kind::Attribute, ShaderType::Float4, "InEllipseRadii"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEdgeDist"},
    {Element::Kind::Attribute, ShaderType::Float2, "InStrokeWidth"},
    {Element::Kind::Varying, ShaderType::Float2, "vOffsets"},
    {Element::Kind::Varying, ShaderType::Float4, "vRadii"},
    {Element::Kind::Varying, ShaderType::Float2, "vEdgeDist"},
    {Element::Kind::Varying, ShaderType::Float2, "vStrokeWidth"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "StrokeEnabled"},
};

inline constexpr Module ChainComplexGeometry = {"ChainComplexGeometry",
                                                ChainComplexGeometryDims,
                                                1,
                                                ChainComplexGeometryElements,
                                                14,
                                                true,
                                                true,
                                                /*bareNames=*/true};

}  // namespace tgfx
