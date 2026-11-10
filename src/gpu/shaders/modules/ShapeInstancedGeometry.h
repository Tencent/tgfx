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

#include "gpu/shaders/Module.h"

namespace tgfx {

/// ShapeInstancedGeometryProcessor's instanced path triangles: the position goes through the view
/// Matrix and then shifts by the per-instance offset, the local coordinate is recovered through
/// the geometry's uv matrix, the color is the per-instance one and the coverage rides the
/// per-vertex slot. The element order mirrors the geometry's declared attribute order
/// (position, coverage, color, offset, instanceColor); VERTEX_COLOR reserves slot 2 for the
/// geometry's stride-filler color attribute, which this shader never reads, so the instance
/// attributes keep their physical slots 3 and 4.
inline constexpr ModuleDim ShapeInstancedGeometryDims[] = {{"VERTEX_COLOR", 2}};

inline constexpr Element ShapeInstancedGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "Coverage"},
    {Element::Kind::Attribute, ShaderType::Float4, "VertexColor", 1, 0, 0x02},
    {Element::Kind::InstanceAttribute, ShaderType::Float2, "Offset"},
    {Element::Kind::InstanceAttribute, ShaderType::Float4, "InstanceColor"},
    {Element::Kind::Varying, ShaderType::Float2, "Coord"},
    {Element::Kind::Varying, ShaderType::Float, "Coverage"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "UVMatrix"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "ViewMatrix"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
};

inline constexpr Module ShapeInstancedGeometry = {"ShapeInstancedGeometry",
                                                  ShapeInstancedGeometryDims,
                                                  1,
                                                  ShapeInstancedGeometryElements,
                                                  11,
                                                  true,
                                                  true};

}  // namespace tgfx
