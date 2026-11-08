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
#include <string>
#include "ContractRules.h"

namespace tgfx {

/// What a stored direct-GLSL text contains that its dialect must not: reason names the leak
/// ("surviving set", "surviving binding", "dialect header", "surviving conditional", "subpass
/// syntax") or is null for a clean text, and offset is the position of the surviving qualifier
/// inside text (only meaningful for the two qualifier reasons).
struct DirectFormLeak {
  const char* reason = nullptr;
  size_t offset = 0;
};

/// Scans one stored direct-GLSL text — already preprocessed, so everything left is live for the
/// variant — for anything that must not survive the dialect rewrite: a set/binding layout
/// qualifier (preceded by '(' or ',', the layout-qualifier context), the wrong dialect header,
/// a surviving conditional directive, or Vulkan-only subpass syntax.
DirectFormLeak FindDirectFormLeak(const std::string& text, bool esForm);

/// The binding-layout rule: every produced variant must declare its resources exactly where
/// the runtime pipeline expects them. The WGSL of the WebGPU variants is scanned for its
/// @group/@binding qualifiers, the SPIR-V of the Vulkan variants for its descriptor set and
/// binding decorations, and both are checked against ShaderBindingLayout.h — the same values
/// the runtime's three-set pipeline layout is built from. A mismatch is what the layout drift
/// of the pre-contract era was: the runtime binds a group the shader never declared, and every
/// draw of that variant fails at load. The HLSL of the D3D12 variants is scanned for its
/// register(bN)/register(tN)/register(sN) qualifiers, checked against the same binding points the
/// D3D12 root signature builds its CBV and descriptor tables from.
class BindingLayoutRule : public ArtifactRule {
 public:
  size_t check(const ArtifactRuleContext& context, Diagnostics* diagnostics) const override;
};

}  // namespace tgfx
