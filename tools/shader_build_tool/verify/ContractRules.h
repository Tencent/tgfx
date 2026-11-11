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
#include "core/Diagnostics.h"
#include "core/Types.h"

namespace tgfx {

// The verify sublayer holds the build-time contract checks, split by what a rule reads:
//
//   source rules   read the shader text            (macro names, the pointwise dispatch, spec readers)
//   artifact rules read what the build produced    (the reflection contracts, the binding layout)
//
// A rule reports each violation as a Diagnostic (with its code and subject) and returns the
// count; the build gates on the sum, so a contract mismatch fails the build instead of
// surfacing as a runtime miss. Rules never print: the cli renders the records. The registries
// below are the only way the build reaches a check — adding one means adding it to a registry,
// not threading a new call through main.

/// The inputs of a source-level rule: the directory the shaders live in.
struct SourceRuleContext {
  std::string shaderDir;
};

/// The inputs of an artifact-level rule: the variants the build produced.
struct ArtifactRuleContext {
  const std::vector<VariantData>* variants = nullptr;
};

/// One contract check over the shader sources.
class SourceRule {
 public:
  virtual ~SourceRule() = default;

  /// Runs the check against the context, reporting every violation to diagnostics. Returns the
  /// violation count (0 = clean).
  virtual size_t check(const SourceRuleContext& context, Diagnostics* diagnostics) const = 0;
};

/// One contract check over what the build produced.
class ArtifactRule {
 public:
  virtual ~ArtifactRule() = default;

  /// Runs the check against the context, reporting every violation to diagnostics. Returns the
  /// violation count (0 = clean).
  virtual size_t check(const ArtifactRuleContext& context, Diagnostics* diagnostics) const = 0;
};

/// The source rules of one build, in the order their violations print: the macro names, the
/// pointwise dispatch, and the spec-reader declarations.
std::vector<std::unique_ptr<SourceRule>> MakeSourceRules();

/// The artifact rules of one build, in the order their violations print: the reflection contracts
/// and the binding layout.
std::vector<std::unique_ptr<ArtifactRule>> MakeArtifactRules();

}  // namespace tgfx
