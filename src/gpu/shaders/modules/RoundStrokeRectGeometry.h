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

/// RoundStrokeRectGeometryProcessor's stroked rounded rect: the ellipse offsets, radii and the
/// per-vertex coverage ride the varyings; the AA edge is the normalized-offset SDF scaled by the
/// vertex coverage. UV_MATRIX mirrors the geometry's own flag: with a uv matrix the buffer carries
/// no uv coordinate (slot 4 empty) and the local coordinate is recovered from the transform, which
/// this shader never samples, so the attribute is simply declared away.
inline constexpr ModuleDim RoundStrokeRectGeometryDims[] = {{"UV_MATRIX", 2}};

inline constexpr Element RoundStrokeRectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "InCoverage"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEllipseOffset"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEllipseRadii"},
    {Element::Kind::Attribute, ShaderType::Float2, "InUVCoord", 1, 0, 0},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Varying, ShaderType::Float2, "Offsets"},
    {Element::Kind::Varying, ShaderType::Float, "Coverage"},
    {Element::Kind::Varying, ShaderType::Float2, "Radii"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "AAEnabled"},
};

inline constexpr Module RoundStrokeRectGeometry = {"RoundStrokeRectGeometry",
                                                   RoundStrokeRectGeometryDims,
                                                   1,
                                                   RoundStrokeRectGeometryElements,
                                                   std::size(RoundStrokeRectGeometryElements),
                                                   true,
                                                   true};

}  // namespace tgfx
