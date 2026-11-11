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
#include "verify/ContractChecks.h"

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

}  // namespace tgfx
