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
#include <unordered_set>
#include "gpu/ProgramInfo.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

// The set of fragment-processor classes that currently implement lowerToAOT. Kept next to Lower as
// the single source of truth for blame attribution in Analyze; the pass/fail decision itself is
// always taken from Lower's real result, so a drift here can only mislabel the blocking class, not
// change the recovered/blocked verdict.
static const std::unordered_set<std::string>& AOTLowerableNames() {
  static const std::unordered_set<std::string> names = {
      "TextureEffect", "ColorMatrixFragmentProcessor", "LumaFragmentProcessor",
      "ComposeFragmentProcessor"};
  return names;
}

// Depth-first search for the first fragment processor whose class has no AOT lowering. Returns an
// empty string when every node in the tree is lowerable (in which case Lower's failure, if any, is
// a shape issue rather than a missing lowering).
static std::string FindBlockingProcessor(const FragmentProcessor* processor) {
  if (processor == nullptr) {
    return "null";
  }
  if (AOTLowerableNames().count(processor->name()) == 0) {
    return processor->name();
  }
  for (size_t i = 0; i < processor->numChildProcessors(); ++i) {
    auto blocking = FindBlockingProcessor(processor->childProcessor(i));
    if (!blocking.empty()) {
      return blocking;
    }
  }
  return "";
}

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
  if (!AOTEffectDecomposer::Lower(processors, &graph)) {
    analysis.outcome = AOTDecomposeOutcome::BlockedByLowering;
    for (auto processor : processors) {
      auto blocking = FindBlockingProcessor(processor);
      if (!blocking.empty()) {
        analysis.blockingProcessor = blocking;
        break;
      }
    }
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
                                AOTEffectGraph* graph) {
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

bool AOTEffectDecomposer::Decompose(const AOTEffectGraph& graph, AOTDecompositionMode mode,
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
  if (textureParameters == nullptr || textureParameters->isYUV ||
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
