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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/bundle/ChainSpecializer.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif
#include <utility>
#include <vector>
#include "gpu/aot/bind/Binder.h"
#include "gpu/aot/ir/DrawIR.h"

namespace tgfx {

bool ChainSpecializationEnabled() {
  static const bool enabled = [] {
#ifdef __EMSCRIPTEN__
    // The wasm build has no environment: the demo pages opt in with ?aotspec=1.
    return EM_ASM_INT({
      return new URLSearchParams(location.search).get('aotspec') === '1' ? 1 : 0;
    }, 0) != 0;
#else
    const char* value = std::getenv("TGFX_AOT_SPECIALIZE");
    return value != nullptr && value[0] == '1';
#endif
  }();
  return enabled;
}

namespace {

// The emitter collapses "#pragma tgfx_spec_accessors begin" to a single token, so the markers are
// matched as substrings of whatever line carries them.
constexpr const char* kAccessorsBegin = "tgfx_spec_accessorsbegin";
constexpr const char* kAccessorsEnd = "tgfx_spec_accessorsend";
constexpr const char* kExecutorBegin = "tgfx_spec_executorbegin";
constexpr const char* kExecutorEnd = "tgfx_spec_executorend";

// Replaces the region from the begin marker's line through the end marker's line (both inclusive)
// with `replacement` (which must end with a newline). On success, `regionOut` receives the
// original region text. Returns false when either marker is missing.
bool ReplaceRegion(std::string* text, const char* begin, const char* end,
                   const std::string& replacement, std::string* regionOut) {
  auto beginPos = text->find(begin);
  if (beginPos == std::string::npos) {
    return false;
  }
  auto lineStart = text->rfind('\n', beginPos);
  lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
  auto endPos = text->find(end, beginPos);
  if (endPos == std::string::npos) {
    return false;
  }
  auto lineEnd = text->find('\n', endPos);
  if (lineEnd == std::string::npos) {
    return false;
  }
  if (regionOut != nullptr) {
    *regionOut = text->substr(lineStart, lineEnd - lineStart);
  }
  text->replace(lineStart, lineEnd - lineStart + 1, replacement);
  return true;
}

std::string FormatInt4(const std::array<int, 4>& value) {
  return "ivec4(" + std::to_string(value[0]) + ", " + std::to_string(value[1]) + ", " +
         std::to_string(value[2]) + ", " + std::to_string(value[3]) + ")";
}

}  // namespace

std::string SpecializeChainFragmentGLSL(const std::string& artifactText,
                                        const AOTProgram& program) {
  if (program.slots.empty()) {
    return "";
  }
  auto packed = EncodeChainSlots(program);
  if (packed.empty()) {
    return "";
  }
  const auto slotCount = program.slots.size();
  const auto capacity = packed.size();

  std::string text = artifactText;
  std::string accessorsRegion;
  std::string executorRegion;
  // Locate both regions first; the executor's original text decides the XP form, and either
  // region missing means the artifact predates the specialization split — keep it generic.
  {
    auto beginPos = text.find(kAccessorsBegin);
    if (beginPos == std::string::npos) {
      return "";
    }
    auto accessorsEnd = text.find(kAccessorsEnd, beginPos);
    if (accessorsEnd == std::string::npos) {
      return "";
    }
    auto executorBegin = text.find(kExecutorBegin);
    if (executorBegin == std::string::npos || executorBegin < accessorsEnd) {
      return "";
    }
    if (text.find(kExecutorEnd, executorBegin) == std::string::npos) {
      return "";
    }
    auto lineStart = text.rfind('\n', beginPos);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    auto lineEnd = text.find('\n', accessorsEnd);
    accessorsRegion = text.substr(lineStart, lineEnd == std::string::npos
                                                ? std::string::npos
                                                : lineEnd - lineStart);
    lineStart = text.rfind('\n', executorBegin);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    lineEnd = text.find('\n', text.find(kExecutorEnd, executorBegin));
    executorRegion = text.substr(lineStart, lineEnd == std::string::npos
                                                 ? std::string::npos
                                                 : lineEnd - lineStart);
  }
  if (accessorsRegion.find("SlotPacked") == std::string::npos ||
      executorRegion.find("chainRunSlots") == std::string::npos) {
    return "";
  }
  const bool hasXP = executorRegion.find("xpBlended") != std::string::npos;
  // The emitted artifact carries the CHAIN_XP_SRC_UNPREMUL macro already expanded (the build's
  // preprocessing resolved it), so the specialized executor reuses the exact expression the
  // generic region used — its form differs by layout (a plain `result` where the coverage
  // subtree rides the final coverage instead of the source).
  std::string xpSrcExpr;
  if (hasXP) {
    auto assign = executorRegion.find("chainXPSrc");
    if (assign == std::string::npos) {
      return "";
    }
    auto eq = executorRegion.find('=', assign);
    auto semi = executorRegion.find(';', eq);
    if (eq == std::string::npos || semi == std::string::npos) {
      return "";
    }
    xpSrcExpr = executorRegion.substr(eq + 1, semi - eq - 1);
    while (!xpSrcExpr.empty() && xpSrcExpr.front() == ' ') {
      xpSrcExpr.erase(xpSrcExpr.begin());
    }
    if (xpSrcExpr.empty()) {
      return "";
    }
  }

  // Folded structural constants: exactly the words EncodeChainSlots uploads, so the specialized
  // shader computes the same values the interpreter would read from the uniform block.
  std::string accessors;
  accessors += "#pragma tgfx_spec_accessorsbegin\n";
  accessors += "const int TGFX_SC_SlotCount = " + std::to_string(slotCount) + ";\n";
  accessors += "const int TGFX_SC_RootIndex = " + std::to_string(program.rootRegister) + ";\n";
  accessors +=
      "const int TGFX_SC_CoverageRoot = " + std::to_string(program.coverageRootRegister) + ";\n";
  accessors +=
      "const int TGFX_SC_ClipRegister = " + std::to_string(program.clipCoverageRegister) + ";\n";
  accessors += "const ivec4 TGFX_SC_Slots[" + std::to_string(capacity) + "] = ivec4[" +
               std::to_string(capacity) + "](";
  for (size_t i = 0; i < capacity; ++i) {
    accessors += FormatInt4(packed[i]);
    accessors += (i + 1 < capacity) ? ", " : ");\n";
  }
  accessors += "int chainSlotCount() { return TGFX_SC_SlotCount; }\n";
  accessors += "ivec4 chainSlot(int slot) { return TGFX_SC_Slots[slot]; }\n";
  accessors += "int chainRootIndex() { return TGFX_SC_RootIndex; }\n";
  accessors += "int chainCoverageRootIndex() { return TGFX_SC_CoverageRoot; }\n";
  accessors += "int chainClipRegister() { return TGFX_SC_ClipRegister; }\n";
  accessors += "#pragma tgfx_spec_accessorsend\n";

  // One call site per declared slot at a literal index: the op dispatch, the register reads and
  // the result-register writes all fold without relying on driver loop unrolling. Dead slots
  // (OP_NONE tail, outRegister == 0) fold to nothing.
  std::string executor;
  executor += "#pragma tgfx_spec_executorbegin\n";
  executor += "void chainRunSlots(out vec4 result, out vec4 clipRegister, "
              "out vec4 coverageRootRegister";
  if (hasXP) {
    executor += ", out vec4 xpBlended";
  }
  executor += ") {\n";
  executor += "  result = vec4(0.0);\n";
  executor += "  clipRegister = vec4(0.0);\n";
  executor += "  coverageRootRegister = vec4(0.0);\n";
  if (hasXP) {
    executor += "  xpBlended = vec4(0.0);\n";
  }
  executor += "#define TGFX_CHAIN_RUN_SLOT(n) \\\n";
  executor += "  { \\\n";
  executor += "    vec4 value = evalChainSlot(n); \\\n";
  executor += "    int outRegister = (TGFX_SC_Slots[n].w >> 20) & 0x1F; \\\n";
  executor += "    if (outRegister != 0) { \\\n";
  executor += "      int written = outRegister - 1; \\\n";
  executor += "      chainResults[written] = value; \\\n";
  executor += "      if (written == TGFX_SC_RootIndex) { \\\n";
  executor += "        result = value; \\\n";
  executor += "      } \\\n";
  executor += "      if (written == TGFX_SC_ClipRegister) { \\\n";
  executor += "        clipRegister = value; \\\n";
  executor += "      } \\\n";
  executor += "      if (written == TGFX_SC_CoverageRoot) { \\\n";
  executor += "        coverageRootRegister = value; \\\n";
  executor += "      } \\\n";
  executor += "    } \\\n";
  executor += "  }\n";
  executor += "  TGFX_CHAIN_RUN_SLOT(0)\n";
  for (size_t n = 1; n < capacity; ++n) {
    executor += "  if (" + std::to_string(n) +
                " < TGFX_SC_SlotCount) { TGFX_CHAIN_RUN_SLOT(" + std::to_string(n) + ") }\n";
  }
  if (hasXP) {
    executor += "  if (xpNeedsBlendPass()) { \\\n";
    executor += "    chainXPSrc = " + xpSrcExpr + "; \\\n";
    executor += "    xpBlended = evalChainSlot(TGFX_SC_SlotCount); \\\n";
    executor += "  }\n";
  }
  executor += "#undef TGFX_CHAIN_RUN_SLOT\n";
  executor += "}\n";
  executor += "#pragma tgfx_spec_executorend\n";

  if (!ReplaceRegion(&text, kAccessorsBegin, kAccessorsEnd, accessors, nullptr) ||
      !ReplaceRegion(&text, kExecutorBegin, kExecutorEnd, executor, nullptr)) {
    return "";
  }
  return text;
}

std::vector<std::pair<std::string, double>> MakeChainSpecializationConstantsVulkan(
    const AOTProgram& program) {
  std::vector<std::pair<std::string, double>> constants;
  auto packed = EncodeChainSlots(program);
  if (packed.empty()) {
    return constants;
  }
  // The SpecId assignment mirrors the accessors' layout(constant_id) declarations.
  constants.emplace_back("0", 1);
  constants.emplace_back("1", static_cast<double>(program.slots.size()));
  constants.emplace_back("2", static_cast<double>(program.rootRegister));
  constants.emplace_back("3", static_cast<double>(program.coverageRootRegister));
  constants.emplace_back("4", static_cast<double>(program.clipCoverageRegister));
  for (size_t s = 0; s < packed.size(); ++s) {
    for (int c = 0; c < 4; ++c) {
      constants.emplace_back(std::to_string(10 + s * 4 + static_cast<size_t>(c)),
                             static_cast<double>(packed[s][static_cast<size_t>(c)]));
    }
  }
  return constants;
}

std::string SpecializeChainFragmentWGSL(const std::string& artifactText,
                                        const AOTProgram& program) {
  auto packed = EncodeChainSlots(program);
  if (packed.empty()) {
    return "";
  }
  // tint's WGSL keeps the override declarations verbatim (names and ids), so the specialization
  // is a handful of default-value rewrites — the same shape as the GL text path. Each override
  // appears exactly once per artifact; the trailing literal is replaced in place.
  std::string text = artifactText;
  auto setDefault = [&text](const std::string& name, long value) -> bool {
    auto anchor = text.find("override " + name + " : i32 = ");
    if (anchor == std::string::npos) {
      return false;
    }
    auto valueStart = anchor + ("override " + name + " : i32 = ").size();
    auto end = text.find('i', valueStart);
    if (end == std::string::npos) {
      return false;
    }
    text.replace(valueStart, end - valueStart, std::to_string(value));
    return true;
  };
  if (!setDefault("CHAIN_SPECIALIZED", 1) || !setDefault("SC_SlotCount",
                                                         static_cast<long>(program.slots.size())) ||
      !setDefault("SC_RootIndex", static_cast<long>(program.rootRegister)) ||
      !setDefault("SC_CoverageRoot", static_cast<long>(program.coverageRootRegister)) ||
      !setDefault("SC_ClipReg", static_cast<long>(program.clipCoverageRegister))) {
    return "";
  }
  for (size_t s = 0; s < packed.size(); ++s) {
    for (int c = 0; c < 4; ++c) {
      if (!setDefault("SC_S" + std::to_string(s) + "_" + std::to_string(c),
                      static_cast<long>(packed[s][static_cast<size_t>(c)]))) {
        return "";
      }
    }
  }
  return text;
}

std::vector<std::pair<std::string, double>> MakeChainSpecializationConstants(
    const AOTProgram& program) {
  std::vector<std::pair<std::string, double>> constants;
  auto packed = EncodeChainSlots(program);
  if (packed.empty()) {
    return constants;
  }
  constants.emplace_back("CHAIN_SPECIALIZED", 1);
  constants.emplace_back("SC_SlotCount", static_cast<double>(program.slots.size()));
  constants.emplace_back("SC_RootIndex", static_cast<double>(program.rootRegister));
  constants.emplace_back("SC_CoverageRoot", static_cast<double>(program.coverageRootRegister));
  constants.emplace_back("SC_ClipReg", static_cast<double>(program.clipCoverageRegister));
  for (size_t s = 0; s < packed.size(); ++s) {
    for (int c = 0; c < 4; ++c) {
      constants.emplace_back("SC_S" + std::to_string(s) + "_" + std::to_string(c),
                             static_cast<double>(packed[s][static_cast<size_t>(c)]));
    }
  }
  return constants;
}

}  // namespace tgfx
