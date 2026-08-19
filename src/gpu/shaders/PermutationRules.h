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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tgfx {

/**
 * Input contract of the RoundStrokeRectFillShader matcher rule. Only predicates that shape
 * dimension values belong here; whole-draw rejections (wrong geometry processor kind,
 * perspective UV) stay in the runtime Extract step and never affect the reachable set.
 */
struct RoundStrokeRectInputs {
  bool isCoverageAA = false;
  bool hasCommonColor = false;
  bool hasUVMatrix = false;
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Input contract of the MaskFillShader matcher rule. The mask TextureEffect shape checks
 * (alpha-only, no subset, no RGBAAA, no YUV) are whole-draw rejections and stay in Extract.
 */
struct MaskFillInputs {
  int xpType = -1;  // -1 = no representable XferProcessor.
};

/**
 * Dimension values a matcher rule produces for both stages, before domain encoding.
 */
struct RuleComposedValues {
  std::vector<int> vertValues;
  std::vector<int> fragValues;
};

/**
 * Pure mapping from rule inputs to dimension values; unsupported combinations return nullopt.
 * This is the single source of truth for what the rule can produce: the runtime matcher feeds it
 * real inputs, while the build tool feeds it every input combination, so the compiled variant
 * list and the matcher's reachable set can never drift apart.
 */
std::optional<RuleComposedValues> ComposeRoundStrokeRect(const RoundStrokeRectInputs& inputs);

/**
 * Pure mapping for the MaskFillShader rule; see ComposeRoundStrokeRect for the sharing contract.
 */
std::optional<RuleComposedValues> ComposeMaskFill(const MaskFillInputs& inputs);

/**
 * Returns every (vertIndex, fragIndex) pair the RoundStrokeRect rule can ever produce, by
 * enumerating the full input lattice through ComposeRoundStrokeRect.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateRoundStrokeRectReachable();

/**
 * Returns every (vertIndex, fragIndex) pair the MaskFill rule can ever produce.
 */
std::set<std::pair<uint32_t, uint32_t>> EnumerateMaskFillReachable();

/**
 * Returns the reachable permutation set for a shader whose matcher rule has been migrated to the
 * Compose pattern, or nullopt when the rule has not been migrated yet.
 */
std::optional<std::set<std::pair<uint32_t, uint32_t>>> EnumerateReachablePermutations(
    const std::string& shaderName);

}  // namespace tgfx
