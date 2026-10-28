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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>

namespace tgfx {

// Single source of truth for the cross-language kernel contracts, consumed by the runtime
// (onSetData writers, the chain op enum) and by shader_build_tool (which validates the GLSL side
// against these constants at bundle build time). Renaming a field or an op code anywhere fails
// the bundle build instead of silently leaving contract fields unwritten or misinterpreting a
// chain slot.
// The plain pointwise slot capacity shared by the Perlin noise and YUV kernels: the size of the
// OpType/parameter arrays in their op tails. The GLSL side instantiates the same capacity via
// TGFX_SLOT_ARRAY_SUFFIX in perlin_noise.frag / yuv_texture_fill.frag, and shader_build_tool
// parses that suffix and fails the bundle build on drift, so this constant is the single source.
inline constexpr size_t MaxPointwiseSlots = 3;

namespace ClipContract {

// The runtime clip contract fields (rrect_clip_uniforms.inc / clip_coverage.inc on the GLSL
// side; the rect/rrect effect writers on the C++ side). Every shader whose fragment uniform
// block carries HasClip must declare all of them.
constexpr const char* Rect = "Rect";
constexpr const char* HasClip = "HasClip";
constexpr const char* LocalRect = "LocalRect";
constexpr const char* RadiiX = "RadiiX";
constexpr const char* RadiiY = "RadiiY";
constexpr const char* AntiAlias = "AntiAlias";
constexpr const char* DeviceToLocal = "DeviceToLocal";

}  // namespace ClipContract

namespace Op {

// The one op table of the pointwise interpreters (chain and slot): the single place an op value
// is defined. Mirrored by the OP_* defines in operator/op_codes.inc (validated by
// shader_build_tool, which also rejects any OP_* define elsewhere) and anchored to by the
// AOTChainOp enum. The single-slot interpreter dispatches on the AOTPointwiseOpType subset of
// these values, so it deliberately never emits the chain-only ones (Texture, the coverage ops,
// and the modulators).
constexpr int ColorMatrix = 0;
constexpr int Luma = 1;
constexpr int AlphaThreshold = 2;
constexpr int ColorSpaceXform = 3;
constexpr int None = 4;
constexpr int Texture = 5;
constexpr int ConstColor = 6;
constexpr int Blend = 7;
constexpr int AARectCoverage = 8;
constexpr int Gradient = 9;
constexpr int LocalRectCoverage = 10;
constexpr int RRectCoverage = 11;
constexpr int TexModulate = 12;
constexpr int InputOpaque = 13;
constexpr int MulAlpha = 14;
constexpr int PerlinNoise = 15;

}  // namespace Op

namespace SamplerContract {

// The prefix of every ordinary texture sampler: TextureSampler_<N>, with N dense from zero in
// declaration order. The runtime emits these names (ProgramBuilder) and shader_build_tool checks
// the reflected samplers against the same pattern.
constexpr const char* TextureSamplerPrefix = "TextureSampler_";

// Every semantic sampler name a precompiled kernel may declare. Ordinary texture bindings use the
// generic TextureSampler_<N> pattern (N dense from zero, checked by shader_build_tool); the names
// below carry dedicated meaning and appear verbatim in the kernels, the runtime emitters, and the
// onSetData writers. A new semantic sampler must be registered here or the bundle build fails —
// the point is that adding a binding interface is a deliberate contract change, not a typo.
constexpr const char* DstTexture = "DstTextureSampler";
constexpr const char* MaskTexture = "MaskTextureSampler";
constexpr const char* Mask = "MaskSampler";
constexpr const char* LocalMask = "LocalMaskSampler";
constexpr const char* EdgeMask = "EdgeMaskSampler";
constexpr const char* GradientTexture = "GradientTexture";

inline bool IsSemanticSamplerName(const std::string& name) {
  return name == DstTexture || name == MaskTexture || name == Mask || name == LocalMask ||
         name == EdgeMask || name == GradientTexture;
}

}  // namespace SamplerContract

}  // namespace tgfx
