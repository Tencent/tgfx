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

/// A three-plane YUV source (the simplified former YUVTextureFillShader): I420 declares the third
/// plane's sampler (FORMAT = 0) while NV12 reads the interleaved RG plane (FORMAT = 1). The color
/// is the YUV-to-RGB conversion of the sampled planes, optionally through the RGBAAA dual-plane
/// alpha; the bounded pointwise tail the old kernel embedded is stripped — color-graded YUV draws
/// flatten to RGBA first (see the plan's YUV entry).
inline constexpr ModuleDim YUVTextureDims[] = {{"FORMAT", 2}};

inline constexpr Element YUVTextureElements[] = {
    {Element::Kind::VertexParam, ShaderType::Mat3, "CoordMatrix"},
    {Element::Kind::Varying, ShaderType::Float3, "Coord"},
    {Element::Kind::Texture, ShaderType::Float4, "SamplerY"},
    {Element::Kind::Texture, ShaderType::Float4, "SamplerUV"},
    {Element::Kind::Texture, ShaderType::Float4, "SamplerV", 1, 0, 0x01},
    {Element::Kind::Param, ShaderType::Float4, "Subset"},
    {Element::Kind::Param, ShaderType::Mat3, "ColorConversion"},
    {Element::Kind::Param, ShaderType::Int, "LimitedRange"},
    {Element::Kind::Param, ShaderType::Float2, "AlphaStart"},
    {Element::Kind::Param, ShaderType::Int, "HasRgbaaa"},
};

inline constexpr Module YUVTexture = {
    "YUVTexture", YUVTextureDims, 1, YUVTextureElements, std::size(YUVTextureElements), true, true};

}  // namespace tgfx
