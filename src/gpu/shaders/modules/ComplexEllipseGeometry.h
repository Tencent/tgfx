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

/// ComplexEllipseGeometryProcessor's ellipse: the normalized offset, radii and edge distance ride
/// the varyings; the stroke form additionally carries the stroke width attribute and varying
/// (STROKE = 0 leaves slot 5 empty). The coverage is the analytic complex-ellipse SDF, and the
/// device-space mask is the shared DeviceMask module (a compile-time PRESENT dimension).
inline constexpr ModuleDim ComplexEllipseGeometryDims[] = {{"STROKE", 2}};

inline constexpr Element ComplexEllipseGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEllipseOffset"},
    {Element::Kind::Attribute, ShaderType::Float4, "InEllipseRadii"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEdgeDist"},
    {Element::Kind::Attribute, ShaderType::Float2, "InStrokeWidth", 1, 0},
    {Element::Kind::Varying, ShaderType::Float2, "Offsets"},
    {Element::Kind::Varying, ShaderType::Float4, "Radii"},
    {Element::Kind::Varying, ShaderType::Float2, "EdgeDist"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Varying, ShaderType::Float2, "StrokeWidth", 1, 0},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module ComplexEllipseGeometry = {"ComplexEllipseGeometry",
                                                  ComplexEllipseGeometryDims,
                                                  1,
                                                  ComplexEllipseGeometryElements,
                                                  std::size(ComplexEllipseGeometryElements),
                                                  true,
                                                  true};

}  // namespace tgfx
