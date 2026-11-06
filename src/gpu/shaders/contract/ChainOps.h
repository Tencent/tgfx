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

#include <cstdint>
#include "gpu/shaders/contract/ChainISA.h"

namespace tgfx {
namespace ChainISA {

// The parameters of every chain operator, split the way the runtime carries them:
//
//   Data       per-instance values, written into the chain data pool. Every field owns whole vec4
//              words, so a bare float costs one word; that is the padding rule the generator and
//              the packer share, and it needs no per-type layout table.
//   Structure  ints that change the code path. Each becomes one specialization constant per slot,
//              and the same values ride the fallback form's uniform mirror.
//
// A new operator declares its two field lists and one row in TGFX_CHAIN_OPS; the structs, the
// word counts and the capacity checks below all derive from those. Nothing here is consumed yet:
// the encoder and the GLSL generator are the next steps.

struct Float1 {
  float x = 0.0f;
};

struct Float2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct Float3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct Float4 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;
};

struct Matrix3 {
  float m[9] = {};
};

struct Matrix4 {
  float m[16] = {};
};

struct Float4Array8 {
  Float4 v[8] = {};
};

/// The structural int type: one of these becomes one specialization constant per slot.
using Int32 = int32_t;

// The word cost of one field type, in the pool's vec4 units.
#define TGFX_CHAIN_WORD_COUNT_Float1 1
#define TGFX_CHAIN_WORD_COUNT_Float2 1
#define TGFX_CHAIN_WORD_COUNT_Float3 1
#define TGFX_CHAIN_WORD_COUNT_Float4 1
#define TGFX_CHAIN_WORD_COUNT_Matrix3 3
#define TGFX_CHAIN_WORD_COUNT_Matrix4 4
#define TGFX_CHAIN_WORD_COUNT_Float4Array8 8
#define TGFX_CHAIN_WORD_COUNT_Int32 1

// The field-list vocabulary: a list is `#define <OP>_DATA(T) T(Float4, rect) ...`, so the same
// list produces the struct member, the word count and, later, the packer and the GLSL reader.
#define TGFX_CHAIN_DATA_MEMBER(type, name) type name = {};
#define TGFX_CHAIN_STRUCT_MEMBER(type, name) type name = 0;
#define TGFX_CHAIN_DATA_WORDS(type, name) +TGFX_CHAIN_WORD_COUNT_##type
#define TGFX_CHAIN_STRUCT_FIELD(type, name) +1

#define TGFX_CHAIN_DECLARE_OP(name, dataFields, structureFields)                            \
  struct name##Data {                                                                       \
    dataFields(TGFX_CHAIN_DATA_MEMBER)                                                      \
  };                                                                                        \
  struct name##Structure {                                                                  \
    structureFields(TGFX_CHAIN_STRUCT_MEMBER)                                               \
  };                                                                                        \
  inline constexpr int name##DataWords = 0 dataFields(TGFX_CHAIN_DATA_WORDS);                \
  inline constexpr int name##StructureFields = 0 structureFields(TGFX_CHAIN_STRUCT_FIELD);   \
  static_assert(name##DataWords <= DataCapacity, #name " data exceeds the chain data pool"); \
  static_assert(name##StructureFields <= StructureCapacity,                                  \
                #name " declares more structural fields than a slot has columns");

#define TGFX_CHAIN_EMPTY_FIELDS(T)

// ---- the operators, in the same order as TGFX_CHAIN_OPS -------------------------------------

#define TGFX_COLOR_MATRIX_DATA(T) \
  T(Matrix4, linear)              \
  T(Float4, translate)
TGFX_CHAIN_DECLARE_OP(ColorMatrix, TGFX_COLOR_MATRIX_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_LUMA_DATA(T) \
  T(Float1, kr)           \
  T(Float1, kg)           \
  T(Float1, kb)
TGFX_CHAIN_DECLARE_OP(Luma, TGFX_LUMA_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_ALPHA_THRESHOLD_DATA(T) T(Float1, threshold)
TGFX_CHAIN_DECLARE_OP(AlphaThreshold, TGFX_ALPHA_THRESHOLD_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_COLOR_SPACE_XFORM_DATA(T) \
  T(Float4, srcTF0)                    \
  T(Float4, srcTF1)                    \
  T(Float4, srcOOTF)                   \
  T(Matrix3, colorXform)               \
  T(Float4, dstOOTF)                   \
  T(Float4, dstTF0)                    \
  T(Float4, dstTF1)
#define TGFX_COLOR_SPACE_XFORM_STRUCTURE(T) \
  T(Int32, flags)                           \
  T(Int32, srcTFType)                       \
  T(Int32, dstTFType)
TGFX_CHAIN_DECLARE_OP(ColorSpaceXform, TGFX_COLOR_SPACE_XFORM_DATA,
                      TGFX_COLOR_SPACE_XFORM_STRUCTURE)

TGFX_CHAIN_DECLARE_OP(None, TGFX_CHAIN_EMPTY_FIELDS, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_TEXTURE_DATA(T) \
  T(Float4, subset)          \
  T(Float2, alphaStart)      \
  T(Matrix3, deviceMatrix)
#define TGFX_TEXTURE_STRUCTURE(T) \
  T(Int32, readSwizzle)           \
  T(Int32, alphaOnly)             \
  T(Int32, clampOn)               \
  T(Int32, hasRgbaaa)             \
  T(Int32, deviceSpace)
TGFX_CHAIN_DECLARE_OP(Texture, TGFX_TEXTURE_DATA, TGFX_TEXTURE_STRUCTURE)

#define TGFX_CONST_COLOR_DATA(T) T(Float4, color)
#define TGFX_CONST_COLOR_STRUCTURE(T) T(Int32, inputMode)
TGFX_CHAIN_DECLARE_OP(ConstColor, TGFX_CONST_COLOR_DATA, TGFX_CONST_COLOR_STRUCTURE)

#define TGFX_BLEND_DATA(T) T(Float4, color)
#define TGFX_BLEND_STRUCTURE(T)  \
  T(Int32, blendMode)            \
  T(Int32, childType)            \
  T(Int32, multiplyInputAlpha)
TGFX_CHAIN_DECLARE_OP(Blend, TGFX_BLEND_DATA, TGFX_BLEND_STRUCTURE)

#define TGFX_AARECT_COVERAGE_DATA(T) T(Float4, rect)
TGFX_CHAIN_DECLARE_OP(AARectCoverage, TGFX_AARECT_COVERAGE_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_GRADIENT_DATA(T)  \
  T(Float4, leftBorder)        \
  T(Float4, rightBorder)       \
  T(Float4, start)             \
  T(Float4, end)               \
  T(Float4, scale01)           \
  T(Float4, bias01)            \
  T(Float4, scale23)           \
  T(Float4, bias23)            \
  T(Float1, threshold)         \
  T(Float1, bias)              \
  T(Float1, scale)             \
  T(Float4, thresholds1_7)     \
  T(Float4, thresholds9_13)    \
  T(Float4Array8, scales)      \
  T(Float4Array8, biases)      \
  T(Matrix3, coordMatrix)
#define TGFX_GRADIENT_STRUCTURE(T) \
  T(Int32, layoutType)             \
  T(Int32, colorizerKind)          \
  T(Int32, intervalCount)          \
  T(Int32, lutLeaf)
TGFX_CHAIN_DECLARE_OP(Gradient, TGFX_GRADIENT_DATA, TGFX_GRADIENT_STRUCTURE)

#define TGFX_LOCAL_RECT_COVERAGE_DATA(T) \
  T(Float4, rect)                        \
  T(Matrix3, deviceToLocal)
TGFX_CHAIN_DECLARE_OP(LocalRectCoverage, TGFX_LOCAL_RECT_COVERAGE_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_RRECT_COVERAGE_DATA(T) \
  T(Float4, rect)                   \
  T(Float4, radiiX)                 \
  T(Float4, radiiY)                 \
  T(Matrix3, deviceToLocal)         \
  T(Float1, antiAlias)
TGFX_CHAIN_DECLARE_OP(RRectCoverage, TGFX_RRECT_COVERAGE_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_TEX_MODULATE_STRUCTURE(T) \
  T(Int32, alphaOnly)                  \
  T(Int32, sourceLeaf)
TGFX_CHAIN_DECLARE_OP(TexModulate, TGFX_CHAIN_EMPTY_FIELDS, TGFX_TEX_MODULATE_STRUCTURE)

TGFX_CHAIN_DECLARE_OP(InputOpaque, TGFX_CHAIN_EMPTY_FIELDS, TGFX_CHAIN_EMPTY_FIELDS)

TGFX_CHAIN_DECLARE_OP(MulAlpha, TGFX_CHAIN_EMPTY_FIELDS, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_PERLIN_NOISE_DATA(T) \
  T(Float4, frequencyStitch)      \
  T(Matrix3, uvMatrix)
#define TGFX_PERLIN_NOISE_STRUCTURE(T) \
  T(Int32, noiseType)                  \
  T(Int32, numOctaves)                 \
  T(Int32, stitchTiles)                \
  T(Int32, lutLeaf)
TGFX_CHAIN_DECLARE_OP(PerlinNoise, TGFX_PERLIN_NOISE_DATA, TGFX_PERLIN_NOISE_STRUCTURE)

// Every row of the op table must have its parameter declaration above: a row without one fails
// here instead of surfacing as a missing operator in the encoder.
#define TGFX_CHAIN_OP_DECLARED(name, id, inputs, sensitive, defineName) \
  static_assert(name##DataWords >= 0, "the op table has a row with no parameter declaration");
TGFX_CHAIN_OPS(TGFX_CHAIN_OP_DECLARED)
#undef TGFX_CHAIN_OP_DECLARED

}  // namespace ChainISA
}  // namespace tgfx
