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

// Stage four: the pass's resource demand picks the final profile, the family's cross-slot
// constraints close over it, and the pass output's slot becomes the chain's root.
bool ResolveFinalChainProfile(AOTChainContext* ctx) {
  auto* layout = ctx->layout;
  // The pass kernel and its declared profile describe the color pass alone; the draw's coverage
  // may only raise the final profile (a coverage gradient or a third tiled leaf moves a Chain pass
  // onto the TiledGraph family, whose budgets are a superset for those fields).
  AOTResourceDemand colorDemand = {};
  for (auto nodeID : ctx->pass->nodes) {
    const auto* node = ctx->nodes[nodeID.index()];
    auto demand = NodeContractOf(node->kind).demand;
    if (demand != nullptr) {
      demand(node, &colorDemand);
    }
  }
  colorDemand.noiseSources = layout->noise != nullptr ? 1 : 0;
  colorDemand.leafBindings += colorDemand.noiseSources * 2;
  auto declaredProfile = ctx->pass->resourceProfile;
  if (declaredProfile == AOTResourceProfile::Automatic) {
    declaredProfile = SelectAOTResourceProfile(colorDemand);
  }
  if (!FitsAOTResourceProfile(colorDemand, declaredProfile)) {
    return false;
  }
  ctx->finalProfile = declaredProfile;
  if (ctx->finalProfile == AOTResourceProfile::Chain &&
      (ctx->hasGradient ||
       layout->program.tiledLeaves.size() > ChainResourceCapacity.tiledRecipes)) {
    if (ctx->coverageGraph == nullptr) {
      return false;
    }
    ctx->finalProfile = AOTResourceProfile::TiledGraph;
  }
  const size_t leafCount = layout->leafNodes.size();
  if ((layout->lutGradient != nullptr && leafCount >= 2) ||
      (layout->noise != nullptr &&
       (ctx->hasGradient || leafCount > 2 || ctx->coverageLeafFromUVCoord))) {
    return false;
  }
  ctx->rootIndex = ctx->slotOf[ctx->pass->output.index()];
  return ctx->rootIndex != SIZE_MAX;
}

// Stage five: resolves the coverage subtree's root register, and validates the subtree's shape.
// Accepted roots: a single blend consuming the unit input (the xfer-dst coverage family), a bare
// texture or gradient consuming the unit alpha, a constant (the inverted shader mask filter whose
// shader yields no processor: ConstColor in any input mode, the same pmApplyConstColor math as
// the runtime emission), or an analytic coverage node (RectCoverage / RRectCoverage) closing a
// chain that starts at the unit. The unit may otherwise feed only texture leaves (their input is
// unused — blend operands sample raw), the gradient (a blend child, so it reads the opaque -4
// designator), and analytic coverage nodes. Analytic clip slots may multiply a blend-rooted
// subtree: the scalar coverage commutes with the pointwise blend result, so the product matches
// the runtime chain order (verified differentially). Two-child blends keep the plain route rather
// than risking a wrong value when the GP coverage is below 1.0.
bool ResolveCoverageRoot(AOTChainContext* ctx) {
  // A coverage graph without a valid root carries only analytic clip links: no coverage subtree
  // exists (the links ride the clip channel), so there is nothing to resolve here.
  if (ctx->coverageGraph == nullptr || !ctx->coverageRoot.isValid()) {
    return true;
  }
  const size_t covRootCombined = ctx->colorCount + ctx->coverageRoot.index();
  auto* covRootNode = ctx->nodes[covRootCombined];
  const bool rootIsBlend = covRootNode->kind == AOTEffectKind::Blend;
  const bool rootIsTexture = covRootNode->kind == AOTEffectKind::TextureSource;
  const bool rootIsGradient = covRootNode->kind == AOTEffectKind::GradientSource;
  const bool rootIsConstant = covRootNode->kind == AOTEffectKind::ConstColor;
  const bool rootIsAnalytic = covRootNode->kind == AOTEffectKind::RectCoverage ||
                              covRootNode->kind == AOTEffectKind::RRectCoverage;
  if (!rootIsBlend && !rootIsTexture && !rootIsGradient && !rootIsConstant && !rootIsAnalytic) {
    return false;
  }
  int coverageBlendCount = 0;
  bool rootConsumesUnit = false;
  for (size_t j = 1; j < ctx->covCount; ++j) {
    auto* node = ctx->nodes[ctx->colorCount + j];
    if (node->kind == AOTEffectKind::Blend) {
      ++coverageBlendCount;
    }
    for (auto input : node->inputs) {
      if (input.index() != 0) {
        continue;
      }
      if (ctx->colorCount + j == covRootCombined) {
        rootConsumesUnit = true;
        continue;
      }
      if (node->kind != AOTEffectKind::TextureSource &&
          node->kind != AOTEffectKind::GradientSource &&
          node->kind != AOTEffectKind::RectCoverage && node->kind != AOTEffectKind::RRectCoverage) {
        return false;
      }
    }
  }
  // An analytic root consumes the previous analytic node rather than the unit, but the chain
  // still transmits the unit coverage transitively, so only analytic roots may omit a direct
  // unit input (the blend-count rule already rejects blends under an analytic root).
  if (coverageBlendCount != (rootIsBlend ? 1 : 0) || (!rootConsumesUnit && !rootIsAnalytic)) {
    return false;
  }
  auto covSlot = ctx->slotOf[covRootCombined];
  if (covSlot == SIZE_MAX) {
    return false;
  }
  ctx->coverageRootSlot = static_cast<int>(covSlot);
  if (!ctx->coverageLeafFromUVCoord) {
    return true;
  }
  // The atlas glyph mask is TRUE coverage in the runtime composite (the dedicated MaskFill
  // shader carries it as coverage — see AtlasTextNonSrcOverBlendKeepsDstProbe), unlike a
  // MaskFilter's source-modulating mask, whose subtree keeps the CoverageRootIndex source
  // route. Route the atlas subtree's root through the clip-coverage register so it rides the
  // XP's coverage input with the unpremultiplied source. A draw clip then chains on top of
  // the glyph (glyph * clip), so the glyph register is the clip chain's innermost input. An
  // early-fold clip suffix sits in the color block, ahead of the glyph slots, and cannot
  // read the glyph register.
  if (ctx->clipCoverageSlot >= 0) {
    return false;
  }
  ctx->clipCoverageSlot = ctx->coverageRootSlot;
  ctx->clipChainInput = ctx->coverageRootSlot;
  ctx->coverageRootSlot = -1;
  return true;
}

// Stage six: routes the analytic coverage terms' links onto the clip-coverage channel. They
// never multiply the color root: each link chains from ctx->clipChainInput (the scalar unit one,
// or the glyph register the atlas path set) so their product is a pure clip coverage value the
// kernel reads through ClipCoverageRegister and composites as the XP's coverage input on top of
// the GP coverage — folding them into the source was only correct for SrcOver-class blending, and
// chaining them from the coverage unit (-3) squared the GP coverage. The links were encoded as
// ordinary slots by stage three; this only rewires their inputs, in chain order, and the color
// root stays the pre-clip result.
void RouteAnalyticChains(AOTChainContext* ctx) {
  if (ctx->clipLinks == nullptr) {
    return;
  }
  for (auto link : *ctx->clipLinks) {
    auto slot = ctx->slotOf[ctx->colorCount + link.index()];
    if (slot == SIZE_MAX) {
      continue;
    }
    ctx->slots[slot].in0 = ctx->clipChainInput;
    ctx->clipChainInput = static_cast<int>(slot);
    ctx->clipCoverageSlot = static_cast<int>(slot);
  }
}

// Stage seven: register allocation. The slot wiring built above uses instruction ordinals; this
// pass rewrites every reference into the kernel's recycled chainResults registers, so the
// per-profile instruction budget (16 for Chain and gradient, 32 for noise) and the live-result
// budget (MaxChainRegisters) are independent. A DAG exceeding the register budget is refused here.
bool AssignChainRegisters(AOTChainContext* ctx) {
  if (ctx->slots.size() > MaxChainSlots) {
    return false;
  }
  std::vector<std::vector<int>> instructionInputs(ctx->slots.size());
  for (size_t index = 0; index < ctx->slots.size(); ++index) {
    for (int input : {ctx->slots[index].in0, ctx->slots[index].in1}) {
      if (input >= 0) {
        instructionInputs[index].push_back(input);
      }
    }
  }
  ctx->assignment = {};
  if (!AllocateChainRegisters(instructionInputs, static_cast<int>(ctx->rootIndex),
                              ctx->coverageRootSlot, ctx->clipCoverageSlot, MaxChainRegisters,
                              &ctx->assignment)) {
    return false;
  }
  for (size_t index = 0; index < ctx->slots.size(); ++index) {
    if (ctx->slots[index].in0 >= 0) {
      ctx->slots[index].in0 =
          ctx->assignment.outRegister[static_cast<size_t>(ctx->slots[index].in0)];
    }
    if (ctx->slots[index].in1 >= 0) {
      ctx->slots[index].in1 =
          ctx->assignment.outRegister[static_cast<size_t>(ctx->slots[index].in1)];
    }
    ctx->slots[index].outRegister = ctx->assignment.outRegister[index];
  }
  return true;
}

// Stage eight: the slot rules and the final profile's capacities decide last, on the exact
// instruction array the compile hands to AOTChainBuilder::ValidateSlots.
bool FinalizeChainLayout(AOTChainContext* ctx) {
  auto* layout = ctx->layout;
  std::vector<int> tiledLeafIndices = {};
  for (const auto& tiled : layout->program.tiledLeaves) {
    tiledLeafIndices.push_back(tiled.leafIndex);
  }
  if (ctx->assignment.rootRegister < 0 ||
      !AOTChainBuilder::ValidateSlots(ctx->slots, static_cast<size_t>(ctx->assignment.rootRegister),
                                      ctx->assignment.coverageRootRegister,
                                      ctx->assignment.clipCoverageRegister,
                                      layout->leafNodes.size(), layout->lutGradient != nullptr,
                                      tiledLeafIndices, &ctx->finalProfile)) {
    return false;
  }
  layout->program.slots = std::move(ctx->slots);
  layout->program.rootRegister = static_cast<size_t>(ctx->assignment.rootRegister);
  layout->program.coverageRootRegister = ctx->assignment.coverageRootRegister;
  layout->program.clipCoverageRegister = ctx->assignment.clipCoverageRegister;
  layout->program.profile = ctx->finalProfile;
  return true;
}

}  // namespace tgfx
