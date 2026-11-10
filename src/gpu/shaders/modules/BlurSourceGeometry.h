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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <iterator>
#include "gpu/shaders/Module.h"

#pragma once

#include <iterator>
#include "gpu/shaders/Module.h"

namespace tgfx {

inline constexpr Element BlurSourceGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "Matrix"},
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
    {Element::Kind::Varying, ShaderType::Float2, "Coord"},
};

inline constexpr Module BlurSourceGeometry = {"BlurSourceGeometry",
                                              nullptr,
                                              0,
                                              BlurSourceGeometryElements,
                                              std::size(BlurSourceGeometryElements),
                                              true,
                                              true};

}  // namespace tgfx
