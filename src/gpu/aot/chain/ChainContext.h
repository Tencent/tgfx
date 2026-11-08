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

#include <vector>
#include "gpu/aot/AOTResourceProfile.h"
#include "gpu/aot/chain/AOTChainRegisterAllocator.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/ir/Program.h"

namespace tgfx {

struct AOTChainLayout;
struct PassIR;

/**
 * The shared state of the chain-layout pipeline (see AOTChainBuilder's PlanChainLayout): each
 * stage fills part of it and reads what the earlier stages produced, so no shape rule is ever
 * re-derived. Opaque outside the AOT pipeline — only the pipeline stages and the per-kind
 * contract encoders (declared below and registered by the node contract table) touch it.
 */
struct AOTChainContext {
  // Pipeline inputs.
  const AOTEffectGraph* graph = nullptr;
  const PassIR* pass = nullptr;
  // The analytic coverage terms' links, as node IDs in the coverage graph, in chain order; they
  // are encoded as ordinary slots and rewired onto the clip-coverage channel by stage seven.
  const std::vector<AOTNodeID>* clipLinks = nullptr;
  const AOTEffectGraph* coverageGraph = nullptr;
  AOTNodeID coverageRoot = AOTNodeID();
  bool coverageLeafFromUVCoord = false;
  AOTChainLayout* layout = nullptr;
  // Stage results, consumed by the later stages.
  std::vector<const AOTEffectNode*> nodes = {};
  size_t colorCount = 0;
  size_t covCount = 0;
  std::vector<bool> inPass = {};
  std::vector<size_t> slotOf = {};
  std::vector<size_t> rawSlotOf = {};
  std::vector<size_t> ordered = {};
  std::vector<AOTChainSlot> slots = {};
  bool hasGradient = false;
  AOTResourceProfile finalProfile = AOTResourceProfile::Automatic;
  size_t rootIndex = 0;
  int coverageRootSlot = -1;
  int clipCoverageSlot = -1;
  int clipChainInput = -4;
  AOTChainRegisterAssignment assignment = {};
};

}  // namespace tgfx
