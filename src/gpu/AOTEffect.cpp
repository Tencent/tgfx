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

#include "gpu/AOTEffect.h"

namespace tgfx {

const AOTEffectNode* AOTEffectGraph::nodeAt(AOTNodeID nodeID) const {
  if (!nodeID.isValid() || nodeID.index() >= nodes.size()) {
    return nullptr;
  }
  return &nodes[nodeID.index()];
}

bool AOTNodeBuilder::addGeometryColor(AOTNodeID* output) {
  if (output == nullptr || !nodes.empty()) {
    return false;
  }
  AOTEffectNode node = {};
  node.kind = AOTEffectKind::GeometryColor;
  node.traits = {EffectDomain::Pointwise, EffectInputUsage::Ignore, true, true, true};
  nodes.push_back(std::move(node));
  *output = AOTNodeID(0);
  return true;
}

bool AOTNodeBuilder::addTextureSource(AOTNodeID input, const AOTTextureParameters& parameters,
                                      AOTNodeID* output) {
  if (parameters.textureProxy == nullptr) {
    return false;
  }
  EffectTraits traits = {
      EffectDomain::Pointwise,
      parameters.isAlphaOnly ? EffectInputUsage::ColorRGBA : EffectInputUsage::ColorAlpha, false,
      false, true};
  return addUnaryNode(AOTEffectKind::TextureSource, input, traits, parameters, output);
}

bool AOTNodeBuilder::addColorMatrix(AOTNodeID input, const AOTColorMatrixParameters& parameters,
                                    AOTNodeID* output) {
  EffectTraits traits = {EffectDomain::Pointwise, EffectInputUsage::ColorRGBA, false, false, true};
  return addUnaryNode(AOTEffectKind::ColorMatrix, input, traits, parameters, output);
}

bool AOTNodeBuilder::addLuma(AOTNodeID input, const AOTLumaParameters& parameters,
                             AOTNodeID* output) {
  EffectTraits traits = {EffectDomain::Pointwise, EffectInputUsage::ColorRGBA, false, false, true};
  return addUnaryNode(AOTEffectKind::Luma, input, traits, parameters, output);
}

bool AOTNodeBuilder::finish(AOTNodeID root, AOTEffectGraph* graph) const {
  if (graph == nullptr || !contains(root)) {
    return false;
  }
  for (size_t index = 0; index < nodes.size(); ++index) {
    for (auto input : nodes[index].inputs) {
      if (!input.isValid() || input.index() >= index) {
        return false;
      }
    }
  }
  AOTEffectGraph result = {};
  result.nodes = nodes;
  result.rootNode = root;
  *graph = std::move(result);
  return true;
}

bool AOTNodeBuilder::addUnaryNode(AOTEffectKind kind, AOTNodeID input, EffectTraits traits,
                                  AOTEffectParameters parameters, AOTNodeID* output) {
  if (output == nullptr || !contains(input)) {
    return false;
  }
  AOTEffectNode node = {};
  node.kind = kind;
  node.inputs.push_back(input);
  node.traits = traits;
  node.parameters = std::move(parameters);
  auto nodeID = AOTNodeID(static_cast<uint32_t>(nodes.size()));
  nodes.push_back(std::move(node));
  *output = nodeID;
  return true;
}

bool AOTNodeBuilder::contains(AOTNodeID nodeID) const {
  return nodeID.isValid() && nodeID.index() < nodes.size();
}

}  // namespace tgfx
