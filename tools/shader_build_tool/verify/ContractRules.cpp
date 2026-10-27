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

#include "ContractRules.h"

#include "ContractChecks.h"

namespace tgfx {

namespace {

// The source rules: each wraps one check of ContractChecks.cpp, whose implementation stays
// where it is — a rule owns when the check runs and what it is named by, not the check itself.

class ChainOpCodesRule : public SourceRule {
 public:
  size_t check(const SourceRuleContext& context) const override {
    return ValidateChainOpCodes(context.shaderDir);
  }
};

class PointwiseOpCodesRule : public SourceRule {
 public:
  size_t check(const SourceRuleContext& context) const override {
    return ValidatePointwiseOpCodes(context.shaderDir);
  }
};

class SlotCapacitiesRule : public SourceRule {
 public:
  size_t check(const SourceRuleContext& context) const override {
    return ValidateSlotCapacities(context.shaderDir);
  }
};

class SourceReferencesRule : public SourceRule {
 public:
  size_t check(const SourceRuleContext& context) const override {
    return ValidateShaderSourceReferences(context.shaderDir, context.registeredShaders);
  }
};

// The artifact rules: two wrap the variant checks of ContractChecks.cpp, one the inline
// expansion policy of InlineExpansion.cpp.

class ClipContractRule : public ArtifactRule {
 public:
  size_t check(const ArtifactRuleContext& context) const override {
    return ValidateClipContractFields(*context.variants);
  }
};

class ReflectionContractRule : public ArtifactRule {
 public:
  size_t check(const ArtifactRuleContext& context) const override {
    return ValidateReflectionContracts(*context.variants);
  }
};

class InlineExpansionRule : public ArtifactRule {
 public:
  size_t check(const ArtifactRuleContext& context) const override {
    return ValidateInlineExpansion(*context.inlineExpansions);
  }
};

}  // namespace

std::vector<std::unique_ptr<SourceRule>> MakeSourceRules() {
  std::vector<std::unique_ptr<SourceRule>> rules;
  rules.push_back(std::make_unique<ChainOpCodesRule>());
  rules.push_back(std::make_unique<PointwiseOpCodesRule>());
  rules.push_back(std::make_unique<SlotCapacitiesRule>());
  rules.push_back(std::make_unique<SourceReferencesRule>());
  return rules;
}

std::vector<std::unique_ptr<ArtifactRule>> MakeArtifactRules() {
  std::vector<std::unique_ptr<ArtifactRule>> rules;
  rules.push_back(std::make_unique<ClipContractRule>());
  rules.push_back(std::make_unique<ReflectionContractRule>());
  rules.push_back(std::make_unique<InlineExpansionRule>());
  return rules;
}

}  // namespace tgfx
