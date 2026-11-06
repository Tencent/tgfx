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

#include <algorithm>
#include <string>
#include <vector>

namespace tgfx {

// Single source of truth for the cross-language kernel contracts, consumed by the runtime
// (onSetData writers, the chain op enum) and by shader_build_tool (which validates the GLSL side
// against these constants at bundle build time). Renaming a field or an op code anywhere fails
// the bundle build instead of silently leaving contract fields unwritten or misinterpreting a
// chain slot.
// The YUV kernel's color-grade tail capacity: the size of the chain instruction arrays the tail
// reads (the same slot encoding the interpreter uses). The GLSL side instantiates the same
// capacity via CHAIN_SLOT_CAPACITY in yuv_texture_fill.frag, and shader_build_tool parses that
// define and fails the bundle build on drift, so this constant is the single source.
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

// The pointwise color operators of the fused kernels, one row per operator: the single place an
// operator's name, ABI value and facts are declared. Every mirror below is generated from this
// list — the Op constants and the OP_* defines here, the AOTEffectKind / AOTPointwiseOpType /
// AOTChainOp members and the parameter variant in the IR — so adding an operator is one row plus
// its own semantics (parameter struct, math, bind), and removing a row fails every consumer's
// compile instead of leaving a stale mirror behind.
// Columns: name, ABI value (Op::*, frozen bundle compatibility), the IR parameter struct, the
// per-slot parameter member name, and pass-boundary sensitivity — an operator that amplifies the
// 1/255 pass-boundary rounding into a visible flip (a step function) must share its pass with its
// input's producer (IsPassBoundarySensitive; see AOTEffectDecomposer's CanStartPassAt).
#define TGFX_POINTWISE_OPS(X)                                                 \
  X(ColorMatrix, 0, AOTColorMatrixParameters, colorMatrix, false)             \
  X(Luma, 1, AOTLumaParameters, luma, false)                                  \
  X(AlphaThreshold, 2, AOTAlphaThresholdParameters, alphaThreshold, true)     \
  X(ColorSpaceXform, 3, AOTColorSpaceXformParameters, colorSpaceXform, false) \
  X(ConstColor, 6, AOTConstColorParameters, constColor, false)                \
  X(Blend, 7, AOTBlendParameters, blend, false)

namespace Op {

// The one op table of the pointwise interpreters (chain and slot): the single place an op value
// is defined. shader_build_tool injects these values into every compiled variant as the OP_*
// defines (Op::Defines); op_codes.inc only marks the include point, and the AOTChainOp enum
// anchors to them by assignment. The single-slot interpreter dispatches on the AOTPointwiseOpType
// subset of these values, so it deliberately never emits the chain-only ones (Texture, the
// coverage ops, and the modulators). The pointwise color operators come from the list above; the
// rest is the chain interpreter's instruction vocabulary (pass-through marker, texture, the
// coverage ops, the modulators, the noise source), which has no IR mirror to keep in sync.
#define TGFX_OP_CONSTANT(name, id, params, member, passBoundarySensitive) constexpr int name = id;
TGFX_POINTWISE_OPS(TGFX_OP_CONSTANT)
#undef TGFX_OP_CONSTANT
constexpr int None = 4;
constexpr int Texture = 5;
constexpr int AARectCoverage = 8;
constexpr int Gradient = 9;
constexpr int LocalRectCoverage = 10;
constexpr int RRectCoverage = 11;
constexpr int TexModulate = 12;
constexpr int InputOpaque = 13;
constexpr int MulAlpha = 14;
constexpr int PerlinNoise = 15;

namespace detail {

// The ABI value set, split the way the declarations are: the list's pointwise operators and the
// chain vocabulary. The asserts below pin uniqueness and the historical values — the OP_* values
// are part of every shipped variant's encoding (slot words, uniform OpType), so renumbering
// invalidates every bundle rather than failing loudly.
constexpr int PointwiseAbiValues[] = {
#define TGFX_OP_ABI_VALUE(name, id, params, member, passBoundarySensitive) id,
    TGFX_POINTWISE_OPS(TGFX_OP_ABI_VALUE)
#undef TGFX_OP_ABI_VALUE
};
constexpr int VocabularyAbiValues[] = {
    None,          Texture,     AARectCoverage, Gradient, LocalRectCoverage,
    RRectCoverage, TexModulate, InputOpaque,    MulAlpha, PerlinNoise};

constexpr bool AbiValuesUnique() {
  for (size_t i = 0; i < sizeof(PointwiseAbiValues) / sizeof(int); ++i) {
    for (size_t j = i + 1; j < sizeof(PointwiseAbiValues) / sizeof(int); ++j) {
      if (PointwiseAbiValues[i] == PointwiseAbiValues[j]) {
        return false;
      }
    }
    for (size_t j = 0; j < sizeof(VocabularyAbiValues) / sizeof(int); ++j) {
      if (PointwiseAbiValues[i] == VocabularyAbiValues[j]) {
        return false;
      }
    }
  }
  for (size_t i = 0; i < sizeof(VocabularyAbiValues) / sizeof(int); ++i) {
    for (size_t j = i + 1; j < sizeof(VocabularyAbiValues) / sizeof(int); ++j) {
      if (VocabularyAbiValues[i] == VocabularyAbiValues[j]) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace detail

static_assert(detail::AbiValuesUnique(), "two ops share one ABI value");
static_assert(None == 4 && Texture == 5 && PerlinNoise == 15,
              "the op ABI values are frozen bundle compatibility");

/// The OP_* macro name of a pointwise operator: CamelCase to SCREAMING_SNAKE (ColorSpaceXform to
/// COLOR_SPACE_XFORM). The chain vocabulary's names carry consecutive capitals (AARectCoverage)
/// and are spelled out explicitly below instead.
inline std::string PointwiseDefineName(const char* name) {
  std::string result;
  for (size_t i = 0; name[i] != '\0'; ++i) {
    const char c = name[i];
    if (c >= 'A' && c <= 'Z' && i > 0) {
      result.push_back('_');
    }
    result.push_back(static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
  }
  return result;
}

/// The OP_* macros every compiled variant receives. The table above is their only definition, so a
/// shader source cannot carry a second copy that drifts; shader_build_tool prepends this list to
/// every stage's defines and covers it with the manifest's source digest. The output is sorted by
/// ABI value, so it is byte-identical regardless of how the two tables above are ordered.
inline std::vector<std::string> Defines() {
  struct Entry {
    int value;
    const char* name;
    const char* define;
  };
  std::vector<Entry> entries = {
#define TGFX_OP_DEFINE(name, id, params, member, passBoundarySensitive) {id, #name, ""},
      TGFX_POINTWISE_OPS(TGFX_OP_DEFINE)
#undef TGFX_OP_DEFINE
          {None, "None", "NONE"},
      {Texture, "Texture", "TEXTURE"},
      {AARectCoverage, "AARectCoverage", "AARECT_COVERAGE"},
      {Gradient, "Gradient", "GRADIENT"},
      {LocalRectCoverage, "LocalRectCoverage", "LOCAL_RECT_COVERAGE"},
      {RRectCoverage, "RRectCoverage", "RRECT_COVERAGE"},
      {TexModulate, "TexModulate", "TEX_MODULATE"},
      {InputOpaque, "InputOpaque", "INPUT_OPAQUE"},
      {MulAlpha, "MulAlpha", "MUL_ALPHA"},
      {PerlinNoise, "PerlinNoise", "PERLIN_NOISE"},
  };
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry& a, const Entry& b) { return a.value < b.value; });
  std::vector<std::string> defines;
  defines.reserve(entries.size());
  for (const auto& entry : entries) {
    const std::string define =
        entry.define[0] != '\0' ? std::string(entry.define) : PointwiseDefineName(entry.name);
    defines.push_back("OP_" + define + "=" + std::to_string(entry.value));
  }
  return defines;
}

}  // namespace Op

namespace SamplerContract {

// The prefix of every ordinary texture sampler: TextureSampler_<N>, with N dense from zero in
// declaration order. shader_build_tool checks the reflected samplers against this pattern.
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
