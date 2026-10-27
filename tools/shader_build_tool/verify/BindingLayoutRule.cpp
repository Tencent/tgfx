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
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "BindingLayoutRule.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "core/DiagnosticCodes.h"
#include "gpu/ShaderBindingLayout.h"
#include "gpu/ShaderProfile.h"

namespace tgfx {

namespace {

std::string ProfileTagOf(const VariantData& variant) {
  auto profile = ParseProfileTag(variant.profileTag);
  return profile ? ProfileTag(*profile) : variant.profileTag;
}

void Report(Diagnostics* diagnostics, Severity severity, const char* code,
            const VariantData& variant, std::string message) {
  Diagnostic diagnostic;
  diagnostic.severity = severity;
  diagnostic.layer = Layer::Verify;
  diagnostic.code = code;
  diagnostic.shader = variant.shaderName;
  diagnostic.profile = ProfileTagOf(variant);
  diagnostic.vertPermutationIndex = static_cast<int>(variant.vertPermutationIndex);
  diagnostic.fragPermutationIndex = static_cast<int>(variant.fragPermutationIndex);
  diagnostic.message = std::move(message);
  diagnostics->report(std::move(diagnostic));
}

// One @group(N)/@binding(M) qualifier pair found in a WGSL text. Tint writes the qualifiers of
// one variable in either order ("@binding(n) @group(m)" in practice), so both orders match.
struct GroupBinding {
  int group = -1;
  int binding = -1;
};

std::vector<GroupBinding> ScanWgslGroupBindings(const std::vector<uint8_t>& blob) {
  std::string text(blob.begin(), blob.end());
  static const std::regex groupFirst(R"(@group\((\d+)\)\s*@binding\((\d+)\))");
  static const std::regex bindingFirst(R"(@binding\((\d+)\)\s*@group\((\d+)\))");
  std::vector<GroupBinding> found;
  std::smatch match;
  std::string::const_iterator searchStart(text.cbegin());
  while (std::regex_search(searchStart, text.cend(), match, groupFirst)) {
    found.push_back({std::stoi(match[1].str()), std::stoi(match[2].str())});
    searchStart = match.suffix().first;
  }
  searchStart = text.cbegin();
  while (std::regex_search(searchStart, text.cend(), match, bindingFirst)) {
    found.push_back({std::stoi(match[2].str()), std::stoi(match[1].str())});
    searchStart = match.suffix().first;
  }
  return found;
}

/// Reports the WGSL qualifiers that do not sit where the runtime's three bind groups expect
/// them: the vertex stage declares the vertex UBO in group 0 / binding 0, the fragment stage
/// the fragment UBO in group 1 / binding 1 and its texture/sampler pairs in group 2 at the
/// SamplerBinding packing. Every scan that cannot run (a pattern the emitter no longer produces,
/// an empty qualifier table, a truncated body) is reported as unverifiable: a check that never
/// looked is not a check that passed.
size_t CheckWebgpuVariant(const VariantData& variant, Diagnostics* diagnostics) {
  size_t errors = 0;
  // The vertex entry parameters of the WGSL must all sit on slots the reflection covers: the
  // runtime pipeline binds the reflected locations, so a parameter on an uncovered slot would
  // read whatever the layout happens to leave there. The shaderc performance pass legally drops
  // inputs the vertex body never uses, so the WGSL may declare fewer parameters than the
  // reflection — never more, and never outside it.
  {
    std::string text(variant.vertexBlob.begin(), variant.vertexBlob.end());
    std::set<int> reflected;
    for (const auto& input : variant.vertexReflection.vertexInputs) {
      reflected.insert(static_cast<int>(input.location));
    }
    auto mainAt = text.find("fn main(");
    if (mainAt == std::string::npos) {
      Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, variant,
             "the WGSL vertex stage has no 'fn main('; the vertex slot check could not run");
      errors++;
    } else {
      auto arrowAt = text.find("->", mainAt);
      if (arrowAt == std::string::npos) {
        Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, variant,
               "the WGSL vertex stage's main declaration has no return arrow; the vertex slot "
               "check could not run");
        errors++;
      } else {
        auto params = text.substr(mainAt + 8, arrowAt - mainAt - 8);
        size_t cursor = 0;
        while ((cursor = params.find("@location(", cursor)) != std::string::npos) {
          auto end = params.find(')', cursor);
          if (end == std::string::npos) {
            Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable,
                   variant,
                   "the WGSL vertex stage has an unterminated '@location('; the vertex slot "
                   "check could not finish");
            errors++;
            break;
          }
          auto slot = std::stoi(params.substr(cursor + 10, end - cursor - 10));
          if (reflected.count(slot) == 0) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
                   "the WGSL reads vertex slot " + std::to_string(slot) +
                       ", which the reflected vertex inputs do not cover");
            errors++;
          }
          cursor = end;
        }
      }
    }
  }
  auto vertBindings = ScanWgslGroupBindings(variant.vertexBlob);
  if (vertBindings.empty()) {
    // The vertex stage always declares the vertex UBO, so an empty table means the scan could
    // not read the qualifiers — not that the stage has none.
    Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, variant,
           "the WGSL vertex stage declares no @group/@binding pairs; the vertex binding check "
           "could not run");
    errors++;
  }
  for (const auto& pair : vertBindings) {
    if (pair.group != VERTEX_UBO_DESCRIPTOR_SET || pair.binding != VERTEX_UBO_BINDING_POINT) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
             "vertex resource outside group " + std::to_string(VERTEX_UBO_DESCRIPTOR_SET) +
                 "/binding " + std::to_string(VERTEX_UBO_BINDING_POINT) + " (found @group(" +
                 std::to_string(pair.group) + ") @binding(" + std::to_string(pair.binding) + "))");
      errors++;
    }
  }
  auto fragBindings = ScanWgslGroupBindings(variant.fragmentBlob);
  std::set<int> textureBindings;
  for (const auto& pair : fragBindings) {
    if (pair.group == FRAGMENT_UBO_DESCRIPTOR_SET) {
      if (pair.binding != FRAGMENT_UBO_BINDING_POINT) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
               "fragment UBO at @group(" + std::to_string(pair.group) + ") @binding(" +
                   std::to_string(pair.binding) + "), expected binding " +
                   std::to_string(FRAGMENT_UBO_BINDING_POINT));
        errors++;
      }
    } else if (pair.group == TEXTURE_DESCRIPTOR_SET) {
      textureBindings.insert(pair.binding);
    } else {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
             "fragment resource in group " + std::to_string(pair.group) +
                 " (the pipeline binds only groups " + std::to_string(VERTEX_UBO_DESCRIPTOR_SET) +
                 ", " + std::to_string(FRAGMENT_UBO_DESCRIPTOR_SET) + " and " +
                 std::to_string(TEXTURE_DESCRIPTOR_SET) + ")");
      errors++;
    }
  }
  // The texture pairs sit on the SamplerBinding packing: bindings START+2i and START+2i+1.
  auto expected = static_cast<int>(variant.fragmentReflection.samplers.size());
  for (int i = 0; i < expected; ++i) {
    auto pair = SamplerBinding(ShaderProfile::WebGPU, i);
    if (textureBindings.count(pair.first) == 0 || textureBindings.count(pair.second) == 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
             "texture pair " + std::to_string(i) + " missing from group " +
                 std::to_string(TEXTURE_DESCRIPTOR_SET) + " (expected bindings " +
                 std::to_string(pair.first) + " and " + std::to_string(pair.second) + ")");
      errors++;
    }
  }
  return errors;
}

// The descriptor set and binding decorations of one SPIR-V module: a word-level walk over the
// instruction stream that pairs the DescriptorSet and Binding decorations of each decorated id.
// Returns false when the blob is not a parseable SPIR-V module (wrong size, wrong magic, or a
// truncated instruction stream), which the caller reports as an unverifiable stage.
bool ScanSpirvDecorations(const std::vector<uint8_t>& blob,
                          std::map<uint32_t, std::pair<int, int>>* decorations) {
  if (blob.size() < 20 || blob.size() % 4 != 0) {
    return false;
  }
  std::vector<uint32_t> words(blob.size() / 4);
  std::memcpy(words.data(), blob.data(), blob.size());
  if (words[0] != 0x07230203u) {
    return false;
  }
  constexpr uint32_t OpDecorate = 71;
  constexpr uint32_t DecorationBinding = 33;
  constexpr uint32_t DecorationDescriptorSet = 34;
  size_t offset = 5;  // magic, version, generator, bound, schema
  while (offset < words.size()) {
    auto opcode = words[offset] & 0xffffu;
    auto wordCount = words[offset] >> 16u;
    if (wordCount == 0 || offset + wordCount > words.size()) {
      return false;
    }
    if (opcode == OpDecorate && wordCount >= 4) {
      auto target = words[offset + 1];
      auto kind = words[offset + 2];
      auto value = static_cast<int>(words[offset + 3]);
      if (kind == DecorationDescriptorSet) {
        (*decorations)[target].first = value;
      } else if (kind == DecorationBinding) {
        (*decorations)[target].second = value;
      }
    }
    offset += wordCount;
  }
  return true;
}

/// Reports the SPIR-V resources that do not sit where the runtime's three descriptor sets
/// expect them: the vertex UBO in set 0 / binding 0, the fragment UBO in set 1 / binding 1,
/// the samplers in set 2.
size_t CheckVulkanVariant(const VariantData& variant, Diagnostics* diagnostics) {
  size_t errors = 0;
  auto checkStage = [&](const std::vector<uint8_t>& blob, const char* stage, int uboSet,
                        int uboBinding, bool expectSamplers) {
    std::map<uint32_t, std::pair<int, int>> decorations;
    if (!ScanSpirvDecorations(blob, &decorations)) {
      Report(diagnostics, Severity::Unverifiable, DiagnosticCode::VerifyNotApplicable, variant,
             std::string("the ") + stage +
                 " stage blob is not a parseable SPIR-V module; the binding check could not run");
      errors++;
      return;
    }
    for (const auto& entry : decorations) {
      const auto& setBinding = entry.second;
      if (setBinding.first == uboSet) {
        if (setBinding.second != uboBinding) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
                 std::string(stage) + " UBO in set " + std::to_string(setBinding.first) +
                     " at binding " + std::to_string(setBinding.second) + ", expected " +
                     std::to_string(uboBinding));
          errors++;
        }
      } else if (setBinding.first == TEXTURE_DESCRIPTOR_SET) {
        if (!expectSamplers) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
                 std::string(stage) + " resource in the texture set " +
                     std::to_string(TEXTURE_DESCRIPTOR_SET));
          errors++;
        }
      } else {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyBindingLayout, variant,
               std::string(stage) + " resource in set " + std::to_string(setBinding.first) +
                   " (the pipeline layout has sets " + std::to_string(VERTEX_UBO_DESCRIPTOR_SET) +
                   ", " + std::to_string(FRAGMENT_UBO_DESCRIPTOR_SET) + " and " +
                   std::to_string(TEXTURE_DESCRIPTOR_SET) + ")");
        errors++;
      }
    }
  };
  checkStage(variant.vertexBlob, "vertex", VERTEX_UBO_DESCRIPTOR_SET, VERTEX_UBO_BINDING_POINT,
             false);
  checkStage(variant.fragmentBlob, "fragment", FRAGMENT_UBO_DESCRIPTOR_SET,
             FRAGMENT_UBO_BINDING_POINT, true);
  return errors;
}

}  // namespace

size_t BindingLayoutRule::check(const ArtifactRuleContext& context,
                                Diagnostics* diagnostics) const {
  size_t violations = 0;
  for (const auto& variant : *context.variants) {
    auto profile = ParseProfileTag(variant.profileTag);
    if (!profile) {
      continue;
    }
    if (*profile == ShaderProfile::WebGPU) {
      violations += CheckWebgpuVariant(variant, diagnostics);
    } else if (*profile == ShaderProfile::Vulkan) {
      violations += CheckVulkanVariant(variant, diagnostics);
    }
    // The GL and GLES storage carries no set or binding (the emitters strip the qualifiers and
    // the form check rejects any survivor), and MSL carries only the binding numbers — the
    // MSLResourceBinding mapping the translator builds from the SPIR-V decorations.
  }
  return violations;
}

}  // namespace tgfx
