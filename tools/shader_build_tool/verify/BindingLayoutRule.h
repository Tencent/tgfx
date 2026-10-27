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

#include "ContractRules.h"

namespace tgfx {

/// The binding-layout rule: every produced variant must declare its resources exactly where
/// the runtime pipeline expects them. The WGSL of the WebGPU variants is scanned for its
/// @group/@binding qualifiers, the SPIR-V of the Vulkan variants for its descriptor set and
/// binding decorations, and both are checked against ShaderBindingLayout.h — the same values
/// the runtime's three-set pipeline layout is built from. A mismatch is what the layout drift
/// of the pre-contract era was: the runtime binds a group the shader never declared, and every
/// draw of that variant fails at load.
class BindingLayoutRule : public ArtifactRule {
 public:
  size_t check(const ArtifactRuleContext& context) const override;
};

}  // namespace tgfx
