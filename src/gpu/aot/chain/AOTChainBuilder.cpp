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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/ProgramInfo.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"

namespace tgfx {

bool AOTChainBuilder::IsChainCompatibleTiledMode(TiledTextureShaderMode mode) {
  return mode == TiledTextureShaderMode::None || mode == TiledTextureShaderMode::Clamp ||
         mode == TiledTextureShaderMode::RepeatNearestNone ||
         mode == TiledTextureShaderMode::RepeatLinearNone ||
         mode == TiledTextureShaderMode::MirrorRepeat ||
         mode == TiledTextureShaderMode::ClampToBorderNearest ||
         mode == TiledTextureShaderMode::ClampToBorderLinear;
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
// excluded) and the graph layout's own profile resolution, so the planner, CanExecute and
// construction cannot count differently.
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
  DrawIR ir = {};
  ir.color = graph;
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!ChainProgramFits(ir, *pass, &profile)) {
    return false;
  }
  pass->resourceProfile = profile;
  return true;
}

// Planner for a pointwise DAG whose only leaves are texture sources / const colors and whose
// interior nodes are pure pointwise or blend ops. Such a DAG evaluates in a single fused pass (the
// chain kernel) with no intermediate materialization. Resolved plain, tiled and device-space
// texture leaves are supported; non-pointwise nodes require a different planner. A noise-free
// graph admits its whole node space (a dead node would still be declined by the layout's op
// admission); a noise graph keeps to the root-reachable set, which is all the noise family's
// budgets cover.
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
    // The chain walk must be able to express the operator — the same admission the layout runs.
    int op = ChainISA::None;
    if (!BuildPointwiseSlot(graph, node, &op)) {
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

// The memoization both consumers share: the program lives on the draw's IR, the first caller that
// needs it builds it, and every later caller — the matcher's second prepare, the writer on a
// program-cache hit — reads it back.
const AOTProgram* AOTChainBuilder::ResolveChainProgram(const ProgramInfo* programInfo) {
  auto* carried = programInfo != nullptr ? programInfo->getCarriedIR() : nullptr;
  if (carried == nullptr) {
    return nullptr;
  }
  if (!carried->chainProgram.has_value()) {
    // The draw's record-time IR is the only input: every draw lowers once at record time, so a
    // missing IR means the expression is not expressible and no second lowering could fix that.
    // The whole expression is the pass: the fused form is derived from the graph itself.
    AOTProgram program = {};
    if (!BuildChainProgram(*carried, nullptr, &program)) {
      return nullptr;
    }
    carried->chainProgram = std::move(program);
  }
  return &*carried->chainProgram;
}
}  // namespace tgfx
