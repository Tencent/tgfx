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

#include <cstdint>
#include <optional>
#include <set>
#include <utility>
#include <vector>
#include "gpu/shaders/PrecompiledShader.h"

namespace tgfx {

/// The variant plan of one shader family: what the build must emit for it.
///
/// The planner is the single enumeration entry of the tool — the build loop, --audit and
/// --report-only all ask it, so the three paths can never disagree about what a build
/// contains:
///
///   ShaderRegistry ─▶ VariantPlanner (this layer)
///                        ├ the Compose-reachable set, in its fixed (sorted) order
///                        └ IsBuildablePermutation validation
///                     ─▶ every consumer of variants
///
/// The reachable set comes from the family's Compose functions (the single source of truth the
/// runtime matcher uses too), so a build can never contain a variant the matcher cannot
/// request, and vice versa.
struct ShaderPlan {
  /// The (vert, frag) index pairs the family's Compose functions can reach, in sorted order —
  /// the order every consumer of the plan sees, so the bundle content is reproducible. Null
  /// when the family has not been migrated to the Compose pattern.
  std::optional<std::set<std::pair<uint32_t, uint32_t>>> variants;

  /// The reachable pairs that are not buildable: an index outside its domain, or mirrored
  /// dimensions in disagreement. Such a variant would compile against a broken
  /// vertex/fragment interface, so a consumer fails the family instead of emitting it.
  std::vector<std::pair<uint32_t, uint32_t>> unbuildable;
};

class VariantPlanner {
 public:
  /// Plans one family: enumerates its Compose-reachable set and validates every pair against
  /// the declared domains.
  static ShaderPlan PlanShader(const PrecompiledShaderInfo& info);
};

}  // namespace tgfx
