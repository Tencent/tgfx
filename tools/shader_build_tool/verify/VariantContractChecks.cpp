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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "verify/ContractChecks.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "core/DiagnosticCodes.h"
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/bind/AOTParamRegistry.h"
#include "gpu/shaders/Families.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/PrecompiledShader.h"
#include "gpu/shaders/contract/ShaderBundleContract.h"
#include "verify/ContractCheckSupport.h"

namespace tgfx {

namespace {

using contract_check::HasUniform;
using contract_check::Report;

}  // namespace

// Every shader whose fragment block carries the clip contract marker (HasClip) must declare the
// whole contract: a missing field means the onSetData writers (which reference the same
// ClipContract constants) would silently skip their upload and the clip would read stale data.
// The Texture element names of the declaration-driven families: the derived layout numbers
// them, so the reflected set is legal exactly as declared.
static const std::set<std::string>& DeclaredTextureNames() {
  static const std::set<std::string> names = [] {
    std::set<std::string> result;
    for (size_t familyIndex = 0; familyIndex < FamilyCount(); ++familyIndex) {
      const Family& family = Families()[familyIndex];
      for (const auto& variant : EnumerateVariants(family, static_cast<uint16_t>(familyIndex))) {
        const auto modules = ModulesOf(family, variant);
        for (uint8_t slot = 0; slot < modules.count; ++slot) {
          const Module* module = modules.list[slot];
          if (module == nullptr || !ModulePresent(family, variant, slot)) {
            continue;
          }
          for (uint8_t index = 0; index < module->elementCount; ++index) {
            const Element& element = module->elements[index];
            if (element.kind == Element::Kind::Texture) {
              result.insert(ElementNameOf(module, element));
            }
          }
        }
      }
    }
    return result;
  }();
  return names;
}

static bool IsDeclaredTextureName(const std::string& name) {
  return DeclaredTextureNames().count(name) != 0;
}

size_t ValidateClipContractFields(const std::vector<VariantData>& variants,
                                  Diagnostics* diagnostics) {
  const std::string contractFields[] = {
      ClipContract::Rect,          ClipContract::HasClip, ClipContract::LocalRect,
      ClipContract::RadiiX,        ClipContract::RadiiY,  ClipContract::AntiAlias,
      ClipContract::DeviceToLocal,
  };
  size_t errors = 0;
  for (const auto& variant : variants) {
    if (!HasUniform(variant.fragmentReflection, ClipContract::HasClip)) {
      continue;
    }
    for (const auto& field : contractFields) {
      if (!HasUniform(variant.fragmentReflection, field)) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyClipField, variant.shaderName,
               variant.profileTag, -1, static_cast<int>(variant.fragPermutationIndex),
               "carries the clip contract but is missing uniform '" + field + "'");
        ++errors;
      }
    }
  }
  return errors;
}


size_t ValidateReflectionContracts(const std::vector<VariantData>& variants,
                                   Diagnostics* diagnostics) {
  size_t errors = 0;
  const auto maxEntries = BundleFormat::Reflection::MaxListEntries;
  const auto maxNameLength = BundleFormat::Reflection::MaxNameLength;
  for (const auto& variant : variants) {
    const StageReflectionData* stages[] = {&variant.vertexReflection, &variant.fragmentReflection};
    for (const auto* reflection : stages) {
      // The bundle writer serializes name lengths and per-stage entry counts as uint8
      // (BundleWriter's WriteU8 sites): values over the format's byte would wrap silently (a
      // 256-byte name is written with length 0), so reject them here at build time instead.
      if (reflection->uniforms.size() > maxEntries || reflection->samplers.size() > maxEntries) {
        Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection, variant.shaderName,
               variant.profileTag, -1, -1,
               "reflection exceeds the " + std::to_string(maxEntries) +
                   "-entry serialization limit (uniforms=" +
                   std::to_string(reflection->uniforms.size()) +
                   ", samplers=" + std::to_string(reflection->samplers.size()) + ")");
        ++errors;
      }
      std::map<std::string, int> seenNames;
      for (const auto* list : {&reflection->uniforms, &reflection->samplers}) {
        bool isSamplerList = list == &reflection->samplers;
        for (const auto& entry : *list) {
          if (entry.name.find('\0') != std::string::npos) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "reflection carries an embedded NUL in a name");
            ++errors;
            continue;
          }
          if (entry.name.size() > maxNameLength) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "entry '" + entry.name + "' exceeds the " + std::to_string(maxNameLength) +
                       "-byte name serialization limit");
            ++errors;
            continue;
          }
          if (++seenNames[entry.name] > 1) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "declares duplicate name '" + entry.name + "' in one stage");
            ++errors;
          }
          if (entry.arraySize < 1 || (isSamplerList && entry.arraySize != 1)) {
            Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection,
                   variant.shaderName, variant.profileTag, -1, -1,
                   "entry '" + entry.name + "' has arraySize " + std::to_string(entry.arraySize) +
                       " (uniforms need >= 1, samplers need exactly 1)");
            ++errors;
          }
        }
      }
      // Ordinary texture bindings must use the generic TextureSampler_<N> pattern with N dense
      // from zero (a gap would shift every later binding on the runtime side); dedicated-purpose
      // samplers use their registered semantic names instead (SamplerContract in KernelContract.h).
      // A declaration-driven family's Texture elements name their samplers <Module>_<name>, which
      // the derived layout numbers, so those names are legal exactly as declared.
      int nextSamplerIndex = 0;
      for (const auto& sampler : reflection->samplers) {
        const std::string prefix = SamplerContract::TextureSamplerPrefix;
        bool generic = sampler.name.size() > prefix.size() &&
                       sampler.name.compare(0, prefix.size(), prefix) == 0;
        if (generic) {
          auto digits = sampler.name.substr(prefix.size());
          generic = !digits.empty() && std::all_of(digits.begin(), digits.end(),
                                                   [](char ch) { return ch >= '0' && ch <= '9'; });
        }
        if (SamplerContract::IsSemanticSamplerName(sampler.name) ||
            IsDeclaredTextureName(sampler.name)) {
          continue;
        }
        int parsed = -1;
        if (generic) {
          parsed = std::stoi(sampler.name.substr(prefix.size()));
        }
        if (!generic) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection, variant.shaderName,
                 variant.profileTag, -1, -1,
                 "declares sampler '" + sampler.name +
                     "' (neither the TextureSampler_<N> pattern nor a registered semantic name; "
                     "see SamplerContract in KernelContract.h)");
          ++errors;
        } else if (parsed != nextSamplerIndex) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyReflection, variant.shaderName,
                 variant.profileTag, -1, -1,
                 "generic sampler #" + std::to_string(nextSamplerIndex) + " is named '" +
                     sampler.name + "' (indices must be dense from zero)");
          ++errors;
        }
        ++nextSamplerIndex;
      }
    }
  }
  return errors;
}

size_t ValidateInlineExpansion(const std::vector<InlineExpansionRecord>& records,
                               Diagnostics* diagnostics) {
  // One record per (shader, function) with the worst variant, instead of one per variant.
  struct Violation {
    size_t copies = 0;
    size_t instructions = 0;
    size_t duplicated = 0;
    size_t variants = 0;
    uint32_t worstVariant = 0;
  };
  std::map<std::pair<std::string, std::string>, Violation> violations = {};
  size_t invalid = 0;
  for (const auto& record : records) {
    if (!record.expansion.valid) {
      // Not a policy violation: the module itself could not be analyzed, so the check could not
      // run. Reported as unverifiable rather than folded into the policy count.
      Diagnostic diagnostic;
      diagnostic.severity = Severity::Unverifiable;
      diagnostic.layer = Layer::Verify;
      diagnostic.code = DiagnosticCode::VerifyNotApplicable;
      diagnostic.shader = record.shaderName;
      diagnostic.fragPermutationIndex = static_cast<int>(record.fragPermutationIndex);
      diagnostic.message = "the SPIR-V module could not be analyzed";
      diagnostics->report(std::move(diagnostic));
      ++invalid;
      continue;
    }
    for (const auto& function : record.expansion.functions) {
      auto duplicated = DuplicatedInstructions(function);
      if (duplicated < MaxDuplicatedInstructions) {
        continue;
      }
      auto& violation = violations[{record.shaderName, function.name}];
      ++violation.variants;
      if (duplicated > violation.duplicated) {
        violation.copies = function.copies;
        violation.instructions = function.instructions;
        violation.duplicated = duplicated;
        violation.worstVariant = record.fragPermutationIndex;
      }
    }
  }
  for (const auto& entry : violations) {
    const auto& violation = entry.second;
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.layer = Layer::Verify;
    diagnostic.code = DiagnosticCode::VerifyInlinePolicy;
    diagnostic.shader = entry.first.first;
    diagnostic.fragPermutationIndex = static_cast<int>(violation.worstVariant);
    diagnostic.message =
        "'" + entry.first.second + "' (" + std::to_string(violation.instructions) +
        " instructions) reaches the driver " + std::to_string(violation.copies) +
        " times in frag " + std::to_string(violation.worstVariant) + " (" +
        std::to_string(violation.variants) + " variant(s)), " +
        std::to_string(violation.duplicated) + " duplicated instructions; the limit is " +
        std::to_string(MaxDuplicatedInstructions) +
        ". Funnel the call sites into one: select the operands, then call once (or run the call "
        "in a loop whose pass count is uniform data).";
    diagnostics->report(std::move(diagnostic));
  }
  return invalid + violations.size();
}


// The base name of a reflected field: the runtime's structural suffix (Subset, Subset_1) appends
// an underscore and digits to the declared name, so the reconciliation resolves both forms.

static std::string BaseFieldName(const std::string& name) {
  auto separator = name.find_last_of('_');
  if (separator == std::string::npos || separator + 1 == name.size() || separator == 0) {
    return name;
  }
  for (size_t index = separator + 1; index < name.size(); ++index) {
    if (name[index] < '0' || name[index] > '9') {
      return name;
    }
  }
  return name.substr(0, separator);
}

// The declared entry for a reflected name, resolved through the exact name first and the
// structural-suffix-stripped base second: a name like GradientThresholds1_7 is itself declared,
// while Subset_1 resolves to the declared Subset.
static const std::pair<const std::string, UniformFormat>* DeclaredEntry(
    const std::map<std::string, UniformFormat>& formats, const std::string& name) {
  auto exact = formats.find(name);
  if (exact != formats.end()) {
    return &*exact;
  }
  auto base = formats.find(BaseFieldName(name));
  if (base != formats.end()) {
    return &*base;
  }
  return nullptr;
}

// The declared-name reconciliation, both directions: every reflected uniform field must have a
// declared writer constant with a matching format (a GLSL typo would otherwise read as zero or
// stale data at runtime), and every declared constant must appear in some variant (a dead
// declaration is a drift nobody reconciles any more).
size_t ValidateFamilyParams(const std::vector<VariantData>& variants,
                            Diagnostics* diagnostics) {
  size_t errors = 0;
  const auto& formats = AllParamFormats();
  std::set<std::string> usedNames;
  // A name the tables declare twice with different formats resolves to whichever entry the table
  // kept, so the reflection comparison alone could pass on the wrong declaration; reject the
  // conflict itself instead.
  for (const auto& name : AllParamConflicts()) {
    Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUndeclared, "", "", -1, -1,
           "declared parameter '" + name + "' appears with conflicting formats");
    ++errors;
  }
  // A build with no variants carries no reflection at all, so the usage direction has nothing to
  // say; the name-and-format direction below runs over the (empty) reflection and stays clean.
  if (variants.empty()) {
    return 0;
  }
  for (const auto& variant : variants) {
    const StageReflectionData* stages[] = {&variant.vertexReflection, &variant.fragmentReflection};
    for (const auto* reflection : stages) {
      for (const auto& entry : reflection->uniforms) {
        usedNames.insert(entry.name);
        usedNames.insert(BaseFieldName(entry.name));
        const auto* declared = DeclaredEntry(formats, entry.name);
        if (declared == nullptr) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUndeclared,
                 variant.shaderName, variant.profileTag, -1, -1,
                 "reflects uniform '" + entry.name +
                     "', which no module or catalog constant declares");
          ++errors;
        } else if (declared->second != static_cast<UniformFormat>(entry.format)) {
          Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUndeclared,
                 variant.shaderName, variant.profileTag, -1, -1,
                 "uniform '" + entry.name + "' reflects format " +
                     std::to_string(entry.format) + " but the constant '" + declared->first +
                     "' declares format " + std::to_string(static_cast<int>(declared->second)));
          ++errors;
        }
      }
    }
  }
  for (const auto& declared : formats) {
    if (usedNames.count(declared.first) == 0) {
      Report(diagnostics, Severity::Error, DiagnosticCode::VerifyParamUnused, "", "", -1, -1,
             "declared parameter '" + declared.first + "' is carried by no variant");
      ++errors;
    }
  }
  return errors;
}

// The pointwise dispatch rule: the shared GLSL dispatch must cover the op list. An operator needs
// an `op == OP_<NAME>` branch or a `tgfx_pointwise_exempt <Name>: <reason>` entry (the reason is
// mandatory: an exemption is a design decision, and the file is where it is written down). A
// branch or exemption naming no listed operator fails too, so renaming or removing an operator
// cannot leave a dead arm or a stale note behind.

}  // namespace tgfx
