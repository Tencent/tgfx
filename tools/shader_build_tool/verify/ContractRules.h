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
//  Unless required by applicable law or agreed to in writing, software distributed under
//  the license is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied. see the license for the specific language governing
//  permissions and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "BundleWriter.h"
#include "InlineExpansion.h"

namespace tgfx {

// The verify sublayer holds the build-time contract checks, split by what a rule reads:
//
//   source rules   read the shader text            (op codes, slot capacities, references)
//   artifact rules read what the build produced    (clip fields, reflection, inline expansion)
//
// A rule prints each violation itself and returns the count; the build gates on the sum, so a
// contract mismatch fails the build instead of surfacing as a runtime miss. The registries
// below are the only way the build reaches a check — adding one (the binding-layout rule of
// the layout work, say) means adding it to a registry, not threading a new call through main.

/// The inputs of a source-level rule: the directory the shaders live in and the names of
/// every registered family.
struct SourceRuleContext {
  std::string shaderDir;
  std::vector<std::string> registeredShaders;
};

/// The inputs of an artifact-level rule: the variants the build produced and, per compiled
/// fragment permutation, the inline expansion analysis of its unoptimized SPIR-V.
struct ArtifactRuleContext {
  const std::vector<VariantData>* variants = nullptr;
  const std::vector<InlineExpansionRecord>* inlineExpansions = nullptr;
};

/// One contract check over the shader sources.
class SourceRule {
 public:
  virtual ~SourceRule() = default;

  /// Runs the check against the context. Returns the violation count (0 = clean); the rule
  /// prints each violation itself.
  virtual size_t check(const SourceRuleContext& context) const = 0;
};

/// One contract check over what the build produced.
class ArtifactRule {
 public:
  virtual ~ArtifactRule() = default;

  /// Runs the check against the context. Returns the violation count (0 = clean); the rule
  /// prints each violation itself.
  virtual size_t check(const ArtifactRuleContext& context) const = 0;
};

/// The source rules of one build, in the order their violations print: the chain op codes,
/// the pointwise op codes, the slot capacities, and the references to things that must still
/// exist.
std::vector<std::unique_ptr<SourceRule>> MakeSourceRules();

/// The artifact rules of one build, in the order their violations print: the clip contract
/// fields, the reflection contracts, the binding layout, and the inline expansion policy.
std::vector<std::unique_ptr<ArtifactRule>> MakeArtifactRules();

}  // namespace tgfx
