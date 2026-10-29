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
  if (!AOTChainBuilder::ResolveChainProfile(graph, *pass, &profile)) {
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

// Planner for an over-budget two-child blend at the root: one operand subtree alone is a legal
// fused chain pass, so it materializes into pass 0 and the main pass re-reads the operand root as
// a texture leaf (materializedInput) — the replacement, and with it the main pass's real fit, is
// resolved when the executor builds the main pass's segment graph (the intermediate texture does
// not exist at plan time), so this planner's candidates still go through the executor's admission.
// The operand subtree must stay leaf-free (gradients, noise, pointwise ops and constants only): a
// texture leaf in the operand would need its sampling remapped through the split-off fill, which
// the retry's producer path serves today and this planner does not yet.
static bool DecomposeMaterializedBlend(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }
  auto* root = graph.nodeAt(graph.root());
  if (root == nullptr || root->kind != AOTEffectKind::Blend || root->inputs.size() != 2) {
    return false;
  }
  auto blendParams = std::get_if<AOTBlendParameters>(&root->parameters);
  if (blendParams == nullptr || blendParams->childType != 2) {
    return false;
  }
  std::vector<bool> rootReachable(graph.nodeCount(), false);
  if (!CollectReachableNodes(graph, graph.root(), &rootReachable)) {
    return false;
  }
  for (auto operand : root->inputs) {
    std::vector<bool> operandReachable(graph.nodeCount(), false);
    if (!CollectReachableNodes(graph, operand, &operandReachable)) {
      continue;
    }
    bool operandHasTextureLeaf = false;
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      operandHasTextureLeaf |= operandReachable[index] &&
                               graph.nodeAt(AOTNodeID(index))->kind == AOTEffectKind::TextureSource;
    }
    if (operandHasTextureLeaf) {
      continue;
    }
    AOTPassDescriptor operandPass = {};
    if (!PlanReachableSubtree(graph, operand, operandReachable, &operandPass)) {
      continue;
    }
    AOTEffectPlan result = {};
    operandPass.materializesOutput = true;
    result.passes.push_back(std::move(operandPass));
    AOTPassDescriptor mainPass = {};
    mainPass.output = graph.root();
    mainPass.dependencies = {0};
    mainPass.materializedInput = operand;
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      auto node = graph.nodeAt(AOTNodeID(index));
      if (!rootReachable[index] || operandReachable[index] ||
          node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
          node->kind == AOTEffectKind::GeometryWhiteInput) {
        continue;
      }
      mainPass.nodes.push_back(AOTNodeID(index));
    }
    if (mainPass.nodes.empty()) {
      continue;
    }
    result.passes.push_back(std::move(mainPass));
    result.output = graph.root();
    *plan = std::move(result);
    return true;
  }
  return false;
}

bool AOTEffectDecomposer::Decompose(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  // Keep simple noise on the existing compact kernel. Noise DAGs and ordinary pointwise DAGs
  // use their separate graph/chain families within shared budgets, without materialization.
  // A linear chain over one chain pass's budget splits into several chain passes; YUV sources
  // keep their dedicated kernel.
  // Each planner is the admission authority for its shape: its own fit checks subsume what
  // AOTPlanExecutor::CanExecute would re-derive (the DAG planner resolves the profile and kernel
  // together; the linear planner's fit calls BuildLinearSegment with the same arguments the
  // executor's segmented admission uses; the perlin planner checks the slot shapes and the lookup
  // resources). The re-check therefore runs only as a debug assertion.
  if (plan == nullptr) {
    return false;
  }
  using Planner = bool (*)(const AOTEffectGraph&, AOTEffectPlan*);
  for (Planner planner :
       {DecomposePerlinNoiseChain, DecomposePointwiseDAG, DecomposeMaterializedBlend,
        DecomposeLinearMultiPass, DecomposeYUVChain}) {
    AOTEffectPlan candidate = {};
    if (!planner(graph, &candidate)) {
      continue;
    }
    // The materialized-blend planner defers the main pass's fit to construction (the intermediate
    // texture does not exist at plan time), so its candidate still goes through the executor's
    // real admission; the other planners' own fit checks subsume that admission, which therefore
    // runs only as a debug assertion on them.
    if (planner == &DecomposeMaterializedBlend) {
      if (!AOTPlanExecutor::CanExecute(graph, candidate)) {
        continue;
      }
    } else {
      DEBUG_ASSERT(AOTPlanExecutor::CanExecute(graph, candidate));
    }
    *plan = std::move(candidate);
    return true;
  }
  return false;
}

AOTAxisAnalysis AOTEffectDecomposer::AnalyzeChain(
    const std::vector<const FragmentProcessor*>& processors) {
  return ClassifyAxis(processors);
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

const char* AOTFoldRouteOutcomeName(AOTFoldRouteOutcome outcome) {
  switch (outcome) {
    case AOTFoldRouteOutcome::NotApplicable:
      return "NotApplicable";
    case AOTFoldRouteOutcome::FoldBlockedByXP:
      return "FoldBlockedByXP";
    case AOTFoldRouteOutcome::BlockedByLowering:
      return "BlockedByLowering";
    case AOTFoldRouteOutcome::UnsupportedShape:
      return "UnsupportedShape";
    case AOTFoldRouteOutcome::GPIncompatible:
      return "GPIncompatible";
    case AOTFoldRouteOutcome::Routable:
      return "Routable";
  }
  return "Unknown";
}

AOTFoldRouteOutcome AOTEffectDecomposer::AnalyzeFoldRoute(const ProgramInfo* programInfo) {
  if (programInfo == nullptr) {
    return AOTFoldRouteOutcome::NotApplicable;
  }
  auto total = programInfo->numFragmentProcessors();
  auto colorCount = programInfo->numColorFragmentProcessors();
  if (total == colorCount) {
    return AOTFoldRouteOutcome::NotApplicable;
  }
  // Folding coverage into the color chain is only pixel-identical when the blend does not read the
  // destination: a PorterDuff XP derives its factors from the source alpha, and folding changes
  // what that alpha is. EmptyXferProcessor (fixed-function pipeline blend) applies coverage as a
  // plain multiply, which is exactly what the folded chain reproduces.
  auto xferProcessor = programInfo->getXferProcessor();
  if (xferProcessor == nullptr || !IsProcessor<EmptyXferProcessor>(xferProcessor)) {
    return AOTFoldRouteOutcome::FoldBlockedByXP;
  }
  std::vector<const FragmentProcessor*> allProcessors;
  allProcessors.reserve(total);
  for (size_t i = 0; i < total; ++i) {
    allProcessors.push_back(programInfo->getFragmentProcessor(i));
  }
  AOTEffectGraph graph = {};
  if (!Lower(allProcessors, &graph)) {
    return AOTFoldRouteOutcome::BlockedByLowering;
  }
  AOTEffectPlan plan = {};
  if (!Decompose(graph, &plan)) {
    return AOTFoldRouteOutcome::UnsupportedShape;
  }
  auto gp = programInfo->getGeometryProcessor();
  if (gp == nullptr) {
    return AOTFoldRouteOutcome::GPIncompatible;
  }
  const bool pointwiseDAG =
      plan.passes.size() == 1 && IsAOTPointwiseDAGKernel(plan.passes[0].kernel);
  const auto& gpName = gp->name();
  if (gpName != "DefaultGeometryProcessor" && gpName != "QuadPerEdgeAAGeometryProcessor" &&
      (!pointwiseDAG ||
       (gpName != "MeshGeometryProcessor" && gpName != "AtlasTextGeometryProcessor" &&
        gpName != "EllipseGeometryProcessor" && gpName != "HairlineLineGeometryProcessor" &&
        gpName != "ComplexEllipseGeometryProcessor"))) {
    return AOTFoldRouteOutcome::GPIncompatible;
  }
  if (pointwiseDAG && gpName == "QuadPerEdgeAAGeometryProcessor" &&
      static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp)->getHasSubset()) {
    return AOTFoldRouteOutcome::GPIncompatible;
  }
  if (pointwiseDAG && plan.passes[0].resourceProfile == AOTResourceProfile::NoiseGraph) {
    if (gpName != "QuadPerEdgeAAGeometryProcessor") {
      return AOTFoldRouteOutcome::GPIncompatible;
    }
    const auto* quad = static_cast<const QuadPerEdgeAAGeometryProcessor*>(gp);
    if (!quad->hasCommonColor() || !quad->hasUVMatrix()) {
      return AOTFoldRouteOutcome::GPIncompatible;
    }
  }
  if (pointwiseDAG && gpName == "AtlasTextGeometryProcessor") {
    if (gp->numTextureSamplers() != 0) {
      return AOTFoldRouteOutcome::GPIncompatible;
    }
    bool hasLeaf = false;
    for (auto nodeID : plan.passes[0].nodes) {
      const auto* node = graph.nodeAt(nodeID);
      const auto* gradient = std::get_if<AOTGradientParameters>(&node->parameters);
      if (node->kind == AOTEffectKind::TextureSource ||
          (gradient != nullptr && gradient->colorizerKind == 3)) {
        hasLeaf = true;
        break;
      }
    }
    if (!hasLeaf) {
      return AOTFoldRouteOutcome::GPIncompatible;
    }
  }
  return AOTFoldRouteOutcome::Routable;
}

}  // namespace tgfx
