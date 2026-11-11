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

#include "gpu/aot/plan/AOTEffectDecomposer.h"
#include <algorithm>
#include "gpu/ProgramInfo.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/ir/Lowering.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/plan/AOTPlanner.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {

// The multi-pass source, the unary pointwise set and the const-operand blend are shared shape
// rules (gpu/AOTShapeRules.h) — the same authority the executor's admission checks and the chain
// builder consume.

// The placeholder a cut's replacement stands in as during planning: device sampling, identity
// matrix, no perspective — the facts the fit depends on — with no texture behind it yet.
static AOTTextureParameters CutIntermediatePlaceholder() {
  AOTTextureParameters parameters = {};
  parameters.isIntermediate = true;
  parameters.samplingKind = AOTTextureSamplingKind::Device;
  parameters.uvMatrix = Matrix::I();
  return parameters;
}

// Whether a multi-pass plan may start a new pass at this node. A pass boundary stores the previous
// pass's output in an RGBA8 target, rounding it to the 1/255 grid. Most operators move by at most
// that rounding, but a pass-boundary-sensitive operator (a step function — AlphaThreshold today,
// per its row on the op list) can flip: an input just below the threshold can round to a grid
// point just above it. Such an operator always shares a pass with the one producing its input.
static bool CanStartPassAt(const AOTEffectNode* node) {
  return node != nullptr && !IsPassBoundarySensitive(node->kind);
}

// Whether the linear chain's operator run [firstOperator, passEnd) forms one chain pass, with the
// pass's head standing in as the source (the first pass) or a placeholder cut edge (later passes):
// the same segment build construction runs.
static bool FitsLinearPass(const AOTEffectGraph& graph, const AOTTextureParameters& intermediate,
                           uint32_t firstOperator, uint32_t passEnd, bool firstPass) {
  PassIR candidate = {};
  candidate.output = AOTNodeID(passEnd - 1);
  if (firstPass) {
    candidate.nodes.push_back(AOTNodeID(1));
  } else {
    candidate.materializedInputs.push_back(AOTNodeID(firstOperator - 1));
  }
  for (auto index = firstOperator; index < passEnd; ++index) {
    candidate.nodes.push_back(AOTNodeID(index));
  }
  std::vector<AOTTextureParameters> placeholders(candidate.materializedInputs.size(), intermediate);
  AOTEffectGraph segment = {};
  PassIR segmentPass = {};
  return AOTChainBuilder::BuildCutSegment(graph, candidate, placeholders, Matrix::I(), Matrix::I(),
                                          &segment, &segmentPass);
}

// Planner for a linear chain one fused pass cannot carry: GeometryColor -> plain or device-space
// TextureSource -> unary pointwise ops, with more operators than the chain kernel has instructions
// or more color-space transforms than its parameter block holds. The chain is cut into chain-kernel
// passes, each as long as fits (FitsLinearPass, the same segment build construction runs), every
// later pass reading its predecessor's materialized output through a cut-style materializedInputs
// edge. A pass ends early when the next one would otherwise start at a node that may not start a
// pass (CanStartPassAt); the chain is refused when no boundary is left.
static bool DecomposeLinearMultiPass(const AOTEffectGraph& graph, Plan* plan) {
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
  // A later pass reads an intermediate that does not exist yet; the placeholder carries the facts
  // the fit depends on, exactly like a blend cut's admission.
  auto intermediate = CutIntermediatePlaceholder();
  Plan result = {};
  const auto nodeCount = static_cast<uint32_t>(graph.nodeCount());
  uint32_t firstOperator = 2;
  while (true) {
    bool firstPass = result.passes.empty();
    if (firstPass && !FitsLinearPass(graph, intermediate, firstOperator, firstOperator, true)) {
      return false;
    }
    // One past the last node of this pass.
    auto passEnd = firstOperator;
    while (passEnd < nodeCount &&
           FitsLinearPass(graph, intermediate, firstOperator, passEnd + 1, firstPass)) {
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
    PassIR pass = {};
    if (firstPass) {
      pass.nodes.push_back(AOTNodeID(1));
    } else {
      pass.materializedInputs.push_back(AOTNodeID(firstOperator - 1));
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

static bool DecomposePointwiseDAG(const AOTEffectGraph& graph, Plan* plan) {
  if (plan == nullptr) {
    return false;
  }
  PassIR pass = {};
  if (!AOTChainBuilder::PlanFusedPass(graph, &pass)) {
    return false;
  }
  Plan result = {};
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

// Plans one self-contained pass of a cut plan over the subtree at root, with the cut roots
// standing in as placeholder texture leaves. Fills the original-graph node list (designators
// excluded — they are copied but never listed), the output, the cut edges the subtree actually
// reads (materializedInputs/dependencies, in cut order), and the kernel and resource profile the
// segment resolves to. Runs the same segment build construction uses, so this is the real
// admission: plan-time fit and construction fit cannot drift.
static bool PlanCutPass(const AOTEffectGraph& graph, AOTNodeID root,
                        const std::vector<AOTNodeID>& cutRoots, PassIR* pass) {
  if (pass == nullptr) {
    return false;
  }
  std::vector<bool> reachable(graph.nodeCount(), false);
  if (!CollectReachableStoppingAtCuts(graph, root, cutRoots, &reachable)) {
    return false;
  }
  PassIR candidate = {};
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
  PassIR segmentPass = {};
  if (!AOTChainBuilder::BuildCutSegment(graph, candidate, placeholders, Matrix::I(), Matrix::I(),
                                        &segment, &segmentPass)) {
    return false;
  }
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
                        PassIR* operandPass) {
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

static bool DecomposeMaterializedBlend(const AOTEffectGraph& graph, Plan* plan) {
  if (plan == nullptr || graph.nodeCount() < 2) {
    return false;
  }
  auto geometryNode = graph.nodeAt(AOTNodeID(0));
  if (geometryNode == nullptr || geometryNode->kind != AOTEffectKind::GeometryColor) {
    return false;
  }
  Plan result = {};
  std::vector<AOTNodeID> cutRoots = {};
  while (true) {
    PassIR mainPass = {};
    if (PlanCutPass(graph, graph.root(), cutRoots, &mainPass)) {
      result.passes.push_back(std::move(mainPass));
      result.output = graph.root();
      *plan = std::move(result);
      return true;
    }
    if (result.passes.size() + 1 >= MaxCutPlanPasses) {
      return false;
    }
    PassIR operandPass = {};
    if (!FindNextCut(graph, cutRoots, &operandPass)) {
      return false;
    }
    operandPass.materializesOutput = true;
    cutRoots.push_back(operandPass.output);
    result.passes.push_back(std::move(operandPass));
  }
}

bool AOTEffectDecomposer::DecomposeMultiPass(const AOTEffectGraph& graph, Plan* plan) {
  // Only plans that materialize: a graph one fused pass can carry never reaches these planners
  // (PlanFusedPass is the only source of a single-pass plan), so a multi-pass plan here always
  // has at least two passes and always needs a task.
  if (plan == nullptr) {
    return false;
  }
  using Planner = bool (*)(const AOTEffectGraph&, Plan*);
  for (Planner planner : {DecomposeMaterializedBlend, DecomposeLinearMultiPass}) {
    Plan candidate = {};
    if (!planner(graph, &candidate)) {
      continue;
    }
    // Each planner's own admission subsumes what AOTPlanner::Validate would re-derive: the linear
    // planner's fit calls BuildCutSegment with the same placeholder admission the planner's
    // validation relies on, and the cut planner runs the same segment build construction uses over
    // placeholder intermediates.
    *plan = std::move(candidate);
    return true;
  }
  return false;
}

bool AOTEffectDecomposer::Decompose(const AOTEffectGraph& graph, Plan* plan) {
  // Pointwise DAGs (noise and gradient sources included) run on the chain family within its
  // budgets, without materialization. A linear chain over one chain pass's budget splits into
  // several chain passes; YUV sources keep their dedicated family and never plan here.
  if (DecomposePointwiseDAG(graph, plan)) {
    return true;
  }
  return DecomposeMultiPass(graph, plan);
}

}  // namespace tgfx
