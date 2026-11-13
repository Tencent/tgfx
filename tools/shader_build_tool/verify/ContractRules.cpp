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

#include "ContractRules.h"
#include "BindingLayoutRule.h"
#include "ContractChecks.h"

namespace tgfx {

namespace {
;

class PointwiseDispatchRule : public SourceRule {
 public:
  size_t check(const SourceRuleContext& context, Diagnostics* diagnostics) const override {
    return ValidatePointwiseDispatch(context.shaderDir, diagnostics);
  }
};

class SpecReaderRule : public SourceRule {
 public:
  size_t check(const SourceRuleContext& context, Diagnostics* diagnostics) const override {
    return ValidateSpecReaders(context.shaderDir, diagnostics);
  }
};

class ReflectionContractRule : public ArtifactRule {
 public:
  size_t check(const ArtifactRuleContext& context, Diagnostics* diagnostics) const override {
    return ValidateReflectionContracts(*context.variants, diagnostics);
  }
};

}  // namespace

std::vector<std::unique_ptr<SourceRule>> MakeSourceRules() {
  std::vector<std::unique_ptr<SourceRule>> rules;
  rules.push_back(std::make_unique<PointwiseDispatchRule>());
  rules.push_back(std::make_unique<SpecReaderRule>());
  return rules;
}

std::vector<std::unique_ptr<ArtifactRule>> MakeArtifactRules() {
  std::vector<std::unique_ptr<ArtifactRule>> rules;
  rules.push_back(std::make_unique<ReflectionContractRule>());
  rules.push_back(std::make_unique<BindingLayoutRule>());
  return rules;
}

}  // namespace tgfx
