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
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/AOTEffectDecomposer.h"
#include <algorithm>
#include "gpu/AOTChainBuilder.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/AOTShapeRules.h"
#include "gpu/ProgramInfo.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"

namespace tgfx {

// Counts texture-sampling leaves across the tree. A fused kernel needs one sampler per leaf, so
// this is the sampler-budget input the audit reports.
static int CountTextureLeaves(const FragmentProcessor* processor) {
  if (processor == nullptr) {
    return 0;
  }
  auto name = processor->name();
  int count = (name == "TextureEffect" || name == "TiledTextureEffect" ||
               name == "DeviceSpaceTextureEffect")
                  ? 1
                  : 0;
  for (size_t i = 0; i < processor->numChildProcessors(); ++i) {
    count += CountTextureLeaves(processor->childProcessor(i));
  }
  return count;
}

static AOTAxisAnalysis ClassifyAxis(const std::vector<const FragmentProcessor*>& processors) {
  AOTAxisAnalysis analysis = {};
  analysis.processorCount = static_cast<int>(processors.size());
  if (processors.empty()) {
    analysis.outcome = AOTDecomposeOutcome::Trivial;
    return analysis;
  }
  for (auto processor : processors) {
    analysis.textureLeafCount += CountTextureLeaves(processor);
  }
  AOTEffectGraph graph = {};
  if (!AOTEffectDecomposer::Lower(processors, &graph, &analysis.blockingProcessor)) {
    analysis.outcome = analysis.blockingProcessor.empty() ? AOTDecomposeOutcome::UnsupportedShape
                                                          : AOTDecomposeOutcome::BlockedByLowering;
    return analysis;
  }
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Decompose(graph, &plan)) {
    analysis.outcome = AOTDecomposeOutcome::UnsupportedShape;
    return analysis;
  }
  analysis.outcome = AOTDecomposeOutcome::FusablePointwise;
  return analysis;
}

bool AOTEffectDecomposer::Lower(const std::vector<const FragmentProcessor*>& processors,
                                AOTEffectGraph* graph, std::string* blockingProcessor) {
  if (blockingProcessor != nullptr) {
    blockingProcessor->clear();
  }
  if (graph == nullptr || processors.empty()) {
    return false;
  }
  AOTNodeBuilder builder = {};
  AOTNodeID current = AOTNodeID::Invalid();
  if (!builder.addGeometryColor(&current)) {
    return false;
  }
  for (auto processor : processors) {
    if (processor == nullptr) {
      return false;
    }
    AOTNodeID next = AOTNodeID::Invalid();
    if (!processor->lowerToAOT(&builder, current, &next)) {
      auto missingLowering = builder.missingLoweringProcessor();
      if (blockingProcessor != nullptr && missingLowering != nullptr) {
        *blockingProcessor = missingLowering->name();
      }
      return false;
    }
    current = next;
  }
  AOTEffectGraph result = {};
  if (!builder.finish(current, &result)) {
    return false;
  }
  *graph = std::move(result);
  return true;
}

// The multi-pass source, the unary pointwise set, the perlin/YUV source shapes and the
// const-operand blend are shared shape rules (gpu/AOTShapeRules.h) — the same authority the
// executor's admission checks and the chain builder consume.

// Whether a multi-pass plan may start a new pass at this node. A pass boundary stores the previous
// pass's output in an RGBA8 target, rounding it to the 1/255 grid. Most operators move by at most
// that rounding, but AlphaThreshold is a step function: an input just below the threshold can round
// to a grid point just above it and flip the output from 0 to the full color. So an AlphaThreshold
// always shares a pass with the operator that produces its input.
static bool CanStartPassAt(const AOTEffectNode* node) {
  return node != nullptr && node->kind != AOTEffectKind::AlphaThreshold;
}

// Planner for a linear chain one fused pass cannot carry: GeometryColor -> plain or device-space
// TextureSource -> unary pointwise ops, with more operators than the chain kernel has instructions
// or more color-space transforms than its parameter block holds. The chain is cut into chain-kernel
// passes, each as long as fits (AOTChainBuilder::BuildLinearSegment is the fit check), every later
// pass reading its predecessor's materialized output in device space. A pass ends early when the
// next one would otherwise start at a node that may not start a pass (CanStartPassAt); the chain is
// refused when no boundary is left.
static bool DecomposeLinearMultiPass(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2 || graph.root().index() + 1 != graph.nodeCount()) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor ||
      !IsLinearMultiPassSource(graph, AOTNodeID(1))) {
    return false;
  }
  for (uint32_t index = 2; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!IsUnaryPointwiseOp(node, AOTNodeID(index - 1))) {
      return false;
    }
  }
  const auto& source = std::get<AOTTextureParameters>(graph.nodeAt(AOTNodeID(1))->parameters);
  // A later pass reads an intermediate that does not exist yet; its fit only depends on the
  // source kind, so the plan's own texture stands in for it.
  auto intermediate = AOTChainBuilder::IntermediateSource(source.textureProxy, Matrix::I());
  auto fits = [&](uint32_t firstOperator, uint32_t passEnd, bool firstPass) {
    std::vector<AOTNodeID> operators = {};
    for (auto index = firstOperator; index < passEnd; ++index) {
      operators.push_back(AOTNodeID(index));
    }
    AOTEffectGraph segment = {};
    AOTPassDescriptor segmentPass = {};
    return AOTChainBuilder::BuildLinearSegment(graph, operators, firstPass ? source : intermediate,
                                               &segment, &segmentPass, firstPass);
  };
  AOTEffectPlan result = {};
  const auto nodeCount = static_cast<uint32_t>(graph.nodeCount());
  uint32_t firstOperator = 2;
  while (true) {
    bool firstPass = result.passes.empty();
    if (firstPass && !fits(firstOperator, firstOperator, true)) {
      return false;
    }
    // One past the last node of this pass.
    auto passEnd = firstOperator;
    while (passEnd < nodeCount && fits(firstOperator, passEnd + 1, firstPass)) {
      ++passEnd;
    }
    while (passEnd < nodeCount && passEnd > firstOperator &&
           !CanStartPassAt(graph.nodeAt(AOTNodeID(passEnd)))) {
      --passEnd;
    }
    if (passEnd < nodeCount && !CanStartPassAt(graph.nodeAt(AOTNodeID(passEnd)))) {
      return false;
    }
    if (passEnd == firstOperator && !firstPass) {
      return false;
    }
    AOTPassDescriptor pass = {};
    pass.kernel = AOTKernelKind::PointwiseChain;
    if (firstPass) {
      pass.nodes.push_back(AOTNodeID(1));
    } else {
      pass.dependencies.push_back(static_cast<uint32_t>(result.passes.size() - 1));
    }
    for (auto index = firstOperator; index < passEnd; ++index) {
      pass.nodes.push_back(AOTNodeID(index));
    }
    pass.output = pass.nodes.back();
    result.passes.push_back(std::move(pass));
    if (passEnd == nodeCount) {
      break;
    }
    firstOperator = passEnd;
  }
  for (size_t index = 0; index + 1 < result.passes.size(); ++index) {
    result.passes[index].materializesOutput = true;
  }
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

// Planner for a procedural-noise source: GeometryColor -> PerlinNoiseSource -> [0..3 unary
// pointwise ops]. PerlinNoiseFillShader carries three pointwise slots (a base OpType record
// shared with a composed operator FP and two processor-owned records), so up to three operators
// fold into a single fused pass with no materialization. Besides the pure pointwise ops, a slot
// may be a ConstColor op or a Blend whose other operand is a ConstColor node (the side constant
// stays out of pass.nodes; its value is read from the blend node's inputs at build time).
// A single-child xfer lowers its constant child from an explicit opaque-white designator
// (GeometryWhiteInput, see XfermodeFragmentProcessor::lowerToAOT). The designator never becomes a
// slot, so it is skipped here exactly as the DAG planner skips it; the slot budget, not the raw
// node count, bounds the pass.
static bool DecomposePerlinNoiseChain(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2 || graph.root().index() + 1 != graph.nodeCount()) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  auto noiseParameters =
      std::get_if<AOTPerlinNoiseParameters>(&graph.nodeAt(AOTNodeID(1))->parameters);
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor ||
      !IsPerlinNoiseSource(graph, AOTNodeID(1)) || noiseParameters == nullptr ||
      noiseParameters->permutationsView == nullptr || noiseParameters->noiseView == nullptr) {
    return false;
  }
  std::vector<AOTNodeID> passNodes = {AOTNodeID(1)};
  AOTNodeID prev = AOTNodeID(1);
  size_t slotCount = 0;
  uint32_t index = 2;
  while (index < graph.nodeCount()) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      return false;
    }
    if (node->kind == AOTEffectKind::GeometryWhiteInput) {
      // Designator only; its single consumer (the xfer's constant child) is checked below, and any
      // other consumer is rejected when the loop reaches it.
      ++index;
      continue;
    }
    if (slotCount >= MaxPointwiseSlots) {
      return false;
    }
    if (IsUnaryPointwiseOp(node, prev)) {
      passNodes.push_back(AOTNodeID(index));
      ++slotCount;
      prev = AOTNodeID(index);
      ++index;
      continue;
    }
    if (node->kind == AOTEffectKind::ConstColor && node->inputs.size() == 1) {
      auto next = index + 1 < graph.nodeCount() ? graph.nodeAt(AOTNodeID(index + 1)) : nullptr;
      if (next != nullptr && next->kind == AOTEffectKind::Blend && next->inputs.size() == 2 &&
          ((next->inputs[0] == AOTNodeID(index) && next->inputs[1] == prev) ||
           (next->inputs[1] == AOTNodeID(index) && next->inputs[0] == prev))) {
        // The shared const-operand rule plus the adjacency this planner needs: the constant is
        // the node right before the blend (the slot's uniform record reads it from the blend's
        // input edge). An inexpressible pair rejects the chain here, not at the executor.
        if (!IsConstOperandBlend(graph, next, prev)) {
          return false;
        }
        passNodes.push_back(AOTNodeID(index + 1));
        ++slotCount;
        prev = AOTNodeID(index + 1);
        index += 2;
        continue;
      }
      if (node->inputs[0] == prev) {
        // A const-color op of its own, applied to the chain value (OP_CONST_COLOR honors the
        // input mode).
        passNodes.push_back(AOTNodeID(index));
        ++slotCount;
        prev = AOTNodeID(index);
        ++index;
        continue;
      }
    }
    return false;
  }
  if (slotCount > MaxPointwiseSlots || prev != graph.root()) {
    return false;
  }

  AOTEffectPlan result = {};
  AOTPassDescriptor pass = {};
  pass.kernel = AOTKernelKind::PerlinNoiseFill;
  pass.nodes = std::move(passNodes);
  pass.output = pass.nodes.back();
  result.passes.push_back(std::move(pass));
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

// Planner for a YUV video source with a bounded color-grade tail: GeometryColor -> YUV
// TextureSource -> [0..3 unary pointwise ops]. YUVTextureFillShader carries the plane sampling
// and conversion plus three pointwise slots, so the color-grade shapes (a paint color filter
// over a video frame) ride a single fused pass. Anything beyond the budget — a blend, a fourth
// operator, any non-pointwise node — is rejected and keeps the runtime route.
static bool DecomposeYUVChain(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 3 || graph.root().index() + 1 != graph.nodeCount()) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor ||
      !IsYUVChainSource(graph, AOTNodeID(1))) {
    return false;
  }
  AOTNodeID prev = AOTNodeID(1);
  for (uint32_t index = 2; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!IsUnaryPointwiseOp(node, prev)) {
      return false;
    }
    prev = AOTNodeID(index);
  }
  if (graph.nodeCount() - 2 > MaxPointwiseSlots) {
    return false;
  }
  AOTEffectPlan result = {};
  AOTPassDescriptor pass = {};
  pass.kernel = AOTKernelKind::YUVTextureFill;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    pass.nodes.push_back(AOTNodeID(index));
  }
  pass.output = graph.root();
  result.passes.push_back(std::move(pass));
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

// Plans one fused chain pass over the given reachable node set, rooted at subtreeRoot: the shape
// whitelist (which node kinds a single fused pass may carry), the pass node list (designators
// excluded) and the chain builder's own profile resolution, so the planner, CanExecute and
// construction cannot count differently. Leaf parameters and budgets belong to the chain builder's
// planner, consulted through ResolveChainProfile.
static bool PlanReachableSubtree(const AOTEffectGraph& graph, AOTNodeID subtreeRoot,
                                 const std::vector<bool>& reachable, AOTPassDescriptor* pass) {
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
    switch (node->kind) {
      case AOTEffectKind::TextureSource:
      case AOTEffectKind::ColorMatrix:
      case AOTEffectKind::Luma:
      case AOTEffectKind::AlphaThreshold:
      case AOTEffectKind::ColorSpaceXform:
      case AOTEffectKind::ConstColor:
      case AOTEffectKind::Blend:
      case AOTEffectKind::RectCoverage:
      case AOTEffectKind::RRectCoverage:
      case AOTEffectKind::GradientSource:
      case AOTEffectKind::PerlinNoiseSource:
      // A designator-only input node (two-child xfer children / single-child xfer child) is never
      // a chain slot; it stays out of the pass node list below.
      case AOTEffectKind::GeometryColorOpaqueInput:
      case AOTEffectKind::GeometryWhiteInput:
      // Explicit input-environment and epilogue instructions for a two-child xfer over a
      // computed input; real chain slots (OP_INPUT_OPAQUE / OP_MUL_ALPHA).
      case AOTEffectKind::InputOpaque:
      case AOTEffectKind::MulAlpha:
        break;
      default:
        // GeometryColor only legal at index 0; anything else (Gather/Neighborhood/External) is not
        // fusable into one pass.
        return false;
    }
  }
  *pass = {};
  pass->output = subtreeRoot;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!reachable[index] || node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
        node->kind == AOTEffectKind::GeometryWhiteInput) {
      continue;
    }
    pass->nodes.push_back(AOTNodeID(index));
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!AOTChainBuilder::PlanChainPass(graph, *pass, false, &profile)) {
    return false;
  }
  pass->resourceProfile = profile;
  pass->kernel =
      IsAOTGraphProfile(profile) ? AOTKernelKind::PointwiseGraph : AOTKernelKind::PointwiseChain;
  return true;
}

// Planner for a pointwise DAG whose only leaves are texture sources / const colors and whose
// interior nodes are pure pointwise or blend ops. Such a DAG evaluates in a single fused pass (the
// PointwiseChain kernel) with no intermediate materialization. Resolved plain, tiled and
// device-space texture leaves are supported; non-pointwise nodes require a different planner.
// A noise-free graph admits its whole node space (a dead node would still encode as a dead slot);
// a noise graph keeps to the root-reachable set, which is all the noise family's budgets cover.
static bool DecomposePointwiseDAG(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2) {
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
  AOTPassDescriptor pass = {};
  if (!PlanReachableSubtree(graph, graph.root(), reachable, &pass)) {
    return false;
  }
  AOTEffectPlan result = {};
  result.passes.push_back(std::move(pass));
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

// Planner for an over-budget pointwise graph: iteratively cuts one blend operand at a time into
// its own materializing pass, until the remaining main pass fits one fused chain pass. The cut
// rule is structural — the operand of the blend nearest the root that is legal to materialize and
// fits a single pass itself — with no operator-specific cut points and no optimality search; every
// cut shrinks the remaining graph, so the loop terminates, and the passes come out in topological
// order (an operand pass may itself read earlier cuts). Admission is by the same segment build
// construction runs, over placeholder intermediate leaves, so plan-time fit and construction fit
// cannot drift.

// Collects the nodes reachable from root, stopping at the cut roots: in the collecting pass they
// resolve to texture leaves, so their subtrees belong to earlier passes. The cut roots themselves
// are not part of the reachable set.
static bool CollectReachableStoppingAtCuts(const AOTEffectGraph& graph, AOTNodeID root,
                                           const std::vector<AOTNodeID>& cutRoots,
                                           std::vector<bool>* reachable) {
  if (reachable == nullptr || !root.isValid() || root.index() >= graph.nodeCount()) {
    return false;
  }
  auto isCut = [&cutRoots](AOTNodeID id) {
    for (auto cut : cutRoots) {
      if (id == cut) {
        return true;
      }
    }
    return false;
  };
  std::vector<AOTNodeID> pending = {root};
  while (!pending.empty()) {
    auto id = pending.back();
    pending.pop_back();
    if (isCut(id)) {
      continue;
    }
    const auto* node = graph.nodeAt(id);
    if (node == nullptr) {
      return false;
    }
    if ((*reachable)[id.index()]) {
      continue;
    }
    (*reachable)[id.index()] = true;
    if (node->kind != AOTEffectKind::PerlinNoiseSource) {
      pending.insert(pending.end(), node->inputs.begin(), node->inputs.end());
    }
  }
  return true;
}

// The placeholder a cut's replacement stands in as during planning: device sampling, identity
// matrix, no perspective — the facts the fit depends on — with no texture behind it yet.
static AOTTextureParameters CutIntermediatePlaceholder() {
  AOTTextureParameters parameters = {};
  parameters.isIntermediate = true;
  parameters.samplingKind = AOTTextureSamplingKind::Device;
  parameters.uvMatrix = Matrix::I();
  return parameters;
}

// Plans one self-contained pass of a cut plan over the subtree at root, with the cut roots
// standing in as placeholder texture leaves. Fills the original-graph node list (designators
// excluded — they are copied but never listed), the output, the cut edges the subtree actually
// reads (materializedInputs/dependencies, in cut order), and the kernel and resource profile the
// segment resolves to. Runs the same segment build construction uses, so this is the real
// admission: plan-time fit and construction fit cannot drift.
static bool PlanCutPass(const AOTEffectGraph& graph, AOTNodeID root,
                        const std::vector<AOTNodeID>& cutRoots, AOTPassDescriptor* pass) {
  if (pass == nullptr) {
    return false;
  }
  std::vector<bool> reachable(graph.nodeCount(), false);
  if (!CollectReachableStoppingAtCuts(graph, root, cutRoots, &reachable)) {
    return false;
  }
  AOTPassDescriptor candidate = {};
  candidate.output = root;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    const auto* node = graph.nodeAt(AOTNodeID(index));
    if (!reachable[index] || node == nullptr ||
        node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
        node->kind == AOTEffectKind::GeometryWhiteInput) {
      continue;
    }
    candidate.nodes.push_back(AOTNodeID(index));
  }
  if (candidate.nodes.empty()) {
    return false;
  }
  // The cuts this subtree reads: the full reachable closure (not stopping at cuts) decides which
  // cut roots it samples.
  std::vector<bool> fullReachable(graph.nodeCount(), false);
  if (!CollectReachableNodes(graph, root, &fullReachable)) {
    return false;
  }
  for (size_t cut = 0; cut < cutRoots.size(); ++cut) {
    if (fullReachable[cutRoots[cut].index()]) {
      candidate.materializedInputs.push_back(cutRoots[cut]);
      candidate.dependencies.push_back(static_cast<uint32_t>(cut));
    }
  }
  std::vector<AOTTextureParameters> placeholders(candidate.materializedInputs.size(),
                                                 CutIntermediatePlaceholder());
  AOTEffectGraph segment = {};
  AOTPassDescriptor segmentPass = {};
  if (!AOTChainBuilder::BuildCutSegment(graph, candidate, placeholders, Matrix::I(), Matrix::I(),
                                        &segment, &segmentPass)) {
    return false;
  }
  candidate.kernel = segmentPass.kernel;
  candidate.resourceProfile = segmentPass.resourceProfile;
  *pass = std::move(candidate);
  return true;
}

// A cut candidate must be a self-contained shader value. Its subtree (stopping at earlier cuts)
// must not reach the geometry color — a materializing fill would bake the paint alpha in, and the
// terminal epilogue would apply it a second time — and must not carry device coverage nodes,
// which evaluate the draw's own geometry rather than an operand value. A bare texture leaf or
// constant is not a candidate: swapping it for an intermediate leaf releases no sampler slot (it
// occupies one either way) and only adds an RGBA8 quantization step. A bare gradient or noise
// leaf IS a candidate: their per-kind budgets are released by the cut.
static bool IsCuttableOperand(const AOTEffectGraph& graph, AOTNodeID operand,
                              const std::vector<AOTNodeID>& cutRoots) {
  if (!operand.isValid() || operand.index() == 0 || operand.index() >= graph.nodeCount()) {
    return false;
  }
  const auto* node = graph.nodeAt(operand);
  if (node == nullptr || node->kind == AOTEffectKind::TextureSource ||
      node->kind == AOTEffectKind::ConstColor ||
      node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
      node->kind == AOTEffectKind::GeometryWhiteInput) {
    return false;
  }
  std::vector<bool> reachable(graph.nodeCount(), false);
  if (!CollectReachableStoppingAtCuts(graph, operand, cutRoots, &reachable)) {
    return false;
  }
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    if (!reachable[index]) {
      continue;
    }
    const auto* subNode = graph.nodeAt(AOTNodeID(index));
    if (subNode->kind == AOTEffectKind::RectCoverage ||
        subNode->kind == AOTEffectKind::RRectCoverage) {
      return false;
    }
  }
  return !reachable[0];
}

// The next cut: scanning blend nodes from the root side (the graph is SSA in construction order,
// so the highest node id is the closest to the root) and each blend's inputs in order, the first
// operand that is legal to materialize and fits a single pass itself. Deterministic, and every
// cut strictly shrinks the remaining graph. On success operandPass is the planned materializing
// pass for that operand.
static bool FindNextCut(const AOTEffectGraph& graph, const std::vector<AOTNodeID>& cutRoots,
                        AOTPassDescriptor* operandPass) {
  std::vector<bool> underCut(graph.nodeCount(), false);
  for (auto cut : cutRoots) {
    if (!CollectReachableNodes(graph, cut, &underCut)) {
      return false;
    }
  }
  const auto nodeCount = static_cast<uint32_t>(graph.nodeCount());
  for (uint32_t index = nodeCount - 1; index >= 1; --index) {
    if (underCut[index]) {
      continue;
    }
    const auto* node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr || node->kind != AOTEffectKind::Blend) {
      continue;
    }
    for (auto input : node->inputs) {
      if (!IsCuttableOperand(graph, input, cutRoots)) {
        continue;
      }
      if (PlanCutPass(graph, input, cutRoots, operandPass)) {
        return true;
      }
    }
  }
  return false;
}

static bool DecomposeMaterializedBlend(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }
  AOTEffectPlan result = {};
  std::vector<AOTNodeID> cutRoots = {};
  while (true) {
    AOTPassDescriptor mainPass = {};
    if (PlanCutPass(graph, graph.root(), cutRoots, &mainPass)) {
      result.passes.push_back(std::move(mainPass));
      result.output = graph.root();
      *plan = std::move(result);
      return true;
    }
    if (result.passes.size() + 1 >= MaxCutPlanPasses) {
      return false;
    }
    AOTPassDescriptor operandPass = {};
    if (!FindNextCut(graph, cutRoots, &operandPass)) {
      return false;
    }
    operandPass.materializesOutput = true;
    cutRoots.push_back(operandPass.output);
    result.passes.push_back(std::move(operandPass));
  }
}

bool AOTEffectDecomposer::Decompose(const AOTEffectGraph& graph, AOTEffectPlan* plan,
                                    bool singlePassOnly) {
  // Keep simple noise on the existing compact kernel. Noise DAGs and ordinary pointwise DAGs
  // use their separate graph/chain families within shared budgets, without materialization.
  // A linear chain over one chain pass's budget splits into several chain passes; YUV sources
  // keep their dedicated kernel.
  if (plan == nullptr) {
    return false;
  }
  using Planner = bool (*)(const AOTEffectGraph&, AOTEffectPlan*);
  for (Planner planner :
       {DecomposePerlinNoiseChain, DecomposePointwiseDAG, DecomposeMaterializedBlend,
        DecomposeLinearMultiPass, DecomposeYUVChain}) {
    if (singlePassOnly && planner == &DecomposeMaterializedBlend) {
      // The cut planner's plans always start with a materializing pass and always need a task;
      // a single-pass-only caller would run its iterative segment builds only to discard the
      // result.
      continue;
    }
    AOTEffectPlan candidate = {};
    if (!planner(graph, &candidate)) {
      continue;
    }
    // Each planner's own admission subsumes what AOTPlanExecutor::CanExecute would re-derive: the
    // DAG planner resolves the profile and kernel together, the linear planner's fit calls
    // BuildLinearSegment with the same arguments the executor's segmented admission uses, the
    // perlin planner checks the slot shapes and the lookup resources, and the cut planner runs the
    // same segment build construction uses over placeholder intermediates. The re-check therefore
    // runs only as a debug assertion.
    DEBUG_ASSERT(AOTPlanExecutor::CanExecute(graph, candidate));
    *plan = std::move(candidate);
    return true;
  }
  return false;
}

AOTDecomposeAnalysis AOTEffectDecomposer::Analyze(const ProgramInfo* programInfo) {
  AOTDecomposeAnalysis result = {};
  if (programInfo == nullptr) {
    return result;
  }
  auto total = programInfo->numFragmentProcessors();
  auto colorCount = programInfo->numColorFragmentProcessors();
  std::vector<const FragmentProcessor*> colorProcessors;
  std::vector<const FragmentProcessor*> coverageProcessors;
  colorProcessors.reserve(colorCount);
  for (size_t i = 0; i < colorCount; ++i) {
    colorProcessors.push_back(programInfo->getFragmentProcessor(i));
  }
  for (size_t i = colorCount; i < total; ++i) {
    coverageProcessors.push_back(programInfo->getFragmentProcessor(i));
  }
  result.color = ClassifyAxis(colorProcessors);
  result.coverage = ClassifyAxis(coverageProcessors);
  return result;
}

}  // namespace tgfx
