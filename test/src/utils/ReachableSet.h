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
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "gpu/shaders/Families.h"
#include "gpu/shaders/FamilyDomains.h"

namespace tgfx {

/// The reachable variant set of a declared family: every enumerated variant's (ordinal, ordinal)
/// pair, or nullopt for a name the table does not hold. This lived in the production header until
/// the registry's enumeration half lost its last runtime caller; the tests keep using it as the
/// reference set a bundle must cover, so it lives beside them.
inline std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName) {
  // A plain table scan, not FamilyIndexOf: the callers ask about retired and unknown names and
  // expect nullopt, while FamilyIndexOf asserts on an absent name by design.
  for (size_t index = 0; index < FamilyCount(); ++index) {
    if (shaderName != Families()[index].name) {
      continue;
    }
    std::set<std::pair<uint32_t, uint32_t>> pairs = {};
    for (const auto& variant : EnumerateVariants(Families()[index], static_cast<uint16_t>(index))) {
      pairs.insert({variant.ordinal, variant.ordinal});
    }
    return pairs;
  }
  return std::nullopt;
}

}  // namespace tgfx
