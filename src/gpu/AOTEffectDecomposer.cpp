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
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

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

}  // namespace tgfx
