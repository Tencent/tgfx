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

#include <iterator>
#include "gpu/shaders/Module.h"

namespace tgfx {

/// ComplexEllipseGeometryProcessor's ellipse: the normalized offset, radii and edge distance ride
/// the varyings. The stroke width attribute is unconditional (the fill buffers write zeros) and the
/// StrokeEnabled uniform selects the coverage form at runtime, so one variant serves both forms.
/// The coverage is the analytic complex-ellipse SDF, and the device-space mask is the shared
/// DeviceMask module (a compile-time PRESENT dimension).
inline constexpr Element ComplexEllipseGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEllipseOffset"},
    {Element::Kind::Attribute, ShaderType::Float4, "InEllipseRadii"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEdgeDist"},
    {Element::Kind::Attribute, ShaderType::Float2, "InStrokeWidth"},
    {Element::Kind::Varying, ShaderType::Float2, "Offsets"},
    {Element::Kind::Varying, ShaderType::Float4, "Radii"},
    {Element::Kind::Varying, ShaderType::Float2, "EdgeDist"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Varying, ShaderType::Float2, "StrokeWidth"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "StrokeEnabled"},
};

inline constexpr Module ComplexEllipseGeometry = {"ComplexEllipseGeometry",
                                                  nullptr,
                                                  0,
                                                  ComplexEllipseGeometryElements,
                                                  std::size(ComplexEllipseGeometryElements),
                                                  true,
                                                  true};

}  // namespace tgfx
