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
#include "gpu/AOTPlanExecutor.h"
#include "gpu/ProgramInfo.h"
#include "gpu/processors/FragmentProcessor.h"

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
  if (!AOTEffectDecomposer::ValidateForFusion(graph)) {
    analysis.outcome = AOTDecomposeOutcome::BlockedByValidation;
    return analysis;
  }
  AOTEffectPlan plan = {};
  if (!AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::PreferFusion, &plan)) {
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

bool AOTEffectDecomposer::ValidateForFusion(const AOTEffectGraph& graph) {
  for (uint32_t index = 0; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      return false;
    }
    if (node->kind == AOTEffectKind::ColorMatrix) {
      auto parameters = std::get_if<AOTColorMatrixParameters>(&node->parameters);
      if (parameters == nullptr) {
        return false;
      }
      // Row-major 4x5 matrix; the alpha row is indices 15..19 and index 19 is its constant bias. A
      // non-zero alpha bias means the matrix can produce non-zero alpha from transparent-black
      // input (affectsTransparentBlack, design §6.3). Fusing it would silently drop the source
      // alpha constraint, so reject and fall back to the plain route.
      if (parameters->matrix[19] != 0.0f) {
        return false;
      }
    }
  }
  return true;
}

static bool IsPointwiseTailSource(const AOTEffectNode* node) {
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

static bool IsPointwiseTailOp(const AOTEffectNode* node) {
  return node != nullptr && node->inputs.size() == 1 &&
         (node->kind == AOTEffectKind::ColorMatrix || node->kind == AOTEffectKind::Luma ||
          node->kind == AOTEffectKind::AlphaThreshold ||
          node->kind == AOTEffectKind::ColorSpaceXform);
}

static bool DecomposeLinearPointwiseTail(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2 || graph.root().index() + 1 != graph.nodeCount()) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor ||
      !IsPointwiseTailSource(graph.nodeAt(AOTNodeID(1)))) {
    return false;
  }
  for (uint32_t index = 2; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (!IsPointwiseTailOp(node) || node->inputs[0] != AOTNodeID(index - 1)) {
      return false;
    }
  }

  AOTEffectPlan result = {};
  uint32_t nodeIndex = 1;
  bool firstPass = true;
  while (nodeIndex < graph.nodeCount()) {
    AOTPassDescriptor pass = {};
    pass.kernel = AOTKernelKind::PointwiseTail;
    if (firstPass) {
      pass.nodes.push_back(AOTNodeID(nodeIndex++));
      firstPass = false;
    } else {
      pass.dependencies.push_back(static_cast<uint32_t>(result.passes.size() - 1));
    }
    size_t slotCount = 0;
    while (nodeIndex < graph.nodeCount() && slotCount < 2) {
      pass.nodes.push_back(AOTNodeID(nodeIndex++));
      ++slotCount;
    }
    pass.output = pass.nodes.back();
    result.passes.push_back(std::move(pass));
  }
  for (size_t index = 0; index < result.passes.size(); ++index) {
    result.passes[index].materializesOutput = index + 1 < result.passes.size();
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
static bool DecomposePerlinNoiseChain(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2 || graph.nodeCount() > 6 ||
      graph.root().index() + 1 != graph.nodeCount()) {
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
    if (IsPointwiseTailOp(node) && node->inputs.size() == 1 && node->inputs[0] == prev) {
      passNodes.push_back(AOTNodeID(index));
      ++slotCount;
      prev = AOTNodeID(index);
      ++index;
      continue;
    }
    if (node->kind == AOTEffectKind::ConstColor && node->inputs.size() == 1 &&
        node->inputs[0] == prev) {
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
        passNodes.push_back(AOTNodeID(index + 1));
        ++slotCount;
        prev = AOTNodeID(index + 1);
        index += 2;
        continue;
      }
      // A const-color op of its own.
      passNodes.push_back(AOTNodeID(index));
      ++slotCount;
      prev = AOTNodeID(index);
      ++index;
      continue;
    }
    return false;
  }
  if (slotCount > 3 || prev != graph.root()) {
    return false;
  }

  AOTEffectPlan result = {};
  AOTPassDescriptor pass = {};
  pass.kernel = AOTKernelKind::PerlinNoiseFill;
  pass.nodes = std::move(passNodes);
  pass.output = pass.nodes.back();
  pass.materializesOutput = false;
  result.passes.push_back(std::move(pass));
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

// Existing narrow planner: a strictly linear TextureSource -> ColorMatrix/Luma chain, mapped onto
// the TextureFill / TextureColorMatrix / TexturedColorMatrix / TexturedLuma kernels. Preserved as the
// standard-plan and conservative fallback path.
static bool DecomposeLinearTextureChain(const AOTEffectGraph& graph, AOTDecompositionMode mode,
                                        AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2 || graph.root().index() + 1 != graph.nodeCount()) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  auto textureNode = graph.nodeAt(AOTNodeID(1));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor ||
      textureNode == nullptr || textureNode->kind != AOTEffectKind::TextureSource ||
      textureNode->inputs.size() != 1 || textureNode->inputs[0] != AOTNodeID(0)) {
    return false;
  }
  auto textureParameters = std::get_if<AOTTextureParameters>(&textureNode->parameters);
  if (textureParameters == nullptr ||
      textureParameters->samplingKind != AOTTextureSamplingKind::Plain ||
      textureParameters->isYUV ||
      (textureParameters->isAlphaOnly && textureParameters->hasRGBAAA)) {
    return false;
  }
  for (uint32_t index = 2; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr || node->inputs.size() != 1 || node->inputs[0] != AOTNodeID(index - 1) ||
        (node->kind != AOTEffectKind::ColorMatrix && node->kind != AOTEffectKind::Luma)) {
      return false;
    }
  }

  AOTEffectPlan result = {};
  uint32_t nodeIndex = 1;
  if (mode == AOTDecompositionMode::PreferFusion && graph.nodeCount() > 2 &&
      graph.nodeAt(AOTNodeID(2))->kind == AOTEffectKind::ColorMatrix) {
    AOTPassDescriptor pass = {};
    pass.kernel = AOTKernelKind::TextureColorMatrix;
    pass.nodes = {AOTNodeID(1), AOTNodeID(2)};
    pass.output = AOTNodeID(2);
    result.passes.push_back(std::move(pass));
    nodeIndex = 3;
  } else {
    AOTPassDescriptor pass = {};
    pass.kernel = AOTKernelKind::TextureFill;
    pass.nodes = {AOTNodeID(1)};
    pass.output = AOTNodeID(1);
    result.passes.push_back(std::move(pass));
    nodeIndex = 2;
  }

  while (nodeIndex < graph.nodeCount()) {
    auto nodeID = AOTNodeID(nodeIndex);
    auto node = graph.nodeAt(nodeID);
    AOTPassDescriptor pass = {};
    pass.kernel = node->kind == AOTEffectKind::ColorMatrix ? AOTKernelKind::TexturedColorMatrix
                                                           : AOTKernelKind::TexturedLuma;
    pass.nodes = {nodeID};
    pass.dependencies = {static_cast<uint32_t>(result.passes.size() - 1)};
    pass.output = nodeID;
    result.passes.push_back(std::move(pass));
    ++nodeIndex;
  }

  for (size_t index = 0; index < result.passes.size(); ++index) {
    result.passes[index].materializesOutput = index + 1 < result.passes.size();
  }
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

// New planner: a pointwise DAG whose only leaves are texture sources / const colors and whose
// interior nodes are pure pointwise or blend ops. Such a DAG evaluates in a single fused pass (the
// PointwiseChain kernel) with no intermediate materialization. Resolved plain and tiled texture
// leaves are supported; device-space and non-pointwise nodes require a different planner.
static bool DecomposePointwiseDAG(const AOTEffectGraph& graph, AOTEffectPlan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }
  int textureLeaves = 0;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    auto node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      return false;
    }
    switch (node->kind) {
      case AOTEffectKind::TextureSource: {
        auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
        if (parameters == nullptr || parameters->isYUV ||
            (parameters->isAlphaOnly && parameters->hasRGBAAA)) {
          return false;
        }
        bool supportedSampling = parameters->samplingKind == AOTTextureSamplingKind::Plain ||
                                 (parameters->samplingKind == AOTTextureSamplingKind::Tiled &&
                                  parameters->tiledRecipe.has_value());
        if (!supportedSampling) {
          return false;
        }
        ++textureLeaves;
        break;
      }
      case AOTEffectKind::ColorMatrix:
      case AOTEffectKind::Luma:
      case AOTEffectKind::AlphaThreshold:
      case AOTEffectKind::ColorSpaceXform:
      case AOTEffectKind::ConstColor:
      case AOTEffectKind::Blend:
      case AOTEffectKind::RectCoverage:
      case AOTEffectKind::GradientSource:
        break;
      default:
        // GeometryColor only legal at index 0; anything else (Gather/Neighborhood/External) is not
        // fusable into one pass.
        return false;
    }
  }
  if (textureLeaves > MaxFusedAOTSamplers) {
    return false;
  }
  AOTEffectPlan result = {};
  AOTPassDescriptor pass = {};
  pass.kernel = AOTKernelKind::PointwiseChain;
  pass.output = graph.root();
  pass.materializesOutput = false;
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    pass.nodes.push_back(AOTNodeID(index));
  }
  result.passes.push_back(std::move(pass));
  result.output = graph.root();
  *plan = std::move(result);
  return true;
}

bool AOTEffectDecomposer::Decompose(const AOTEffectGraph& graph, AOTDecompositionMode mode,
                                    AOTEffectPlan* plan) {
  if (mode == AOTDecompositionMode::PreferFusion && DecomposeLinearPointwiseTail(graph, plan)) {
    return true;
  }
  if (DecomposePerlinNoiseChain(graph, plan)) {
    return true;
  }
  if (DecomposeLinearTextureChain(graph, mode, plan)) {
    return true;
  }
  return DecomposePointwiseDAG(graph, plan);
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
    case AOTFoldRouteOutcome::BlockedByValidation:
      return "BlockedByValidation";
    case AOTFoldRouteOutcome::UnsupportedShape:
      return "UnsupportedShape";
    case AOTFoldRouteOutcome::NotExecutable:
      return "NotExecutable";
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
  if (!ValidateForFusion(graph)) {
    return AOTFoldRouteOutcome::BlockedByValidation;
  }
  AOTEffectPlan plan = {};
  if (!Decompose(graph, AOTDecompositionMode::PreferFusion, &plan)) {
    return AOTFoldRouteOutcome::UnsupportedShape;
  }
  if (!AOTPlanExecutor::CanExecute(graph, plan)) {
    return AOTFoldRouteOutcome::NotExecutable;
  }
  // The fused kernels' matchers only know the two rect-oriented GPs; anything else (shape,
  // atlas text, mesh, ellipse) would fail the strict lookup after a successful plan.
  auto gp = programInfo->getGeometryProcessor();
  if (gp == nullptr || (gp->name() != "DefaultGeometryProcessor" &&
                        gp->name() != "QuadPerEdgeAAGeometryProcessor")) {
    return AOTFoldRouteOutcome::GPIncompatible;
  }
  return AOTFoldRouteOutcome::Routable;
}

}  // namespace tgfx
