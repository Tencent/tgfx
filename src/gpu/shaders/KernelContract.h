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
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/ChainOps.h"

namespace tgfx {

// Single source of truth for the cross-language kernel contracts, consumed by the runtime
// (onSetData writers, the chain op enum) and by shader_build_tool (which validates the GLSL side
// against these constants at bundle build time). Renaming a field or an op code anywhere fails
// the bundle build instead of silently leaving contract fields unwritten or misinterpreting a
// chain slot.
// The YUV kernel's color-grade tail capacity: the size of the chain instruction arrays the tail
// reads (the same slot encoding the interpreter uses). The GLSL side aliases CHAIN_SLOT_CAPACITY
// from TGFX_TAIL_SLOT_CAPACITY (yuv_texture_fill.frag), which the build injects from
// AOTResourceProfile::SlotCapacityDefines() into every compiled stage, so this constant is the
// single source and the compiled arrays cannot drift from it.
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
#define TGFX_POINTWISE_OPS(X)                                                                     \
  X(ColorMatrix, ChainISA::ColorMatrix, AOTColorMatrixParameters, colorMatrix, false)             \
  X(Luma, ChainISA::Luma, ChainISA::LumaData, luma, false)                                        \
  X(AlphaThreshold, ChainISA::AlphaThreshold, ChainISA::AlphaThresholdData, alphaThreshold, true) \
  X(ColorSpaceXform, ChainISA::ColorSpaceXform, AOTColorSpaceXformParameters, colorSpaceXform,    \
    false)                                                                                        \
  X(ConstColor, ChainISA::ConstColor, AOTConstColorParameters, constColor, false)                 \
  X(Blend, ChainISA::Blend, AOTBlendParameters, blend, false)

namespace Op {

// The op values of the chain interpreter and the pointwise slot interpreters: every one derives
// from ChainISA's single list (contract/ChainISA.h), which is also the list Defines() turns into
// the OP_* defines every compiled variant receives, and the AOTChainOp enum anchors to these
// values by assignment. The single-slot interpreter
// dispatches on the AOTPointwiseOpType subset, so it deliberately never emits the chain-only
// operators (Texture, the coverage ops, the modulators). The pointwise color operators also have
// an IR mirror (the list above), which the asserts below keep aligned with the ABI values.
#define TGFX_OP_CONSTANT(name, id, inputs, sensitive, defineName) \
  constexpr int name = ChainISA::name;
TGFX_CHAIN_OPS(TGFX_OP_CONSTANT)
#undef TGFX_OP_CONSTANT

namespace detail {

// The ABI value set in table order. The asserts below pin uniqueness, ascending order and the
// historical values — the OP_* values are part of every shipped variant's encoding (slot words,
// uniform OpType), so renumbering invalidates every bundle rather than failing loudly.
constexpr int AbiValues[] = {
#define TGFX_OP_ABI_ROW(name, id, inputs, sensitive, defineName) id,
    TGFX_CHAIN_OPS(TGFX_OP_ABI_ROW)
#undef TGFX_OP_ABI_ROW
};
constexpr size_t AbiValueCount = sizeof(AbiValues) / sizeof(int);

constexpr int PointwiseAbiValues[] = {
#define TGFX_OP_ABI_VALUE(name, id, params, member, passBoundarySensitive) id,
    TGFX_POINTWISE_OPS(TGFX_OP_ABI_VALUE)
#undef TGFX_OP_ABI_VALUE
};

constexpr bool AbiValuesUnique() {
  for (size_t i = 0; i < AbiValueCount; ++i) {
    for (size_t j = i + 1; j < AbiValueCount; ++j) {
      if (AbiValues[i] == AbiValues[j]) {
        return false;
      }
    }
  }
  return true;
}

constexpr bool AbiValuesAscending() {
  for (size_t i = 1; i < AbiValueCount; ++i) {
    if (AbiValues[i] <= AbiValues[i - 1]) {
      return false;
    }
  }
  return true;
}

constexpr bool PointwiseValuesInAbiSet() {
  for (size_t i = 0; i < sizeof(PointwiseAbiValues) / sizeof(int); ++i) {
    bool found = false;
    for (size_t j = 0; j < AbiValueCount; ++j) {
      if (PointwiseAbiValues[i] == AbiValues[j]) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

}  // namespace detail

static_assert(detail::AbiValuesUnique(), "two ops share one ABI value");
static_assert(detail::AbiValuesAscending(), "the op table must stay in ascending ABI order");
static_assert(detail::PointwiseValuesInAbiSet(), "an IR operator is missing from ChainISA");
static_assert(static_cast<int>(detail::AbiValueCount) == ChainISA::OpCount, "op count mismatch");
static_assert(None == 4 && Texture == 5 && PerlinNoise == 15,
              "the op ABI values are frozen bundle compatibility");

/// The operator names of the op list, in list order. shader_build_tool checks the shared GLSL
/// dispatch (operator/pointwise_color_dispatch.inc) against this set, so an operator cannot join
/// the list without a dispatch branch (or a declared exemption in that file).
inline std::vector<std::string> PointwiseOpNames() {
  return {
#define TGFX_OP_NAME(name, id, params, member, passBoundarySensitive) #name,
      TGFX_POINTWISE_OPS(TGFX_OP_NAME)
#undef TGFX_OP_NAME
  };
}

/// The OP_* macro spelling of an operator, from the table: the tool relates an operator's name to
/// its define while checking the shared GLSL dispatch against the op list. Returns an empty
/// string for a name the table does not carry.
inline std::string DefineName(const std::string& name) {
#define TGFX_OP_DEFINE_LOOKUP(opName, id, inputs, sensitive, defineName) \
  if (name == #opName) {                                                 \
    return #defineName;                                                  \
  }
  TGFX_CHAIN_OPS(TGFX_OP_DEFINE_LOOKUP)
#undef TGFX_OP_DEFINE_LOOKUP
  return {};
}

/// The OP_* macros every compiled variant receives, in table order (ascending by ABI value, as the
/// asserts above require), so the output is byte-identical whatever order the rows were written.
/// The table is their only definition: no shader source can carry a second copy that drifts, and
/// shader_build_tool prepends this list to every stage's defines.
inline std::vector<std::string> Defines() {
  std::vector<std::string> defines;
  defines.reserve(static_cast<size_t>(ChainISA::OpCount));
#define TGFX_OP_DEFINE(opName, id, inputs, sensitive, defineName) \
  defines.push_back("OP_" + std::string(#defineName) + "=" + std::to_string(id));
  TGFX_CHAIN_OPS(TGFX_OP_DEFINE)
#undef TGFX_OP_DEFINE
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
