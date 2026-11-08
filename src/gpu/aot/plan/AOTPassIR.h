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

#pragma once

#include <vector>
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/ir/EffectGraph.h"

namespace tgfx {

// The pass cap of a cut plan: every pass but the last is one materialization edge, so this bounds
// both the intermediate memory and the planning work; a shape needing more passes is refused.
inline constexpr size_t MaxCutPlanPasses = 8;

/**
 * One kernel invocation of a plan. A plan with several passes is a linear chain split into chain
 * passes, each fitting one chain pass: every pass but the last renders into an intermediate RGBA8
 * texture (materializesOutput), and each later pass reads its predecessor's texture in device space
 * as its source (dependencies holds that predecessor's index), so its nodes are operators only.
 *
 * The cut form instead replaces over-budget blend operand subtrees one cut at a time: each pass is a
 * self-contained fused chain pass over its own original-graph nodes, reading the earlier cuts as
 * texture leaves — materializedInputs[i] names the original-graph node that dependencies[i]'s output
 * texture replaces. The passes are in topological order and the last one is the terminal draw (the
 * only pass that runs on the destination). A pass's resource profile is resolved by its segment
 * build; a plan-time fit check runs the same build over placeholder intermediate leaves.
 */
struct PassIR {
  std::vector<AOTNodeID> nodes = {};
  std::vector<uint32_t> dependencies = {};
  AOTNodeID output = AOTNodeID::Invalid();
  bool materializesOutput = false;
  AOTResourceProfile resourceProfile = AOTResourceProfile::Automatic;
  std::vector<AOTNodeID> materializedInputs = {};
};

struct Plan {
  std::vector<PassIR> passes = {};
  AOTNodeID output = AOTNodeID::Invalid();
};

}  // namespace tgfx
