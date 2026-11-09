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

/// EllipseGeometryProcessor's ellipse: the normalized offset and radii ride the varyings, the color
/// is the per-vertex one or the exact Color uniform, and StrokeEnabled scales the offset by the
/// radii inside the SDF (the former FILL_COVERAGE_ELLIPSE). The device-space mask stays on the
/// legacy family until its ALWAYS/RUNTIME semantics are split, so this module declares no mask.
inline constexpr Element EllipseGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "InEllipseOffset"},
    {Element::Kind::Attribute, ShaderType::Float4, "InEllipseRadii"},
    {Element::Kind::Varying, ShaderType::Float2, "Offsets"},
    {Element::Kind::Varying, ShaderType::Float4, "Radii"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "StrokeEnabled"},
};

inline constexpr Module EllipseGeometry = {"EllipseGeometry",
                                           nullptr,
                                           0,
                                           EllipseGeometryElements,
                                           std::size(EllipseGeometryElements),
                                           true,
                                           true};

}  // namespace tgfx
