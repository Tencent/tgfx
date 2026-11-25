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
#include "gpu/ShaderBindingLayout.h"
#include "gpu/shaders/contract/ChainISA.h"
#include "gpu/shaders/contract/ChainOps.h"

namespace tgfx {

// Single source of truth for the cross-language kernel contracts, consumed by the runtime
// (the binder's uniform writers, the chain op enum) and by shader_build_tool (which validates the GLSL side
// against these constants at bundle build time). Renaming a field or an op code anywhere fails
// the bundle build instead of silently leaving contract fields unwritten or misinterpreting a
// chain slot.
// The chain kernel's result-register file: chainResults holds one vec4 per live result, and the
// register allocator recycles them by last use. A local array cannot be expressed by the module
// declarations, so the size rides the kernel contract and the build injects it as
// TGFX_CHAIN_REGISTERS into the shared chain text.
inline constexpr size_t MaxChainRegisters = 16;

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

// The op values of the chain interpreter: every one derives from ChainISA's single list
// (contract/ChainISA.h), which is also the list Defines() turns into the OP_* defines every
// compiled stage receives.
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


}  // namespace detail

static_assert(detail::AbiValuesUnique(), "two ops share one ABI value");
static_assert(detail::AbiValuesAscending(), "the op table must stay in ascending ABI order");
static_assert(static_cast<int>(detail::AbiValueCount) == ChainISA::OpCount, "op count mismatch");
static_assert(None == 4 && Texture == 5 && PerlinNoise == 15,
              "the op ABI values are frozen bundle compatibility");



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

// The sampler naming contract (the TextureSampler_ prefix and the semantic sampler names) moved
// to gpu/ShaderBindingLayout.h; this header re-exports it through the include above.

}  // namespace tgfx
