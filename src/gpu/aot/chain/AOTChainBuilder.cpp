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

#include "gpu/aot/chain/AOTChainBuilder.h"
#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/chain/AOTChainRegisterAllocator.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/chain/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/plan/AOTPassIR.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/shaders/modules/YUVChainTail.h"

namespace tgfx {

namespace {

// Whether a value is a schedulable chain instruction: inside the op vocabulary's bounds and not
// the inactive-slot marker. The kernel's evaluation switch has no default branch, so an
// out-of-enum opcode must be rejected here rather than silently misread.
bool IsSchedulableChainOp(AOTChainOp op) {
  return op != AOTChainOp::None && static_cast<int>(op) < ChainISA::OpCount;
}

}  // namespace

bool AOTChainBuilder::IsChainCompatibleTiledMode(TiledTextureShaderMode mode) {
  return mode == TiledTextureShaderMode::None || mode == TiledTextureShaderMode::Clamp ||
         mode == TiledTextureShaderMode::RepeatNearestNone ||
         mode == TiledTextureShaderMode::RepeatLinearNone ||
         mode == TiledTextureShaderMode::MirrorRepeat ||
         mode == TiledTextureShaderMode::ClampToBorderNearest ||
         mode == TiledTextureShaderMode::ClampToBorderLinear;
}

bool AOTChainBuilder::FitsChainPlan(const AOTEffectGraph& graph, const PassIR& pass,
                                    AOTResourceProfile* outProfile) {
  // A fit probe, not an admission: it runs the planner alone (no bindings, no coverage), which is
  // what the decomposer needs to decide whether a cut fits before any program exists.
  AOTChainLayout layout = {};
  if (!PlanChainLayout(graph, pass, nullptr, AOTNodeID::Invalid(), false, {}, &layout)) {
    return false;
  }
  if (outProfile != nullptr) {
    *outProfile = layout.program.profile;
  }
  return true;
}

bool AOTChainBuilder::Compile(const AOTEffectGraph& graph, const PassIR& pass,
                              const std::vector<AOTCoverageTerm>* coverageTerms, AOTProgram* out) {
  if (out == nullptr) {
    return false;
  }
  AOTChainLayout layout = {};
  if (!CompileChainLayout(graph, pass, coverageTerms, &layout)) {
    return false;
  }
  *out = std::move(layout.program);
  return true;
}

AOTTextureParameters AOTChainBuilder::IntermediateSource(std::shared_ptr<TextureProxy> textureProxy,
                                                         const Matrix& uvMatrix) {
  AOTTextureParameters parameters = {};
  parameters.textureProxy = std::move(textureProxy);
  parameters.samplingKind = AOTTextureSamplingKind::Device;
  parameters.uvMatrix = uvMatrix;
  return parameters;
}

// Plans one fused chain pass over the given reachable node set, rooted at subtreeRoot: the shape
// whitelist (which node kinds a single fused pass may carry), the pass node list (designators
// excluded) and the chain builder's own profile resolution, so the planner, CanExecute and
// construction cannot count differently. Leaf parameters and budgets belong to the chain builder's
// planner, consulted through ResolveChainProfile.
static bool PlanReachableSubtree(const AOTEffectGraph& graph, AOTNodeID subtreeRoot,
                                 const std::vector<bool>& reachable, PassIR* pass) {
  if (pass == nullptr) {
    return false;
  }
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    if (!reachable[index]) {
      continue;
    }
    auto node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      return false;
    }
    if (!TraitsOf(node->kind).chainFusable) {
      return false;
    }
  }
  *pass = {};
  pass->output = subtreeRoot;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!reachable[index] || TraitsOf(node->kind).isInputDesignator()) {
      continue;
    }
    pass->nodes.push_back(AOTNodeID(index));
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!AOTChainBuilder::FitsChainPlan(graph, *pass, &profile)) {
    return false;
  }
  pass->resourceProfile = profile;
  return true;
}

// Planner for a pointwise DAG whose only leaves are texture sources / const colors and whose
// interior nodes are pure pointwise or blend ops. Such a DAG evaluates in a single fused pass (the
// PointwiseChain kernel) with no intermediate materialization. Resolved plain, tiled and
// device-space texture leaves are supported; non-pointwise nodes require a different planner.
// A noise-free graph admits its whole node space (a dead node would still encode as a dead slot);
// a noise graph keeps to the root-reachable set, which is all the noise family's budgets cover.
bool AOTChainBuilder::PlanFusedPass(const AOTEffectGraph& graph, PassIR* pass) {
  if (pass == nullptr || graph.nodeCount() < 2) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }
  bool containsNoise = false;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    containsNoise |= graph.nodeAt(AOTNodeID(index))->kind == AOTEffectKind::PerlinNoiseSource;
  }
  std::vector<bool> reachable(graph.nodeCount(), !containsNoise);
  if (containsNoise && !CollectReachableNodes(graph, graph.root(), &reachable)) {
    return false;
  }
  return PlanReachableSubtree(graph, graph.root(), reachable, pass);
}

bool AOTChainBuilder::CompileGraph(const AOTEffectGraph& graph,
                                   const std::vector<AOTCoverageTerm>* coverageTerms,
                                   AOTProgram* out) {
  if (out == nullptr) {
    return false;
  }
  PassIR pass = {};
  PassIR fused = {};
  if (!PlanFusedPass(graph, &fused)) {
    return false;
  }
  return CompileChainProgramLayout(graph, fused, coverageTerms, out);
}

bool AOTChainBuilder::CompileSegment(const DrawIR& segmentIR, AOTProgram* out) {
  if (out == nullptr) {
    return false;
  }
  PassIR pass = {};
  if (!DeriveWholeGraphPass(segmentIR.color, &pass)) {
    return false;
  }
  return CompileChainProgramLayout(segmentIR.color, pass, &segmentIR.coverage, out);
}

bool AOTChainBuilder::CollectTextureTail(const AOTEffectGraph& graph, AOTTextureTailShape* shape) {
  if (shape == nullptr || !graph.root().isValid() || graph.nodeCount() < 2) {
    return false;
  }
  std::vector<AOTNodeID> reverseOps = {};
  auto nodeID = graph.root();
  auto* node = graph.nodeAt(nodeID);
  while (node != nullptr && IsUnaryPointwiseKind(node->kind)) {
    if (node->inputs.size() != 1) {
      return false;
    }
    // The slot encoding must be able to express the operator — the same record the chain
    // builds from the node.
    AOTChainSlot slot = {};
    if (!BuildPointwiseSlot(graph, node, &slot)) {
      return false;
    }
    reverseOps.push_back(nodeID);
    nodeID = node->inputs[0];
    node = graph.nodeAt(nodeID);
  }
  // The walk's terminal: a texture leaf fed by the geometry color (node 0).
  if (node == nullptr || node->kind != AOTEffectKind::TextureSource || node->inputs.size() != 1 ||
      node->inputs[0] != AOTNodeID(0)) {
    return false;
  }
  auto* leaf = std::get_if<AOTTextureParameters>(&node->parameters);
  auto* geometry = graph.nodeAt(AOTNodeID(0));
  if (leaf == nullptr || geometry == nullptr || geometry->kind != AOTEffectKind::GeometryColor) {
    return false;
  }
  // Linearity: the geometry color, the leaf and the operators are the whole graph — an extra
  // node (a second source, an unaccounted branch) makes the shape unservable.
  if (graph.nodeCount() != 2 + reverseOps.size()) {
    return false;
  }
  shape->leaf = leaf;
  shape->ops.assign(reverseOps.rbegin(), reverseOps.rend());
  return true;
}

bool AOTChainBuilder::CollectYUVTail(const AOTEffectGraph& graph, AOTTextureTailShape* shape) {
  if (shape == nullptr) {
    return false;
  }
  AOTTextureTailShape tail = {};
  if (!CollectTextureTail(graph, &tail)) {
    return false;
  }
  size_t colorSpaceOps = 0;
  for (auto opID : tail.ops) {
    auto* node = graph.nodeAt(opID);
    if (node->kind == AOTEffectKind::ColorSpaceXform) {
      auto parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
      if (parameters == nullptr || !ColorSpaceXformStepsSupported(parameters->steps.get())) {
        return false;
      }
      // The kernel's color-space parameters are one shared block (the interpreter's contract):
      // at most one transform per tail.
      if (++colorSpaceOps > 1) {
        return false;
      }
    }
  }
  // The multi-plane leaf: a Plain-sampled YUV source (the RGBAAA layout stays acceptable — the
  // kernel serves it through HasRgbaaa / AlphaStart).
  if (!tail.leaf->isYUV || tail.leaf->samplingKind != AOTTextureSamplingKind::Plain) {
    return false;
  }
  if (tail.ops.size() > YUVChainTailSlotCapacity) {
    return false;
  }
  shape->leaf = tail.leaf;
  shape->ops = std::move(tail.ops);
  return true;
}

static bool CompileYUVProgramForInfo(const ProgramInfo* info, AOTProgram* out) {
  if (info == nullptr || out == nullptr) {
    return false;
  }
  auto* ir = info->getCarriedIR();
  if (ir == nullptr) {
    return false;
  }
  AOTChainBuilder::AOTTextureTailShape shape = {};
  if (!AOTChainBuilder::CollectYUVTail(ir->color, &shape)) {
    return false;
  }
  AOTProgram program = {};
  program.slots.reserve(shape.ops.size());
  for (auto opID : shape.ops) {
    AOTChainSlot slot = {};
    // The register fields stay unset: the tail is linear, array order is dataflow order (the
    // binder packs its own selector).
    if (!BuildPointwiseSlot(ir->color, ir->color.nodeAt(opID), &slot)) {
      return false;
    }
    program.slots.push_back(slot);
  }
  *out = std::move(program);
  return true;
}

bool AOTChainBuilder::ValidateSlots(const std::vector<AOTChainSlot>& slots, size_t rootSlot,
                                    int coverageRootSlot, int clipCoverageRegister,
                                    size_t leafCount, bool hasLUT,
                                    const std::vector<int>& tiledLeafIndices,
                                    AOTResourceProfile* profile) {
  if (profile == nullptr || slots.empty() || slots.size() > MaxChainSlots) {
    return false;
  }
  // Validation domains are separated (fail-closed): in0/in1 are INPUT selectors and
  // may carry the kernel's special negative designators (-1 geometry color ... -5 opaque
  // geometry color) or a result register; outRegister / rootSlot / coverageRootSlot /
  // clipCoverageRegister are OUTPUT registers and must name a real chainResults slot (>= 0) or
  // the explicit dead-instruction marker -1 (a raw sampling slot whose result nothing reads —
  // the computed-input texture's leading fetch). The upload packs the output as
  // (outRegister + 1) << 20, so any value below -1 would shift left into undefined behavior
  // instead of being rejected here.
  const auto validInput = [](int reg) {
    return reg >= -5 && reg < static_cast<int>(MaxChainRegisters);
  };
  const auto validOutput = [](int reg) {
    return reg >= -1 && reg < static_cast<int>(MaxChainRegisters);
  };
  if (rootSlot >= MaxChainRegisters || (coverageRootSlot >= 0 && !validOutput(coverageRootSlot)) ||
      clipCoverageRegister >= static_cast<int>(MaxChainRegisters) || leafCount > slots.size()) {
    return false;
  }
  for (const auto& slot : slots) {
    if (!validOutput(slot.outRegister) || !validInput(slot.in0) || !validInput(slot.in1)) {
      return false;
    }
    // Opcode validity comes from the instruction table's own extent (the kernel's evaluation
    // switch has no default branch, so an out-of-enum opcode must be rejected here rather than
    // silently misinterpreted).
    if (!IsSchedulableChainOp(slot.op)) {
      return false;
    }
    // Selector sub-field domains: the upload packs these ints straight into the selector's bit
    // fields, so an out-of-domain value does not merely misbehave — it bleeds into neighboring
    // bits (the result register rides bits 20-24, the rrect/tex-source ordinals bits 16-19) and
    // can index chainResults/chainLeafTex out of bounds in the kernel. The owned API is public
    // surface, so fail closed here instead of trusting the builder.
    if (slot.op == AOTChainOp::ConstColor &&
        (slot.constColor.inputMode < 0 || slot.constColor.inputMode > 2)) {
      return false;
    }
    if (slot.op == AOTChainOp::Blend && (slot.blend.blendMode < 0 || slot.blend.blendMode > 0xFF ||
                                         slot.blend.childType < 0 || slot.blend.childType > 2)) {
      return false;
    }
    if (slot.op == AOTChainOp::Texture &&
        ((slot.textureModulate | slot.textureAlphaOnly | slot.textureModulateGeometryRGB |
          slot.textureModulateFullInput | slot.textureModulateUnit) &
         ~1) != 0) {
      return false;
    }
    if (slot.op == AOTChainOp::TexModulate &&
        (slot.texModulateSourceSlot < 0 || slot.texModulateSourceSlot > 3 ||
         (slot.texModulateAlphaOnly & ~1) != 0)) {
      return false;
    }
    // Required-input checks: the kernel resolves an unmapped input (-2) to vec4(0.0) silently, so
    // an operator that actually consumes its input must not leave it unmapped. Texture slots
    // carry flags in in0/in1 rather than register indices, and Texture is exempt.
    if (slot.op != AOTChainOp::Texture && slot.op != AOTChainOp::PerlinNoise && slot.in0 == -2) {
      return false;
    }
    if ((slot.op == AOTChainOp::Blend || slot.op == AOTChainOp::MulAlpha) && slot.in1 == -2) {
      return false;
    }
  }
  for (size_t index = 0; index < tiledLeafIndices.size(); ++index) {
    auto leafIndex = tiledLeafIndices[index];
    if (leafIndex < 0 || static_cast<size_t>(leafIndex) >= leafCount) {
      return false;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (tiledLeafIndices[previous] == leafIndex) {
        return false;
      }
    }
  }
  // Only real DAG leaves have prefetched samples; LUT and padding children do not.
  for (size_t index = 0; index < slots.size(); ++index) {
    const auto& slot = slots[index];
    if ((slot.op == AOTChainOp::Texture) != (index < leafCount)) {
      return false;
    }
    if (slot.op == AOTChainOp::TexModulate &&
        static_cast<size_t>(slot.texModulateSourceSlot) >= leafCount) {
      return false;
    }
  }
  size_t colorSpaceSlots = 0;
  size_t gradientSlots = 0;
  size_t noiseSlots = 0;
  for (const auto& slot : slots) {
    if (slot.op == AOTChainOp::ColorSpaceXform &&
        (!ColorSpaceXformStepsSupported(slot.colorSpaceXform.steps.get()) ||
         ++colorSpaceSlots > MaxChainColorSpaceXformSlots)) {
      return false;
    }
    if (slot.op == AOTChainOp::PerlinNoise &&
        (++noiseSlots > 1 || !AOTPerlinNoiseParametersSupported(slot.noise) ||
         slot.noise.permutationsView == nullptr || slot.noise.noiseView == nullptr ||
         leafCount > 2 || hasLUT)) {
      return false;
    }
    if (slot.op == AOTChainOp::Gradient && ++gradientSlots > MaxChainGradientSlots) {
      return false;
    }
  }
  if (noiseSlots > 0 && gradientSlots > 0) {
    return false;
  }
  auto resolved = *profile;
  if (resolved == AOTResourceProfile::Automatic) {
    resolved = noiseSlots > 0      ? AOTResourceProfile::NoiseGraph
               : gradientSlots > 0 ? AOTResourceProfile::TiledGraph
                                   : AOTResourceProfile::Chain;
  }
  AOTResourceDemand demand = {};
  demand.instructions = slots.size();
  demand.leafBindings = leafCount + (hasLUT ? 1u : 0u) + noiseSlots * 2;
  demand.tiledRecipes = tiledLeafIndices.size();
  demand.noiseSources = noiseSlots;
  demand.gradients = gradientSlots;
  if (!FitsAOTResourceProfile(demand, resolved)) {
    return false;
  }
  *profile = resolved;
  return true;
}

static bool CompileChainProgramForInfo(const ProgramInfo* programInfo, AOTProgram* out) {
  if (programInfo == nullptr || out == nullptr) {
    return false;
  }
  // The draw's record-time IR is the only input: every draw lowers once at record time, so a
  // missing IR means the expression is not expressible and no second lowering could fix that.
  auto* carried = programInfo->getCarriedIR();
  if (carried == nullptr) {
    return false;
  }
  // The whole expression is the pass: the fused form is derived from the graph itself, so a draw
  // the record-time route left to this point needs no planning input beyond its IR.
  return AOTChainBuilder::CompileGraph(carried->color, &carried->coverage, out);
}

// The memoization both consumers share: the program lives on the draw's IR, the first caller that
// needs it compiles it, and every later caller — the matcher's second prepare, the writer on a
// program-cache hit — reads it back. One entry per form, one compile per draw: the chain memo never
// answers the YUV resolver's question and vice versa (the two accept different graphs).
const AOTProgram* AOTChainBuilder::ResolveChainProgram(const ProgramInfo* programInfo) {
  auto* carried = programInfo != nullptr ? programInfo->getCarriedIR() : nullptr;
  if (carried == nullptr) {
    return nullptr;
  }
  if (!carried->chainProgram.has_value()) {
    AOTProgram program = {};
    if (!CompileChainProgramForInfo(programInfo, &program)) {
      return nullptr;
    }
    carried->chainProgram = std::move(program);
  }
  return &*carried->chainProgram;
}

const AOTProgram* AOTChainBuilder::ResolveYUVProgram(const ProgramInfo* programInfo) {
  auto* carried = programInfo != nullptr ? programInfo->getCarriedIR() : nullptr;
  if (carried == nullptr) {
    return nullptr;
  }
  if (!carried->yuvProgram.has_value()) {
    auto context = programInfo->getContext();
    if (context == nullptr || context->drawingManager() == nullptr) {
      return nullptr;
    }
    AOTProgram program = {};
    if (!CompileYUVProgramForInfo(programInfo, &program)) {
      return nullptr;
    }
    carried->yuvProgram = std::move(program);
  }
  return &*carried->yuvProgram;
}

}  // namespace tgfx
