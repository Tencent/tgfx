/////////////////////////////////////////////////////////////////////////////////////////////////
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
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gpu/shaders/Module.h"

namespace tgfx {

// The chain's non-AA rounded-rect layout (GP_LAYOUT 5): device-space vertices like the RRect
// family's, the local coord and radii the per-pixel SDF reads, and the half-stroke-width attribute.
// The attribute is unconditional (the fill buffers write zeros) and the StrokeEnabled uniform
// selects the coverage form at runtime, so one variant serves both forms. The attributes pair by
// ordinal with RRectsVertexProvider's non-AA buffer, so the vertex buffer the RRect family consumes
// serves this layout unchanged.
// The family's ordinals decode against one flat domain across the geometries, so the dim matches
// every other chain geometry and the allowed filter pins this geometry's unused value.
inline constexpr ModuleDim ChainRRectGeometryDims[] = {{"SUBSET", 2}};

inline constexpr Element ChainRRectGeometryElements[] = {
    {Element::Kind::Attribute, ShaderType::Float2, "Position"},
    {Element::Kind::Attribute, ShaderType::Float2, "LocalCoord"},
    {Element::Kind::Attribute, ShaderType::Float2, "Radii"},
    {Element::Kind::Attribute, ShaderType::Float4, "RectBounds"},
    {Element::Kind::Attribute, ShaderType::Float4, "InColor"},
    {Element::Kind::Attribute, ShaderType::Float2, "InStrokeWidth"},
    {Element::Kind::Varying, ShaderType::Float2, "vLocalCoord"},
    {Element::Kind::Varying, ShaderType::Float2, "vRadii"},
    {Element::Kind::Varying, ShaderType::Float4, "vRectBounds"},
    {Element::Kind::Varying, ShaderType::Float2, "vStrokeWidth"},
    {Element::Kind::Varying, ShaderType::Float4, "vColor"},
    {Element::Kind::Param, ShaderType::Float4, "Color"},
    {Element::Kind::Param, ShaderType::Int, "HasCommonColor"},
    {Element::Kind::Param, ShaderType::Int, "StrokeEnabled"},
};

inline constexpr Module ChainRRectGeometry = {
    "ChainRRectGeometry", ChainRRectGeometryDims, 1, ChainRRectGeometryElements, 14, true, true,
    /*bareNames=*/true};

}  // namespace tgfx
