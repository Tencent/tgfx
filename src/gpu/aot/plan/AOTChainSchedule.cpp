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
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/ir/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/plan/AOTChainBuilder.h"
#include "gpu/aot/plan/AOTChainInternal.h"
#include "gpu/aot/plan/AOTChainRegisterAllocator.h"
#include "gpu/aot/plan/AOTShapeRules.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

// Stage one: merges the color and coverage node spaces, and admits the pass itself — non-empty
// with a valid output, every node listed exactly once, and a chain kernel when the caller asked
// for that check.
bool MergeNodeSpaces(AOTChainContext* ctx) {
  const auto& pass = *ctx->pass;
  if (ctx->layout == nullptr || pass.nodes.empty() || !pass.output.isValid()) {
    return false;
  }
  // Combined node space: color-graph nodes occupy [0, colorCount), coverage-graph nodes follow.
  ctx->colorCount = ctx->graph->nodeCount();
  ctx->covCount = ctx->coverageGraph != nullptr ? ctx->coverageGraph->nodeCount() : 0;
  if (ctx->coverageGraph != nullptr && ctx->coverageRoot.isValid() &&
      ctx->coverageRoot.index() >= ctx->covCount) {
    return false;
  }
  ctx->nodes = {};
  ctx->nodes.reserve(ctx->colorCount + ctx->covCount);
  for (size_t index = 0; index < ctx->colorCount; ++index) {
    auto* node = ctx->graph->nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return false;
    }
    ctx->nodes.push_back(node);
  }
  for (size_t index = 0; index < ctx->covCount; ++index) {
    auto* node = ctx->coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return false;
    }
    ctx->nodes.push_back(node);
  }
  // Every pass node must exist and appear once: a duplicate would encode the same node twice and
  // leave the first copy's consumers reading a stale register.
  ctx->inPass.assign(ctx->colorCount, false);
  for (auto nodeID : pass.nodes) {
    if (nodeID.index() >= ctx->colorCount || ctx->inPass[nodeID.index()]) {
      return false;
    }
    ctx->inPass[nodeID.index()] = true;
  }
  return pass.output.index() < ctx->colorCount && ctx->inPass[pass.output.index()];
}

// A texture whose input is a computed node (not one of the designators): its sampling needs no
// computed value, but its modulation must follow the producer, so the node occupies a raw
// sampling slot in the leading block plus a TEX_MODULATE slot in the op phase.
static bool HasComputedTextureInput(const AOTChainContext* ctx, size_t combined) {
  auto* node = ctx->nodes[combined];
  if (node->kind != AOTEffectKind::TextureSource || node->inputs.empty()) {
    return false;
  }
  const size_t inputBase = combined < ctx->colorCount ? 0 : ctx->colorCount;
  const size_t inputIndex = inputBase + node->inputs[0].index();
  if (inputIndex >= ctx->nodes.size()) {
    return false;
  }
  auto* inputNode = ctx->nodes[inputIndex];
  return !IsInputDesignator(inputNode->kind);
}

static void CollectChainNode(AOTChainContext* ctx, size_t combined, bool texturePhase) {
  if (ctx->nodes[combined]->kind == AOTEffectKind::TextureSource) {
    if (texturePhase) {
      ctx->rawSlotOf[combined] = ctx->ordered.size();
      ctx->ordered.push_back(combined);
      if (!HasComputedTextureInput(ctx, combined)) {
        ctx->slotOf[combined] = ctx->rawSlotOf[combined];
      }
    } else if (ctx->slotOf[combined] == SIZE_MAX) {
      ctx->slotOf[combined] = ctx->ordered.size();
      ctx->ordered.push_back(combined);
    }
  } else if (!texturePhase) {
    ctx->slotOf[combined] = ctx->ordered.size();
    ctx->ordered.push_back(combined);
  }
}

// Stage two: texture leaves first (color pass, then coverage subtree), then the remaining nodes
// in topological order (color ops, then coverage ops). The two subtrees never reference each
// other, so this layout keeps every input ahead of its consumer. A computed-input texture
// appears in both phases: the texture phase holds its raw sampling slot, the op phase its
// TEX_MODULATE instruction (slotOf points there, so consumers reference the modulated value).
// Index 0 of the coverage graph is the GeometryCoverage unit, which never becomes a slot.
// Designator-only input nodes (white / opaque geometry inputs lowered by xfer children) stay out
// too: their value rides on the input-edge designators, not on a chain slot.
void ScheduleChainSlots(AOTChainContext* ctx) {
  ctx->slotOf.assign(ctx->nodes.size(), SIZE_MAX);
  // For a computed-input texture node, the leading raw-sampling slot's position (sampler
  // binding); the node's value for consumers is the topologically-ordered TEX_MODULATE slot
  // tracked in slotOf.
  ctx->rawSlotOf.assign(ctx->nodes.size(), SIZE_MAX);
  ctx->ordered = {};
  ctx->ordered.reserve(ctx->pass->nodes.size() + ctx->covCount);
  for (bool texturePhase : {true, false}) {
    for (auto nodeID : ctx->pass->nodes) {
      CollectChainNode(ctx, nodeID.index(), texturePhase);
    }
    for (size_t index = 1; index < ctx->covCount; ++index) {
      auto kind = ctx->nodes[ctx->colorCount + index]->kind;
      if (kind != AOTEffectKind::GeometryWhiteInput &&
          kind != AOTEffectKind::GeometryColorOpaqueInput) {
        CollectChainNode(ctx, ctx->colorCount + index, texturePhase);
      }
    }
  }
}

}  // namespace tgfx
