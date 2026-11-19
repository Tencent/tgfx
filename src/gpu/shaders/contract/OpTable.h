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
//  either express or implied. See the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <cstdint>
#include "gpu/shaders/contract/ChainBindings.h"
#include "gpu/shaders/contract/ChainISA.h"

namespace tgfx {
namespace OpTable {

/**
 * The ABI type of one operator parameter, as the compiled module declares it. The runtime
 * emitter and the build tool's signature reflection both speak this vocabulary.
 */
enum class Param : uint8_t {
  Float,
  Int,
  Vec2,
  Vec3,
  Vec4,
  Mat3,
  Mat4,
  IVec4,
  Vec4Array8,
  SampledImage,
};

/** Where one call argument comes from — the recipe, spelled as data. */
enum class Source : uint8_t {
  /// The plan call's argument at index a.
  CallArg,
  /// The plan's named block member selected by the ChainBindingSource in a.
  Member,
  /// The member in a, or the integer zero when the plan does not declare it.
  MemberOrZero,
  /// The member in a, element b (the per-leaf records).
  MemberIndexed,
  /// The member in a, array element b.
  ArrayElement,
  /// A layout varying: a is 0 for SourceCoords, 1 for the per-quad subset.
  Varying,
  /// The integer literal a.
  Constant,
  /// The fragment coordinate.
  FragCoord,
  /// A value the row's special recipe builds (only leaf fetch and porter-duff use it).
  Special,
};

struct Arg {
  Param type = Param::Float;
  Source source = Source::CallArg;
  int a = 0;
  int b = 0;

  constexpr Arg() = default;
  constexpr Arg(Param argType, Source argSource) : type(argType), source(argSource) {
  }
  constexpr Arg(Param argType, Source argSource, int argA)
      : type(argType), source(argSource), a(argA) {
  }
  constexpr Arg(Param argType, Source argSource, ChainBindingSource member)
      : type(argType), source(argSource), a(static_cast<int>(member)) {
  }
  constexpr Arg(Param argType, Source argSource, ChainBindingSource member, int argB)
      : type(argType), source(argSource), a(static_cast<int>(member)), b(argB) {
  }
};

/**
 * One operator's complete machine-readable facts: the module id the bundle stores it under, the
 * export name the linker binds, the wrapper defines and includes that produce the module, the
 * parameter list (types and argument sources), and whether the operator is part of the linkable
 * slice. The runtime emitter derives its signatures, import set and gate from this table; the
 * build tool derives the module build and then reflects the compiled signature back against it,
 * so a drift between the GLSL source and this table fails the build instead of the link.
 */
struct Row {
  int id = 0;
  const char* exportName = nullptr;
  /// The wrapper prelude (defines and declarations) the module compiles with.
  const char* prelude = nullptr;
  /// The include files the module compiles from.
  const char* const* includes = nullptr;
  size_t includeCount = 0;
  /// Whether the wrapper declares the four leaf samplers at the layout's leaf bindings.
  bool leafSamplers = false;
  /// Whether the operator is part of the linkable slice.
  bool admitted = false;
  /// "" for data-driven rows; "leafFetch" / "porterDuff" for the two recipes that need code.
  const char* special = nullptr;
  const Arg* args = nullptr;
  size_t argCount = 0;
};

// The two operators that are not chain opcodes: the body imports them on demand.
inline constexpr int LeafFetchId = 0x3F;
inline constexpr int PorterDuffId = 0x3E;

// ---- ConstColor ---------------------------------------------------------------------------
inline constexpr Arg kConstColorArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Vec4, Source::CallArg, 1},
    {Param::Int, Source::CallArg, 2},
};
inline constexpr const char* kConstColorIncludes[] = {"ops/ConstColor.glsl"};

// ---- ColorMatrix --------------------------------------------------------------------------
inline constexpr Arg kColorMatrixArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Mat4, Source::CallArg, 1},
    {Param::Vec4, Source::CallArg, 2},
};
inline constexpr const char* kColorMatrixIncludes[] = {"ops/ColorMatrix.glsl"};

// ---- Luma ---------------------------------------------------------------------------------
inline constexpr Arg kLumaArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Vec3, Source::CallArg, 1},
};
inline constexpr const char* kLumaIncludes[] = {"ops/Luma.glsl"};

// ---- AlphaThreshold ------------------------------------------------------------------------
inline constexpr Arg kAlphaThresholdArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Float, Source::CallArg, 1},
};
inline constexpr const char* kAlphaThresholdIncludes[] = {"ops/AlphaThreshold.glsl"};

// ---- Blend ---------------------------------------------------------------------------------
inline constexpr Arg kBlendArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Vec4, Source::CallArg, 1},
    {Param::Int, Source::CallArg, 2},
};
inline constexpr const char* kBlendIncludes[] = {"xp/xp_blend_colors.inc"};

// ---- Texture -------------------------------------------------------------------------------
inline constexpr Arg kTextureArgs[] = {
    {Param::Vec4, Source::CallArg, 0},  // the prefetched leaf (a LeafTex register)
    {Param::Vec4, Source::CallArg, 1},
    {Param::Vec4, Source::CallArg, 2},
    {Param::Int, Source::CallArg, 3},
};
inline constexpr const char* kTextureIncludes[] = {"source/texture_sampling.inc",
                                                   "ops/Texture.glsl"};

// ---- TexModulate ---------------------------------------------------------------------------
inline constexpr Arg kTexModulateArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Vec4, Source::CallArg, 1},
    {Param::Int, Source::CallArg, 2},
};
inline constexpr const char* kTexModulateIncludes[] = {"source/texture_sampling.inc",
                                                       "ops/TexModulate.glsl"};

// ---- AARectCoverage ------------------------------------------------------------------------
inline constexpr Arg kAARectCoverageArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Vec4, Source::CallArg, 1},
    {Param::Vec2, Source::CallArg, 2},
};
inline constexpr const char* kAARectCoverageIncludes[] = {"ops/AARectCoverage.glsl"};

// ---- LocalRectCoverage ---------------------------------------------------------------------
inline constexpr Arg kLocalRectCoverageArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Vec4, Source::CallArg, 1},
    {Param::Mat3, Source::CallArg, 2},
    {Param::Vec2, Source::CallArg, 3},
};
inline constexpr const char* kLocalRectCoverageIncludes[] = {"coverage/analytic_clip_math.inc",
                                                             "ops/LocalRectCoverage.glsl"};

// ---- RRectCoverage -------------------------------------------------------------------------
inline constexpr Arg kRRectCoverageArgs[] = {
    {Param::Vec4, Source::CallArg, 0},  {Param::Vec4, Source::CallArg, 1},
    {Param::Vec4, Source::CallArg, 2},  {Param::Vec4, Source::CallArg, 3},
    {Param::Float, Source::CallArg, 4}, {Param::Mat3, Source::CallArg, 5},
    {Param::Vec2, Source::CallArg, 6},
};
inline constexpr const char* kRRectCoverageIncludes[] = {"coverage/analytic_clip_math.inc",
                                                         "ops/RRectCoverage.glsl"};

// ---- Gradient ------------------------------------------------------------------------------
inline constexpr Arg kGradientArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Vec3, Source::Varying, 0},
    {Param::Int, Source::Member, ChainBindingSource::GradientLayoutType},
    {Param::Int, Source::Member, ChainBindingSource::GradientColorizerKind},
    {Param::Int, Source::MemberOrZero, ChainBindingSource::GradientLutLeaf},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientLeftBorder},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientRightBorder},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientStart},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientEnd},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientScale01},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientBias01},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientScale23},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientBias23},
    {Param::Float, Source::Member, ChainBindingSource::GradientThreshold},
    {Param::Float, Source::Member, ChainBindingSource::GradientBias},
    {Param::Float, Source::Member, ChainBindingSource::GradientScale},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientThresholds1_7},
    {Param::Vec4, Source::Member, ChainBindingSource::GradientThresholds9_13},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 0},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 1},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 2},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 3},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 4},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 5},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 6},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientScales, 7},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 0},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 1},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 2},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 3},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 4},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 5},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 6},
    {Param::Vec4, Source::ArrayElement, ChainBindingSource::GradientBiases, 7},
};
inline constexpr const char* kGradientIncludes[] = {"pipeline/chain/pointwise_gradient.inc"};

// ---- PerlinNoise ---------------------------------------------------------------------------
inline constexpr Arg kPerlinNoiseArgs[] = {
    {Param::Vec3, Source::Varying, 0},
    {Param::Vec4, Source::Member, ChainBindingSource::NoiseFrequencyStitch},
    {Param::IVec4, Source::Member, ChainBindingSource::NoiseModeOctaves},
};
inline constexpr const char* kPerlinNoiseIncludes[] = {"pipeline/chain/pointwise_noise.inc"};

// ---- ColorSpaceXform -----------------------------------------------------------------------
inline constexpr Arg kColorSpaceXformArgs[] = {
    {Param::Vec4, Source::CallArg, 0},
    {Param::Int, Source::Member, ChainBindingSource::ChainFlags},
    {Param::Vec4, Source::Member, ChainBindingSource::ChainSrcTF0},
    {Param::Vec4, Source::Member, ChainBindingSource::ChainSrcTF1},
    {Param::Int, Source::Member, ChainBindingSource::ChainSrcTFType},
    {Param::Vec4, Source::Member, ChainBindingSource::ChainSrcOOTF},
    {Param::Mat3, Source::Member, ChainBindingSource::ChainColorXform},
    {Param::Vec4, Source::Member, ChainBindingSource::ChainDstOOTF},
    {Param::Vec4, Source::Member, ChainBindingSource::ChainDstTF0},
    {Param::Vec4, Source::Member, ChainBindingSource::ChainDstTF1},
    {Param::Int, Source::Member, ChainBindingSource::ChainDstTFType},
};
inline constexpr const char* kColorSpaceXformIncludes[] = {"ops/ColorSpaceXform.glsl"};

// ---- LeafFetch (pseudo operator; parameters built by the leaf recipe) ----------------------
inline constexpr Arg kLeafFetchArgs[] = {
    {Param::Vec3, Source::Special}, {Param::Vec4, Source::Special}, {Param::Vec2, Source::Special},
    {Param::Int, Source::Special},  {Param::Int, Source::Special},  {Param::Int, Source::Special},
    {Param::Int, Source::Special},  {Param::Mat3, Source::Special}, {Param::Int, Source::Special},
    {Param::Vec2, Source::Special}, {Param::Int, Source::Special},  {Param::Vec4, Source::Special},
};
inline constexpr const char* kLeafFetchIncludes[] = {"source/texture_sampling.inc",
                                                     "entry/ChainLeafFetch.glsl"};

// ---- PorterDuff (pseudo operator; parameters built by the xfer tail) -----------------------
inline constexpr Arg kPorterDuffArgs[] = {
    {Param::Vec4, Source::Special},         {Param::Vec4, Source::Special},
    {Param::Vec2, Source::Special},         {Param::Vec2, Source::Special},
    {Param::Vec2, Source::Special},         {Param::Int, Source::Special},
    {Param::SampledImage, Source::Special},
};
inline constexpr const char* kPorterDuffIncludes[] = {"xp/xp_porter_duff.inc"};

inline constexpr Row kRows[] = {
    {ChainISA::ConstColor, "pmApplyConstColor", "", kConstColorIncludes, 1, false, true, "",
     kConstColorArgs, 3},
    {ChainISA::ColorMatrix, "pmApplyColorMatrix", "", kColorMatrixIncludes, 1, false, true, "",
     kColorMatrixArgs, 3},
    {ChainISA::Luma, "pmApplyLuma", "", kLumaIncludes, 1, false, true, "", kLumaArgs, 2},
    {ChainISA::AlphaThreshold, "pmApplyAlphaThreshold", "", kAlphaThresholdIncludes, 1, false, true,
     "", kAlphaThresholdArgs, 2},
    {ChainISA::Blend, "xpBlendColors", "#define HAS_XP 1\n", kBlendIncludes, 1, false, true, "",
     kBlendArgs, 3},
    {ChainISA::Texture, "tgfxOpTexture", "", kTextureIncludes, 2, true, true, "", kTextureArgs, 4},
    {ChainISA::TexModulate, "tgfxOpTexModulate", "", kTexModulateIncludes, 2, true, true, "",
     kTexModulateArgs, 3},
    {ChainISA::AARectCoverage, "tgfxOpAARectCoverage", "", kAARectCoverageIncludes, 1, false, true,
     "", kAARectCoverageArgs, 3},
    {ChainISA::LocalRectCoverage, "tgfxOpLocalRectCoverage", "", kLocalRectCoverageIncludes, 2,
     false, true, "", kLocalRectCoverageArgs, 4},
    {ChainISA::RRectCoverage, "tgfxOpRRectCoverage", "", kRRectCoverageIncludes, 2, false, true, "",
     kRRectCoverageArgs, 7},
    {ChainISA::Gradient, "evalGradientSlot", "#define NTEX 4\n", kGradientIncludes, 1, true, true,
     "", kGradientArgs, 34},
    {ChainISA::PerlinNoise, "evalNoiseSlot", "", kPerlinNoiseIncludes, 1, true, true, "",
     kPerlinNoiseArgs, 3},
    {ChainISA::ColorSpaceXform, "chainApplyColorSpaceXform", "", kColorSpaceXformIncludes, 1, false,
     true, "", kColorSpaceXformArgs, 11},
    {LeafFetchId, "chainLeafFetch", "", kLeafFetchIncludes, 2, true, true, "leafFetch",
     kLeafFetchArgs, 12},
    {PorterDuffId, "applyPorterDuffXP",
     "#define HAS_XP 1\n#define TGFX_DECLARED_CHAIN 1\n"
     "layout(set = 2, binding = 0) uniform sampler2D dummyDstTex;\n",
     kPorterDuffIncludes, 1, false, true, "porterDuff", kPorterDuffArgs, 7},
};

inline constexpr const Row* Rows() {
  return kRows;
}

inline constexpr size_t RowCount() {
  return sizeof(kRows) / sizeof(kRows[0]);
}

inline const Row* FindRow(int id) {
  for (size_t index = 0; index < RowCount(); ++index) {
    if (kRows[index].id == id) {
      return &kRows[index];
    }
  }
  return nullptr;
}

}  // namespace OpTable
}  // namespace tgfx
