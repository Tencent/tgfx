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

/// DefaultGeometryProcessor's rect geometry: the position goes through the view Matrix, the
/// coverage rides the per-vertex slot (a constant 1.0 for the non-AA path triangles, so the
/// modulation is the identity there) and the paint color comes from the Color uniform.
inline constexpr Element RectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "Coverage"},
    // The per-vertex color the chain interpreter reads (its common-color select). The runtime
    // layout always provides the channel; the declared families that do not read it simply carry
    // an unused input, which costs nothing.
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::Varying, ShaderType::Float, "Coverage"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
};

inline constexpr Module RectGeometry = {"RectGeometry", nullptr, 0, RectGeometryElements, 7,
                                        true,           true};

}  // namespace tgfx
