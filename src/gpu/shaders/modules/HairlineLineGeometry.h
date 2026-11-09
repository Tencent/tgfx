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

/// HairlineLineGeometryProcessor's thin line: the view transform, the per-vertex edge distance
/// (its absolute value times the draw coverage is the edge alpha) and the analytic clip the old
/// declaration carried unconditionally (HasClip == 0 makes the clip a no-op).
inline constexpr Element HairlineLineGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "InEdge"},
    {Element::Kind::Varying, ShaderType::Float, "Edge"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Float, "Coverage"},
    {Element::Kind::Param, ShaderType::Int, "AAEnabled"},
    {Element::Kind::Param, ShaderType::Float4, "Rect"},
    {Element::Kind::Param, ShaderType::Int, "HasClip"},
    {Element::Kind::Param, ShaderType::Float4, "LocalRect"},
    {Element::Kind::Param, ShaderType::Float4, "RadiiX"},
    {Element::Kind::Param, ShaderType::Float4, "RadiiY"},
    {Element::Kind::Param, ShaderType::Float, "AntiAlias"},
    {Element::Kind::Param, ShaderType::Mat3, "DeviceToLocal"},
};

inline constexpr Module HairlineLineGeometry = {
    "HairlineLineGeometry", nullptr, 0, HairlineLineGeometryElements, 14, true, true};

}  // namespace tgfx
