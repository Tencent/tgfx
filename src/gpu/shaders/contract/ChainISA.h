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

namespace tgfx {
namespace ChainISA {

/// The per-slot data pool in vec4 words. A field owns whole words, so a float costs one word.
inline constexpr int DataCapacity = 256;

// The chain interpreter's instruction set: one row per operator, the single place an operator's
// name, ABI value and operand count are declared. Every mirror is generated from this list — the
// Op::* constants and the OP_* defines shader_build_tool injects into every compiled variant. The
// ABI values are frozen: they are part of the
// encoding of every shipped variant, so a renumbering invalidates every bundle instead of
// failing loudly. Keep the rows in ascending ABI order; Defines() relies on it and the asserts
// below check it.
//
// Columns: name, ABI value, operand count, pass-boundary sensitive, OP_* define spelling.
// An operator that amplifies the 1/255 pass-boundary rounding into a visible flip (a step
// function) must share its pass with its input's producer (see IsPassBoundarySensitive).
// The define spelling is explicit rather than derived from the name: the vocabulary names carry
// consecutive capitals (AARectCoverage), which a CamelCase-to-SCREAMING_SNAKE rule splits wrong.
#define TGFX_CHAIN_OPS(X)                                           \
  X(ColorMatrix, 0, 1, false, COLOR_MATRIX)                         \
  X(Luma, 1, 1, false, LUMA)                                        \
  X(AlphaThreshold, 2, 1, true, ALPHA_THRESHOLD)                    \
  X(ColorSpaceXform, 3, 1, false, COLOR_SPACE_XFORM)                \
  X(None, 4, 0, false, NONE)                                        \
  X(Texture, 5, 0, false, TEXTURE)                                  \
  X(ConstColor, 6, 1, false, CONST_COLOR)                           \
  X(Blend, 7, 2, false, BLEND)                                      \
  X(AARectCoverage, 8, 1, false, AARECT_COVERAGE)                   \
  X(Gradient, 9, 0, false, GRADIENT)                                \
  X(LocalRectCoverage, 10, 1, false, LOCAL_RECT_COVERAGE)           \
  X(RRectCoverage, 11, 1, false, RRECT_COVERAGE)                    \
  X(TexModulate, 12, 1, false, TEX_MODULATE)                        \
  X(InputOpaque, 13, 1, false, INPUT_OPAQUE)                        \
  X(MulAlpha, 14, 2, false, MUL_ALPHA)                              \
  X(PerlinNoise, 15, 0, false, PERLIN_NOISE)                        \
  X(YUV, 16, 0, false, YUV)                                         \
  X(GaussianBlur1D, 17, 1, false, GAUSSIAN_BLUR_1D)                 \
  X(PremulAlpha, 18, 2, false, PREMUL_ALPHA)                        \
  X(YUVConvert, 19, 1, false, YUV_CONVERT)                          \
  X(LinearGradientT, 20, 1, false, LINEAR_GRADIENT_T)               \
  X(RadialGradientT, 21, 1, false, RADIAL_GRADIENT_T)               \
  X(DiamondGradientT, 22, 1, false, DIAMOND_GRADIENT_T)             \
  X(ConicGradientT, 23, 1, false, CONIC_GRADIENT_T)                 \
  X(SingleGradientColor, 24, 1, false, SINGLE_GRADIENT_COLOR)       \
  X(DualGradientColor, 25, 1, false, DUAL_GRADIENT_COLOR)           \
  X(GpuTextureRead, 26, 0, false, GPU_TEXTURE_READ)                 \
  X(EllipseCoverage, 27, 0, false, ELLIPSECOVERAGE)                 \
  X(ComplexEllipseCoverage, 28, 0, false, COMPLEXELLIPSECOVERAGE)   \
  X(NonAARRectCoverage, 29, 0, false, NONAARRECTCOVERAGE)           \
  X(ComplexRRectCoverage, 30, 0, false, COMPLEXRRECTCOVERAGE)       \
  X(RoundStrokeRectCoverage, 31, 0, false, ROUNDSTROKERECTCOVERAGE) \
  X(HairlineLineCoverage, 32, 0, false, HAIRLINELINECOVERAGE)       \
  X(HairlineQuadCoverage, 33, 0, false, HAIRLINEQUADCOVERAGE)       \
  X(OpaqueColor, 34, 0, false, OPAQUE_COLOR)                        \
  X(TextCoverageFromAlpha, 35, 0, false, TEXT_COVERAGE_FROM_ALPHA)  \
  X(TextColorUnpremul, 36, 0, false, TEXT_COLOR_UNPREMUL)           \
  X(DeviceCoord, 37, 0, false, DEVICE_COORD)                        \
  X(AlphaModulate, 38, 0, false, ALPHA_MODULATE)

#define TGFX_CHAIN_OP_CONSTANT(name, id, inputs, sensitive, defineName) \
  inline constexpr int name = id;
TGFX_CHAIN_OPS(TGFX_CHAIN_OP_CONSTANT)
#undef TGFX_CHAIN_OP_CONSTANT

/// The number of operators in the table; the OP_* define set and the dispatch must cover them all.
inline constexpr int OpCount = 0
#define TGFX_CHAIN_OP_COUNT(name, id, inputs, sensitive, defineName) +1
    TGFX_CHAIN_OPS(TGFX_CHAIN_OP_COUNT)
#undef TGFX_CHAIN_OP_COUNT
    ;

}  // namespace ChainISA
}  // namespace tgfx
