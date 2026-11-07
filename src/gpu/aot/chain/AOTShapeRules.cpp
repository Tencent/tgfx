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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/ir/NodeContract.h"

namespace tgfx {

// The single authority for the pointwise shapes shared by the planners (AOTEffectDecomposer), the
// planner's admission checks (AOTPlanner) and the chain builder: before this file each of
// them carried its own copy, and the copies had already drifted.

bool CollectReachableStoppingAtCuts(const AOTEffectGraph& graph, AOTNodeID root,
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

bool PassReadsGeometryColor(const AOTEffectGraph& graph, const PassIR& pass) {
  std::vector<bool> reachable(graph.nodeCount(), false);
  if (!CollectReachableStoppingAtCuts(graph, pass.output, pass.materializedInputs, &reachable)) {
    return true;
  }
  return reachable[0];
}

bool PassNeedsLocalRemap(const AOTEffectGraph& graph, const PassIR& pass) {
  std::vector<bool> reachable(graph.nodeCount(), false);
  if (!CollectReachableStoppingAtCuts(graph, pass.output, pass.materializedInputs, &reachable)) {
    return true;
  }
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    if (!reachable[index]) {
      continue;
    }
    const auto* node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      return true;
    }
    const auto remap = TraitsOf(node->kind).remap;
    if (remap == AOTRemapKind::LocalMatrix) {
      return true;
    }
    if (remap == AOTRemapKind::TextureMatrix) {
      auto* parameters = std::get_if<AOTTextureParameters>(&node->parameters);
      if (parameters != nullptr && parameters->samplingKind != AOTTextureSamplingKind::Device) {
        return true;
      }
    }
  }
  return false;
}

bool PlanBakesGeometryAlpha(const AOTEffectGraph& graph, const Plan& plan) {
  for (size_t index = 0; index + 1 < plan.passes.size(); ++index) {
    if (PassReadsGeometryColor(graph, plan.passes[index])) {
      return true;
    }
  }
  return false;
}

bool IsUnaryPointwiseKind(AOTEffectKind kind) {
  return TraitsOf(kind).unaryPointwise;
}

bool IsUnaryPointwiseOp(const AOTEffectNode* node, AOTNodeID expectedInput) {
  return node != nullptr && IsUnaryPointwiseKind(node->kind) && node->inputs.size() == 1 &&
         node->inputs[0] == expectedInput;
}

bool IsLinearMultiPassSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
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

bool IsInputDesignator(AOTEffectKind kind) {
  return TraitsOf(kind).isInputDesignator();
}

bool CollectReachableNodes(const AOTEffectGraph& graph, AOTNodeID root,
                           std::vector<bool>* reachable) {
  if (reachable == nullptr || !root.isValid() || root.index() >= graph.nodeCount()) {
    return false;
  }
  std::vector<AOTNodeID> pending = {root};
  while (!pending.empty()) {
    auto id = pending.back();
    pending.pop_back();
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

}  // namespace tgfx
