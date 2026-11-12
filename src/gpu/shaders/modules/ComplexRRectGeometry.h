///////////////////////////////////////////////////////////////////////////////////////////////
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
///////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

/// ComplexNonAARRectGeometryProcessor's per-corner-radius rounded rect: the position is already in
/// device space, the corner data rides the varyings and the coverage is the analytic per-corner
/// ellipse SDF (the former FILL_COVERAGE_COMPLEX_RRECT). The stroke width attribute is
/// unconditional (the fill buffers write zeros) and the StrokeEnabled uniform selects the coverage
/// form at runtime, so one variant serves both forms.
inline constexpr Element ComplexRRectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float2, "LocalCoord"},
    {Element::Kind::Attribute, ShaderType::Float4, "XRadii"},
    {Element::Kind::Attribute, ShaderType::Float4, "YRadii"},
    {Element::Kind::Attribute, ShaderType::Float4, "RectBounds"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "StrokeWidth"},
    {Element::Kind::Varying, ShaderType::Float2, "LocalCoord"},
    {Element::Kind::Varying, ShaderType::Float4, "XRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "YRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "RectBounds"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Varying, ShaderType::Float2, "StrokeWidth"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "StrokeEnabled"},
};

inline constexpr Module ComplexRRectGeometry = {
    "ComplexRRectGeometry", nullptr, 0, ComplexRRectGeometryElements, 16, true, true};

}  // namespace tgfx
