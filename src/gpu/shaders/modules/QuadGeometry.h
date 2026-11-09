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

/// QuadPerEdgeAAGeometryProcessor's rect geometry: the position arrives pre-transformed to device
/// space (so no view Matrix), the coverage rides the per-vertex slot and the paint color comes
/// from the Color uniform. The accumulated order matches the DefaultGeometryProcessor path, so a
/// family that lists both geometries serves the two providers with one body.
inline constexpr Element QuadGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "Coverage"},
    {Element::Kind::Varying, ShaderType::Float, "Coverage"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
};

inline constexpr Module QuadGeometry = {"QuadGeometry", nullptr, 0, QuadGeometryElements, 4,
                                        true,           true};

}  // namespace tgfx
