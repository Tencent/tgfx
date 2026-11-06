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

// The chain interpreter's instruction set: one row per operator, the single place an operator's
// name, ABI value and operand count are declared. Every mirror is generated from this list — the
// Op::* constants, the OP_* defines shader_build_tool injects into every compiled variant, and
// the encoder's per-slot structural fields. The ABI values are frozen: they are part of the
// encoding of every shipped variant, so a renumbering invalidates every bundle instead of
// failing loudly. Keep the rows in ascending ABI order; Defines() relies on it and the asserts
// below check it.
//
// Columns: name, ABI value, operand count, pass-boundary sensitive, OP_* define spelling.
// An operator that amplifies the 1/255 pass-boundary rounding into a visible flip (a step
// function) must share its pass with its input's producer (see IsPassBoundarySensitive).
// The define spelling is explicit rather than derived from the name: the vocabulary names carry
// consecutive capitals (AARectCoverage), which a CamelCase-to-SCREAMING_SNAKE rule splits wrong.
#define TGFX_CHAIN_OPS(X)                                     \
  X(ColorMatrix, 0, 1, false, COLOR_MATRIX)                   \
  X(Luma, 1, 1, false, LUMA)                                  \
  X(AlphaThreshold, 2, 1, true, ALPHA_THRESHOLD)              \
  X(ColorSpaceXform, 3, 1, false, COLOR_SPACE_XFORM)          \
  X(None, 4, 0, false, NONE)                                  \
  X(Texture, 5, 0, false, TEXTURE)                            \
  X(ConstColor, 6, 1, false, CONST_COLOR)                     \
  X(Blend, 7, 2, false, BLEND)                                \
  X(AARectCoverage, 8, 1, false, AARECT_COVERAGE)             \
  X(Gradient, 9, 0, false, GRADIENT)                          \
  X(LocalRectCoverage, 10, 1, false, LOCAL_RECT_COVERAGE)     \
  X(RRectCoverage, 11, 1, false, RRECT_COVERAGE)              \
  X(TexModulate, 12, 1, false, TEX_MODULATE)                  \
  X(InputOpaque, 13, 1, false, INPUT_OPAQUE)                  \
  X(MulAlpha, 14, 2, false, MUL_ALPHA)                        \
  X(PerlinNoise, 15, 0, false, PERLIN_NOISE)

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
