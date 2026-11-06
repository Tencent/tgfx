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
#define TGFX_CHAIN_WORD_COUNT_float 1
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

// Packing writes one field per word, in declaration order: the same walk the generated GLSL
// readers perform, so the two sides cannot disagree about an offset. A mat3 occupies three words
// as its columns, which is the dense column order a std140 mat3 has.
#define TGFX_CHAIN_PACK_float(value, out) \
  do {                                    \
    (out)[0] = (value);                   \
    (out)[1] = 0.0f;                      \
    (out)[2] = 0.0f;                      \
    (out)[3] = 0.0f;                      \
  } while (0)
#define TGFX_CHAIN_PACK_Float2(value, out)  \
  do {                                  \
    (out)[0] = (value).x;                     \
    (out)[1] = (value).y;                     \
    (out)[2] = 0.0f;                      \
    (out)[3] = 0.0f;                      \
  } while (0)
#define TGFX_CHAIN_PACK_Float3(value, out)  \
  do {                                  \
    (out)[0] = (value).x;                     \
    (out)[1] = (value).y;                     \
    (out)[2] = (value).z;                     \
    (out)[3] = 0.0f;                      \
  } while (0)
#define TGFX_CHAIN_PACK_Float4(value, out)  \
  do {                                  \
    (out)[0] = (value).x;                     \
    (out)[1] = (value).y;                     \
    (out)[2] = (value).z;                     \
    (out)[3] = (value).w;                     \
  } while (0)
#define TGFX_CHAIN_PACK_Matrix3(value, out)     \
  do {                                    \
    (out)[0] = (value).m[0];                    \
    (out)[1] = (value).m[1];                    \
    (out)[2] = (value).m[2];                    \
    (out)[3] = 0.0f;                        \
    (out)[4] = (value).m[3];                    \
    (out)[5] = (value).m[4];                    \
    (out)[6] = (value).m[5];                    \
    (out)[7] = 0.0f;                        \
    (out)[8] = (value).m[6];                    \
    (out)[9] = (value).m[7];                    \
    (out)[10] = (value).m[8];                   \
    (out)[11] = 0.0f;                       \
  } while (0)
#define TGFX_CHAIN_PACK_Matrix4(value, out)     \
  do {                                    \
    for (int column = 0; column < 4; ++column) { \
      for (int row = 0; row < 4; ++row) {        \
        (out)[column * 4 + row] = (value).m[column * 4 + row]; \
      }                                            \
    }                                              \
  } while (0)
#define TGFX_CHAIN_PACK_Float4Array8(value, out)             \
  do {                                                 \
    for (int index = 0; index < 8; ++index) {          \
      (out)[index * 4 + 0] = (value).v[index].x;             \
      (out)[index * 4 + 1] = (value).v[index].y;             \
      (out)[index * 4 + 2] = (value).v[index].z;             \
      (out)[index * 4 + 3] = (value).v[index].w;             \
    }                                                  \
  } while (0)
#define TGFX_CHAIN_PACK_Int32(value, out) \
  do {                              \
    (out)[0] = static_cast<float>(v); \
    (out)[1] = 0.0f;                  \
    (out)[2] = 0.0f;                  \
    (out)[3] = 0.0f;                  \
  } while (0)

#define TGFX_CHAIN_PACK_FIELD(type, name)                     \
  {                                                           \
    TGFX_CHAIN_PACK_##type(data.name, words + at * 4);        \
    at += TGFX_CHAIN_WORD_COUNT_##type;                       \
  }

/// The data-pool stride of one slot: the widest operator a single slot can carry.
inline constexpr int PoolSlotWords = 7;


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
                #name " declares more structural fields than a slot has columns");          \
  inline void Pack##name(const name##Data& data, float* words) {                             \
    (void)data;                                                                              \
    (void)words;                                                                             \
    int at = 0;                                                                              \
    (void)at;                                                                                \
    dataFields(TGFX_CHAIN_PACK_FIELD)                                                        \
  }

#define TGFX_CHAIN_EMPTY_FIELDS(T)

// ---- the operators, in the same order as TGFX_CHAIN_OPS -------------------------------------

#define TGFX_COLOR_MATRIX_DATA(T) \
  T(Matrix4, linear)              \
  T(Float4, translate)
TGFX_CHAIN_DECLARE_OP(ColorMatrix, TGFX_COLOR_MATRIX_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_LUMA_DATA(T) \
  T(float, kr)           \
  T(float, kg)           \
  T(float, kb)
TGFX_CHAIN_DECLARE_OP(Luma, TGFX_LUMA_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_ALPHA_THRESHOLD_DATA(T) T(float, threshold)
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
  T(float, threshold)         \
  T(float, bias)              \
  T(float, scale)             \
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
  T(float, antiAlias)
TGFX_CHAIN_DECLARE_OP(RRectCoverage, TGFX_RRECT_COVERAGE_DATA, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_TEX_MODULATE_STRUCTURE(T) \
  T(Int32, alphaOnly)                  \
  T(Int32, sourceLeaf)
TGFX_CHAIN_DECLARE_OP(TexModulate, TGFX_CHAIN_EMPTY_FIELDS, TGFX_TEX_MODULATE_STRUCTURE)

TGFX_CHAIN_DECLARE_OP(InputOpaque, TGFX_CHAIN_EMPTY_FIELDS, TGFX_CHAIN_EMPTY_FIELDS)

TGFX_CHAIN_DECLARE_OP(MulAlpha, TGFX_CHAIN_EMPTY_FIELDS, TGFX_CHAIN_EMPTY_FIELDS)

#define TGFX_PERLIN_NOISE_DATA(T) T(Float4, frequencyStitch)
#define TGFX_PERLIN_NOISE_STRUCTURE(T) \
  T(Int32, noiseType)                  \
  T(Int32, numOctaves)                 \
  T(Int32, stitchTiles)                \
  T(Int32, lutLeaf)
TGFX_CHAIN_DECLARE_OP(PerlinNoise, TGFX_PERLIN_NOISE_DATA, TGFX_PERLIN_NOISE_STRUCTURE)

/// The chain-wide block region: operators whose data exceeds the slot stride get a fixed offset
/// here instead of a slot index. The region starts right after the slot region, so a reader takes
/// `CHAIN_SLOT_CAPACITY * PoolSlotWords + <offset>` as its pool offset.
inline constexpr int BlockGradientOffset = 0;
inline constexpr int BlockWords = BlockGradientOffset + GradientDataWords;

// The slot stride must hold every operator a slot can carry inside the pool region. The gradient
// and the color-space transform are the two registered exceptions: they use chain-wide regions
// instead of a per-slot block.
static_assert(AARectCoverageDataWords <= PoolSlotWords, "coverage exceeds the slot stride");
static_assert(LocalRectCoverageDataWords <= PoolSlotWords, "coverage exceeds the slot stride");
static_assert(RRectCoverageDataWords <= PoolSlotWords, "coverage exceeds the slot stride");
static_assert(ColorMatrixDataWords <= PoolSlotWords, "a pointwise operator exceeds the slot stride");
static_assert(GradientDataWords > PoolSlotWords, "the gradient must own a chain-wide region");
static_assert(ColorSpaceXformDataWords > PoolSlotWords,
              "the color-space transform must own a chain-wide region");

// The generator's walk: every operator with the data field list that declares it. The row set is
// the same as TGFX_CHAIN_OPS; the count assert below keeps the two aligned, and a row whose
// declaration is missing fails at the name lookup.
#define TGFX_CHAIN_OP_FIELD_LISTS(X)                    \
  X(ColorMatrix, TGFX_COLOR_MATRIX_DATA)                \
  X(Luma, TGFX_LUMA_DATA)                               \
  X(AlphaThreshold, TGFX_ALPHA_THRESHOLD_DATA)          \
  X(ColorSpaceXform, TGFX_COLOR_SPACE_XFORM_DATA)       \
  X(None, TGFX_CHAIN_EMPTY_FIELDS)                      \
  X(Texture, TGFX_TEXTURE_DATA)                         \
  X(ConstColor, TGFX_CONST_COLOR_DATA)                  \
  X(Blend, TGFX_BLEND_DATA)                             \
  X(AARectCoverage, TGFX_AARECT_COVERAGE_DATA)          \
  X(Gradient, TGFX_GRADIENT_DATA)                       \
  X(LocalRectCoverage, TGFX_LOCAL_RECT_COVERAGE_DATA)   \
  X(RRectCoverage, TGFX_RRECT_COVERAGE_DATA)            \
  X(TexModulate, TGFX_CHAIN_EMPTY_FIELDS)               \
  X(InputOpaque, TGFX_CHAIN_EMPTY_FIELDS)               \
  X(MulAlpha, TGFX_CHAIN_EMPTY_FIELDS)                  \
  X(PerlinNoise, TGFX_PERLIN_NOISE_DATA)

inline constexpr int FieldListCount = 0
#define TGFX_CHAIN_FIELD_LIST_COUNT(name, data) +1
    TGFX_CHAIN_OP_FIELD_LISTS(TGFX_CHAIN_FIELD_LIST_COUNT)
#undef TGFX_CHAIN_FIELD_LIST_COUNT
    ;
static_assert(FieldListCount == OpCount, "the field-list table and the op table disagree");

// Every row of the op table must have its parameter declaration above: a row without one fails
// here instead of surfacing as a missing operator in the encoder.
#define TGFX_CHAIN_OP_DECLARED(name, id, inputs, sensitive, defineName) \
  static_assert(name##DataWords >= 0, "the op table has a row with no parameter declaration");
TGFX_CHAIN_OPS(TGFX_CHAIN_OP_DECLARED)
#undef TGFX_CHAIN_OP_DECLARED

}  // namespace ChainISA
}  // namespace tgfx
