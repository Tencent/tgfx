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

// The source of a multi-pass linear plan: an affine RGBA texture fed by the geometry color, sampled
// plainly or in device space.
static bool IsLinearMultiPassSource(const AOTEffectNode* node) {
  if (node == nullptr || node->kind != AOTEffectKind::TextureSource || node->inputs.size() != 1 ||
      node->inputs[0] != AOTNodeID(0)) {
    return false;
  }
  auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
  if (parameters == nullptr || parameters->isYUV || parameters->isAlphaOnly ||
      parameters->hasRGBAAA || parameters->hasPerspective) {
    return false;
  }
  return parameters->samplingKind == AOTTextureSamplingKind::Plain ||
         parameters->samplingKind == AOTTextureSamplingKind::Device;
}

static bool IsUnaryPointwiseOp(const AOTEffectNode* node) {
  return node != nullptr && node->inputs.size() == 1 &&
         (node->kind == AOTEffectKind::ColorMatrix || node->kind == AOTEffectKind::Luma ||
          node->kind == AOTEffectKind::AlphaThreshold ||
          node->kind == AOTEffectKind::ColorSpaceXform);
}

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
      !IsLinearMultiPassSource(graph.nodeAt(AOTNodeID(1)))) {
    return false;
  }
  for (uint32_t index = 2; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!IsUnaryPointwiseOp(node) || node->inputs[0] != AOTNodeID(index - 1)) {
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

static bool IsPerlinNoiseSource(const AOTEffectNode* node) {
  return node != nullptr && node->kind == AOTEffectKind::PerlinNoiseSource &&
         node->inputs.size() == 1 && node->inputs[0] == AOTNodeID(0);
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
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor ||
      !IsPerlinNoiseSource(graph.nodeAt(AOTNodeID(1)))) {
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
    if (slotCount >= PerlinNoiseFragmentProcessor::MaxPointwiseSlots) {
      return false;
    }
    if (IsUnaryPointwiseOp(node) && node->inputs.size() == 1 && node->inputs[0] == prev) {
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
        auto blendParams = std::get_if<AOTBlendParameters>(&next->parameters);
        // A two-child blend needs both operands evaluated, which a single const-operand slot
        // cannot express.
        if (blendParams == nullptr || blendParams->childType == 2) {
          return false;
        }
        // The OP_BLEND slot reads the constant straight from its uniform record, so the operand
        // must not depend on its own input edge.
        if (!AOTConstColorIsInputIndependent(graph, node)) {
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
  if (slotCount > PerlinNoiseFragmentProcessor::MaxPointwiseSlots || prev != graph.root()) {
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
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }
  auto sourceNode = graph.nodeAt(AOTNodeID(1));
  if (sourceNode == nullptr || sourceNode->kind != AOTEffectKind::TextureSource ||
      sourceNode->inputs.size() != 1 || sourceNode->inputs[0] != AOTNodeID(0)) {
    return false;
  }
  auto parameters = std::get_if<AOTTextureParameters>(&sourceNode->parameters);
  if (parameters == nullptr || !parameters->isYUV || parameters->hasRGBAAA ||
      parameters->samplingKind != AOTTextureSamplingKind::Plain) {
    return false;
  }
  AOTNodeID prev = AOTNodeID(1);
  for (uint32_t index = 2; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!IsUnaryPointwiseOp(node) || node->inputs[0] != prev) {
      return false;
    }
    prev = AOTNodeID(index);
  }
  if (graph.nodeCount() - 2 > 3) {
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

// New planner: a pointwise DAG whose only leaves are texture sources / const colors and whose
// interior nodes are pure pointwise or blend ops. Such a DAG evaluates in a single fused pass (the
// PointwiseChain kernel) with no intermediate materialization. Resolved plain, tiled and
// device-space texture leaves are supported; non-pointwise nodes require a different planner.
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
  std::vector<AOTNodeID> pending =
      containsNoise ? std::vector<AOTNodeID>{graph.root()} : std::vector<AOTNodeID>{};
  while (!pending.empty()) {
    auto id = pending.back();
    pending.pop_back();
    const auto* node = graph.nodeAt(id);
    if (node == nullptr) {
      return false;
    }
    if (reachable[id.index()]) {
      continue;
    }
    reachable[id.index()] = true;
    if (node->kind != AOTEffectKind::PerlinNoiseSource) {
      pending.insert(pending.end(), node->inputs.begin(), node->inputs.end());
    }
  }
  // Shape rule only: which node kinds a single fused pass may carry. Leaf parameters, budgets and
  // the resource profile belong to the chain builder's planner, consulted below, so the planner,
  // CanExecute and construction cannot count differently.
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
  AOTEffectPlan result = {};
  AOTPassDescriptor pass = {};
  pass.output = graph.root();
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!reachable[index] || node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
        node->kind == AOTEffectKind::GeometryWhiteInput) {
      continue;
    }
    pass.nodes.push_back(AOTNodeID(index));
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!AOTChainBuilder::ResolveChainProfile(graph, pass, &profile)) {
    return false;
  }
  pass.resourceProfile = profile;
  pass.kernel =
      IsAOTGraphProfile(profile) ? AOTKernelKind::PointwiseGraph : AOTKernelKind::PointwiseChain;
  result.passes.push_back(std::move(pass));
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

bool AOTEffectDecomposer::Decompose(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  // Keep simple noise on the existing compact kernel. Noise DAGs and ordinary pointwise DAGs
  // use their separate graph/chain families within shared budgets, without materialization.
  // A linear chain over one chain pass's budget splits into several chain passes; YUV sources
  // keep their dedicated kernel.
  // Each planner proposes the plan for its shape; the first one the executor accepts wins, so
  // every plan Decompose returns passes AOTPlanExecutor::CanExecute.
  if (plan == nullptr) {
    return false;
  }
  using Planner = bool (*)(const AOTEffectGraph&, AOTEffectPlan*);
  for (Planner planner : {DecomposePerlinNoiseChain, DecomposePointwiseDAG,
                          DecomposeLinearMultiPass, DecomposeYUVChain}) {
    AOTEffectPlan candidate = {};
    if (planner(graph, &candidate) && AOTPlanExecutor::CanExecute(graph, candidate)) {
      *plan = std::move(candidate);
      return true;
    }
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
  if (xferProcessor == nullptr || xferProcessor->name() != "EmptyXferProcessor") {
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
        gpName != "EllipseGeometryProcessor"))) {
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
