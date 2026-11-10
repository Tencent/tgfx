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

#include <iterator>
#include "gpu/shaders/Module.h"

namespace tgfx {

/// The refraction body (the former kernels/glass_refraction.frag): displaces the background
/// sample along the glass shape's refracted direction and adds the edge-light response. DISPERSION
/// is the former DISPERSION_ON — a compile-time branch because the runtime emission compiles one
/// path or the other and never both; SHAPE is the former GEOMETRY_KIND, carried by the geometry
/// child. The lighting and edge-mask gates stay runtime uniforms (LightingOn, EdgeMaskOn), exactly
/// as the legacy kernel kept them: both only add to an already-sampled color, so the branch cannot
/// move a texture coordinate. The UDF kind always declares its two mask samplers (an edge-lit-off
/// draw binds the shared dummy texture and clears EdgeMaskOn), so the UDF-only fields and samplers
/// exist iff SHAPE == 2.
inline constexpr ModuleDim GlassRefractionDims[] = {{"DISPERSION", 2}, {"SHAPE", 3}};

inline constexpr Element GlassRefractionElements[] = {
    // The background-coordinate transform: the fragment processor's own coord transform, uploaded
    // by the standard helper (the geometry's uv matrix pre-concatenated for the quad form).
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
    // Affine by contract: the matcher rejects a projective background transform, so the varying
    // interpolates a plain vec2 — dividing an interpolated z here is NOT bit-identical.
    {Element::Kind::Varying, ShaderType::Float2, "Coord"},
    // The optics block (mirrors GlassOpticsP0..P3 of the runtime upload).
    {Element::Kind::Param, ShaderType::Float4, "OpticsP0"},
    {Element::Kind::Param, ShaderType::Float2, "OpticsP1"},
    {Element::Kind::Param, ShaderType::Float4, "OpticsP2"},
    {Element::Kind::Param, ShaderType::Float4, "OpticsP3"},
    // The shape block (mirrors GlassShapeP0/P1; the SDF kinds never read the UDF-only fields and
    // vice versa).
    {Element::Kind::Param, ShaderType::Float4, "ShapeP0"},
    {Element::Kind::Param, ShaderType::Float4, "ShapeP1"},
    {Element::Kind::Param, ShaderType::Float2, "LightDir"},
    {Element::Kind::Param, ShaderType::Int, "LightingOn"},
    // The UDF kind's own fields (the SDF kinds never read them).
    {Element::Kind::Param, ShaderType::Float4, "FineMaskUV", 1, 1, 0x04},
    {Element::Kind::Param, ShaderType::Float2, "EdgeSpan", 1, 1, 0x04},
    {Element::Kind::Param, ShaderType::Float4, "EdgeMaskUV", 1, 1, 0x04},
    {Element::Kind::Param, ShaderType::Int, "EdgeMaskOn", 1, 1, 0x04},
    // The background source; the UDF masks follow in the old kernel's binding order.
    {Element::Kind::Texture, ShaderType::Float4, "Sampler"},
    {Element::Kind::Texture, ShaderType::Float4, "MaskSampler", 1, 1, 0x04},
    {Element::Kind::Texture, ShaderType::Float4, "EdgeMaskSampler", 1, 1, 0x04},
};

inline constexpr Module GlassRefraction = {"GlassRefraction",
                                           GlassRefractionDims,
                                           2,
                                           GlassRefractionElements,
                                           std::size(GlassRefractionElements),
                                           true,
                                           true};

}  // namespace tgfx
