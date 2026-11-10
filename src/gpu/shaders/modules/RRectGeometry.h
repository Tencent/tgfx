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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

/// RRectDrawOp's non-AA rounded-rect geometry: uniform radii, fill or stroke. The color is a
/// runtime select between the 8-bit quantized varying and the exact common-color uniform.
inline constexpr ModuleDim RRectGeometryDims[] = {{"STROKE", 2}};

inline constexpr Element RRectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float2, "LocalCoord"},
    {Element::Kind::Attribute, ShaderType::Float2, "Radii"},
    {Element::Kind::Attribute, ShaderType::Float4, "RectBounds"},
    {Element::Kind::Attribute, ShaderType::Float4, "Color"},
    {Element::Kind::Attribute, ShaderType::Float2, "StrokeWidth", 1, 0, 0x02},
    {Element::Kind::Varying, ShaderType::Float2, "LocalCoord"},
    {Element::Kind::Varying, ShaderType::Float2, "Radii"},
    {Element::Kind::Varying, ShaderType::Float4, "RectBounds"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Varying, ShaderType::Float2, "StrokeWidth", 1, 0, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "CommonColor"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module RRectGeometry = {
    "RRectGeometry", RRectGeometryDims, 1, RRectGeometryElements, 13, true, true};

}  // namespace tgfx
