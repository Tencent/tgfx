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

/// QuadPerEdgeAAGeometryProcessor's textured quad: the position is already in device space, the
/// coverage rides the per-vertex slot, and the optional per-vertex uv coordinate replaces the
/// position as the modules' source coordinate (HAS_UV_COORD = 1 exactly when the geometry carries
/// no uv matrix). HAS_SUBSET adds the per-vertex subset bounds attribute; the body modules own
/// every transform and sampler.
inline constexpr ModuleDim QuadTextureGeometryDims[] = {{"HAS_UV_COORD", 2}, {"HAS_SUBSET", 2}};

inline constexpr Element QuadTextureGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float, "InCoverage"},
    {Element::Kind::Attribute, ShaderType::Float2, "UVCoord", 1, 0, 0x02},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float4, "InTexSubset", 1, 1, 0x02},
    {Element::Kind::Varying, ShaderType::Float, "Coverage"},
    {Element::Kind::Varying, ShaderType::Float4, "Color"},
    {Element::Kind::Varying, ShaderType::Float4, "TexSubset", 1, 1, 0x02},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
};

inline constexpr Module QuadTextureGeometry = {"QuadTextureGeometry",
                                               QuadTextureGeometryDims,
                                               2,
                                               QuadTextureGeometryElements,
                                               std::size(QuadTextureGeometryElements),
                                               true,
                                               true};

}  // namespace tgfx
