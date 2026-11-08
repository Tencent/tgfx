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
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/spec/SpecInjector.h"
#include "gpu/shaders/contract/SpecContract.h"

namespace tgfx {

namespace {

/// Appends the captured values to a pipeline's fragment-constant table, keyed the way the
/// pipeline-constant backends resolve them: the SpecId in decimal (Metal's MSL function constants
/// carry the SpecId as their index; the Vulkan specialization map keys by SpecId). The master
/// switch is id 0, then every field's words in spec-table order — the same order the values hold.
void AppendSpecConstants(const std::vector<SpecFieldInfo>& fields, const SpecValues& values,
                         std::vector<std::pair<std::string, double>>* constants) {
  if (values.empty() || values.values[0] != 1) {
    return;
  }
  constants->emplace_back("0", 1.0);
  size_t next = 1;
  for (const auto& field : fields) {
    const auto words = field.arraySize == 1 ? 1u : field.arraySize * 4u;
    for (uint32_t word = 0; word < words; ++word) {
      const auto value = next < values.values.size() ? values.values[next] : 0;
      ++next;
      constants->emplace_back(std::to_string(field.firstId + word), static_cast<double>(value));
    }
  }
}

/// Driver source text (GLSL and HLSL): prepends one `#define <prefix><id> <value>` line per
/// captured value. The stored text declares each value as an idempotent macro default — GLSL's
/// `#ifndef TGFX_SPEC_<id>` block (the emitter rewrites the generated const into it), HLSL's
/// SPIRV-Cross `#ifndef SPIRV_CROSS_CONSTANT_ID_<id>` block — so the defines fold every accessor
/// and gate condition in the preprocessor pass, before the compiler parses a single line. GLSL
/// inserts after the #version line (directives must not precede it); HLSL prepends at the top
/// (its text opens with includes, not a version directive).
class MacroPrefixInjector final : public SpecInjector {
 public:
  MacroPrefixInjector(std::string macroPrefix, bool afterVersionLine)
      : prefix(std::move(macroPrefix)), afterVersion(afterVersionLine) {
  }

  bool Apply(const std::string& code, const std::vector<SpecFieldInfo>& fields,
             const SpecValues& values, SpecInjection* injection) const override {
    injection->constants.clear();
    AppendSpecConstants(fields, values, &injection->constants);
    if (injection->constants.empty()) {
      return false;
    }
    std::string defines;
    for (const auto& entry : injection->constants) {
      defines += "#define " + prefix + entry.first + " " +
                 std::to_string(static_cast<int>(entry.second)) + "\n";
    }
    injection->constants.clear();
    if (!afterVersion) {
      injection->code = defines + code;
      return true;
    }
    const auto versionPos = code.find("#version");
    if (versionPos == std::string::npos) {
      return false;
    }
    const auto versionEnd = code.find('\n', versionPos);
    if (versionEnd == std::string::npos) {
      return false;
    }
    injection->code = code.substr(0, versionEnd + 1) + defines + code.substr(versionEnd + 1);
    return true;
  }

 private:
  std::string prefix;
  bool afterVersion;
};

/// A precompiled container resolved with function/pipeline constants (Metal's metallib today,
/// Vulkan's SPIR-V specialization map later): the values join the pipeline descriptor.
class PipelineConstantInjector final : public SpecInjector {
 public:
  bool Apply(const std::string& /*code*/, const std::vector<SpecFieldInfo>& fields,
             const SpecValues& values, SpecInjection* injection) const override {
    AppendSpecConstants(fields, values, &injection->constants);
    return !injection->constants.empty();
  }
};

}  // namespace

const SpecInjector* FindSpecInjector(ShaderCodeFormat format) {
  static const MacroPrefixInjector glslMacros{SpecContract::ConstantPrefix, true};
  static const PipelineConstantInjector pipelineConstants;
  static const MacroPrefixInjector hlslMacros{SpecContract::HlslConstantPrefix, false};
  switch (format) {
    case ShaderCodeFormat::GLSL:
      return &glslMacros;
    case ShaderCodeFormat::HLSL:
      return &hlslMacros;
    case ShaderCodeFormat::METALLIB:
    case ShaderCodeFormat::SPIRV:
    case ShaderCodeFormat::WGSL:
      // The pipeline carries the table: Metal's function constants, Vulkan's specialization map
      // and WebGPU's pipeline constants all take the SpecId-keyed list at creation time.
      return &pipelineConstants;
    default:
      // MSL/DXBC: a precompiled container cannot take values, so the draw keeps the generic form.
      return nullptr;
  }
}

}  // namespace tgfx
