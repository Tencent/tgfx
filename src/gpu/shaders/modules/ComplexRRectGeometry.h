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

/// ComplexNonAARRectGeometryProcessor's per-corner-radius rounded rect: the position is already in
/// device space, the corner data rides the varyings and the coverage is the analytic per-corner
/// ellipse SDF (the former FILL_COVERAGE_COMPLEX_RRECT). The stroke form carries the stroke width
/// attribute and varying; the fill form drops them (STROKE = 0), which is the family's only dim.
inline constexpr ModuleDim ComplexRRectGeometryDims[] = {{"STROKE", 2}};

inline constexpr Element ComplexRRectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float2, "LocalCoord"},
    {Element::Kind::Attribute, ShaderType::Float4, "XRadii"},
    {Element::Kind::Attribute, ShaderType::Float4, "YRadii"},
    {Element::Kind::Attribute, ShaderType::Float4, "RectBounds"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "StrokeWidth", 1, 0, 0x02},
    {Element::Kind::Varying, ShaderType::Float2, "LocalCoord"},
    {Element::Kind::Varying, ShaderType::Float4, "XRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "YRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "RectBounds"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Varying, ShaderType::Float2, "StrokeWidth", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module ComplexRRectGeometry = {"ComplexRRectGeometry",
                                                ComplexRRectGeometryDims,
                                                1,
                                                ComplexRRectGeometryElements,
                                                15,
                                                true,
                                                true};

}  // namespace tgfx
