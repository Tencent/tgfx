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

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/ir/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/aot/plan/AOTChainBuilder.h"
#include "gpu/aot/plan/AOTChainInternal.h"
#include "gpu/aot/plan/AOTChainRegisterAllocator.h"
#include "gpu/aot/plan/AOTShapeRules.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

// Maps one original-graph input edge onto the segment copy: an edge pointing at a cut root
// resolves to that cut's replacement leaf; the geometry color is every segment's own node 0 (a
// source node chained onto it in the original graph keeps that placeholder input in the copy); any
// other edge looks its node up in the copy.
static AOTNodeID ResolveCopiedInput(const std::unordered_map<uint32_t, AOTNodeID>& builtMap,
                                    const std::vector<AOTNodeID>& cutRoots,
                                    const std::vector<AOTNodeID>& cutLeaves, AOTNodeID input) {
  for (size_t index = 0; index < cutRoots.size(); ++index) {
    if (input == cutRoots[index]) {
      return cutLeaves[index];
    }
  }
  if (input == AOTNodeID(0)) {
    return AOTNodeID(0);
  }
  auto found = builtMap.find(input.index());
  return found == builtMap.end() ? AOTNodeID::Invalid() : found->second;
}

// Precomposes the coordinate matrices a copied node's parameters carry: a plain or tiled texture
// leaf's uvMatrix, a gradient's coordMatrix and a noise source's uvMatrix with coordToLocal, while
// a device-sampled texture leaf's uvMatrix is precomposed with deviceLeafRemap instead (it
// addresses the destination's positions). The device-space coverage parameters stay as they are
// (the fill's positions are the device ones). Every other parameter block carries no coordinates
// and passes through unchanged. A terminal pass passes identity for both matrices, because it runs
// on the destination and sees the original spaces.
static AOTEffectParameters RemapNodeParameters(AOTEffectKind kind, AOTEffectParameters parameters,
                                               const Matrix& coordToLocal,
                                               const Matrix& deviceLeafRemap) {
  switch (TraitsOf(kind).remap) {
    case AOTRemapKind::LocalMatrix:
      if (auto* gradient = std::get_if<AOTGradientParameters>(&parameters)) {
        gradient->coordMatrix.preConcat(coordToLocal);
      } else if (auto* noise = std::get_if<AOTPerlinNoiseParameters>(&parameters)) {
        noise->uvMatrix.preConcat(coordToLocal);
      }
      break;
    case AOTRemapKind::TextureMatrix:
      if (auto* texture = std::get_if<AOTTextureParameters>(&parameters)) {
        texture->uvMatrix.preConcat(texture->samplingKind == AOTTextureSamplingKind::Device
                                        ? deviceLeafRemap
                                        : coordToLocal);
      }
      break;
    case AOTRemapKind::None:
      break;
  }
  return parameters;
}

// Copies one chain node into the builder, resolving its input edges through builtMap (and the cut
// replacements). The source node was admitted by its own lowering when the graph was built, so the
// copy needs no kind-specific admission — only the coordinate remap above.
static bool CopyChainNode(const AOTEffectGraph& graph, AOTNodeID nodeID,
                          const std::unordered_map<uint32_t, AOTNodeID>& builtMap,
                          const std::vector<AOTNodeID>& cutRoots,
                          const std::vector<AOTNodeID>& cutLeaves, const Matrix& coordToLocal,
                          const Matrix& deviceLeafRemap, AOTNodeBuilder* builder,
                          AOTNodeID* copied) {
  const auto* node = graph.nodeAt(nodeID);
  if (node == nullptr || builder == nullptr || copied == nullptr) {
    return false;
  }
  std::vector<AOTNodeID> inputs = {};
  inputs.reserve(node->inputs.size());
  for (auto input : node->inputs) {
    auto resolved = ResolveCopiedInput(builtMap, cutRoots, cutLeaves, input);
    if (!resolved.isValid()) {
      return false;
    }
    inputs.push_back(resolved);
  }
  return builder->addCopy(
      *node, inputs,
      RemapNodeParameters(node->kind, node->parameters, coordToLocal, deviceLeafRemap), copied);
}

// Copies the wanted node set into the builder in original-graph order — the graph is SSA, so every
// input precedes its consumer and each copy's inputs are always already built. Designators are
// always copied (a pass shares them with its cut subtrees), nodes in skip never are. The first
// consumer referencing a cut root builds that cut's replacement texture leaf right before itself
// (fed from the opaque-white designator so the intermediate samples raw: the producing pass
// already applied its own modulation, and a geometry-color input would fold the paint alpha in a
// second time).
static bool CopyChainNodes(const AOTEffectGraph& graph, const std::vector<bool>& wanted,
                           const std::vector<bool>& skip, const std::vector<AOTNodeID>& cutRoots,
                           const std::vector<AOTTextureParameters>& intermediates,
                           const Matrix& coordToLocal, const Matrix& deviceLeafRemap,
                           AOTNodeBuilder* builder,
                           std::unordered_map<uint32_t, AOTNodeID>* builtMap) {
  if (cutRoots.size() != intermediates.size()) {
    return false;
  }
  std::vector<AOTNodeID> cutLeaves(cutRoots.size(), AOTNodeID::Invalid());
  for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
    auto* node = graph.nodeAt(AOTNodeID(index));
    if (node == nullptr) {
      return false;
    }
    bool isDesignator = TraitsOf(node->kind).isInputDesignator();
    if ((!wanted[index] && !isDesignator) || (skip[index] && !isDesignator)) {
      continue;
    }
    for (size_t cut = 0; cut < cutRoots.size(); ++cut) {
      if (cutLeaves[cut].isValid()) {
        continue;
      }
      bool references = false;
      for (auto input : node->inputs) {
        references |= input == cutRoots[cut];
      }
      if (!references) {
        continue;
      }
      AOTNodeID white = AOTNodeID::Invalid();
      if (!builder->addGeometryWhiteInput(&white) ||
          !builder->addTextureSource(white, intermediates[cut], &cutLeaves[cut])) {
        return false;
      }
    }
    AOTNodeID copied = AOTNodeID::Invalid();
    if (!CopyChainNode(graph, AOTNodeID(index), *builtMap, cutRoots, cutLeaves, coordToLocal,
                       deviceLeafRemap, builder, &copied)) {
      return false;
    }
    (*builtMap)[index] = copied;
  }
  return true;
}

// Finishes the builder's graph at the mapped root and resolves the segment pass over the copy:
// the node list in the caller's order, the profile and the kernel the segment's own graph settles
// on — which is the pass's real fit.
static bool FinishSegmentPass(const AOTNodeBuilder& builder, AOTNodeID root,
                              const std::unordered_map<uint32_t, AOTNodeID>& builtMap,
                              AOTEffectGraph* segment, PassIR* pass) {
  auto rootMapped = ResolveCopiedInput(builtMap, {}, {}, root);
  if (!rootMapped.isValid()) {
    return false;
  }
  AOTEffectGraph built = {};
  if (!builder.finish(rootMapped, &built)) {
    return false;
  }
  PassIR resultPass = {};
  resultPass.output = rootMapped;
  // The pass node list is every non-designator node of the segment graph itself — the copy may
  // have added nodes the caller did not list (the replacement texture leaf), so every
  // non-designator node of the segment graph itself is listed.
  for (uint32_t index = 1; index < built.nodeCount(); ++index) {
    auto* node = built.nodeAt(AOTNodeID(index));
    if (node == nullptr || node->kind == AOTEffectKind::GeometryColorOpaqueInput ||
        node->kind == AOTEffectKind::GeometryWhiteInput) {
      continue;
    }
    resultPass.nodes.push_back(AOTNodeID(index));
  }
  AOTResourceProfile profile = AOTResourceProfile::Automatic;
  if (!AOTChainBuilder::FitsChainPlan(built, resultPass, false, &profile)) {
    return false;
  }
  resultPass.resourceProfile = profile;
  resultPass.kernel =
      IsAOTGraphProfile(profile) ? AOTKernelKind::PointwiseGraph : AOTKernelKind::PointwiseChain;
  *segment = std::move(built);
  *pass = std::move(resultPass);
  return true;
}

bool AOTChainBuilder::BuildCutSegment(const AOTEffectGraph& graph, const PassIR& cutPass,
                                      const std::vector<AOTTextureParameters>& intermediates,
                                      const Matrix& coordToLocal, const Matrix& deviceLeafRemap,
                                      AOTEffectGraph* segment, PassIR* pass) {
  if (segment == nullptr || pass == nullptr || cutPass.nodes.empty() ||
      cutPass.materializedInputs.size() != intermediates.size()) {
    return false;
  }
  std::vector<bool> skip(graph.nodeCount(), false);
  for (auto cut : cutPass.materializedInputs) {
    if (!cut.isValid() || cut.index() >= graph.nodeCount()) {
      return false;
    }
    std::vector<bool> cutReachable(graph.nodeCount(), false);
    if (!CollectReachableNodes(graph, cut, &cutReachable)) {
      return false;
    }
    for (uint32_t index = 1; index < graph.nodeCount(); ++index) {
      skip[index] = skip[index] || cutReachable[index];
    }
  }
  std::vector<bool> wanted(graph.nodeCount(), false);
  for (auto nodeID : cutPass.nodes) {
    if (!nodeID.isValid() || nodeID.index() >= graph.nodeCount() || skip[nodeID.index()]) {
      return false;
    }
    wanted[nodeID.index()] = true;
  }
  AOTNodeBuilder builder = {};
  AOTNodeID geometry = AOTNodeID::Invalid();
  if (!builder.addGeometryColor(&geometry)) {
    return false;
  }
  std::unordered_map<uint32_t, AOTNodeID> builtMap = {};
  if (!CopyChainNodes(graph, wanted, skip, cutPass.materializedInputs, intermediates, coordToLocal,
                      deviceLeafRemap, &builder, &builtMap)) {
    return false;
  }
  return FinishSegmentPass(builder, cutPass.output, builtMap, segment, pass);
}

}  // namespace tgfx
