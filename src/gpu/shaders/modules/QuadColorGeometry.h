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

/// QuadPerEdgeAAGeometryProcessor's rect geometry with the per-vertex color: the position is
/// already in device space, the coverage rides the per-vertex slot and the color is either the
/// broadcast vertex color or the exact Color uniform, selected by HasCommonColor (the former
/// FILL_COLOR_VARYING_OR_COMMON).
inline constexpr Element QuadColorGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "Coverage"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Varying, ShaderType::Float, "Coverage"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module QuadColorGeometry = {
    "QuadColorGeometry", nullptr, 0, QuadColorGeometryElements, 7, true, true};

}  // namespace tgfx
