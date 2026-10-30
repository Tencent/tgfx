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

#include "gpu/AOTShapeRules.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/RectEffect.h"

namespace tgfx {

// The single authority for the pointwise shapes shared by the planners (AOTEffectDecomposer), the
// executor's admission checks (AOTPlanExecutor) and the chain builder: before this file each of
// them carried its own copy, and the copies had already drifted.

bool IsDeviceSpaceAARectEffect(const FragmentProcessor* fp) {
  if (fp == nullptr || !IsProcessor<RectEffect>(fp) || fp->numChildProcessors() != 0) {
    return false;
  }
  auto rectEffect = static_cast<const RectEffect*>(fp);
  return rectEffect->isDeviceSpaceRect() && rectEffect->isAntiAlias();
}

bool IsUnaryPointwiseKind(AOTEffectKind kind) {
  return kind == AOTEffectKind::ColorMatrix || kind == AOTEffectKind::Luma ||
         kind == AOTEffectKind::AlphaThreshold || kind == AOTEffectKind::ColorSpaceXform;
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

bool IsPerlinNoiseSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  return node != nullptr && node->kind == AOTEffectKind::PerlinNoiseSource &&
         node->inputs.size() == 1 && node->inputs[0] == AOTNodeID(0);
}

bool IsYUVChainSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  if (node == nullptr || node->kind != AOTEffectKind::TextureSource || node->inputs.size() != 1 ||
      node->inputs[0] != AOTNodeID(0)) {
    return false;
  }
  auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
  return parameters != nullptr && parameters->isYUV && !parameters->hasRGBAAA &&
         parameters->samplingKind == AOTTextureSamplingKind::Plain;
}

bool IsConstOperandBlend(const AOTEffectGraph& graph, const AOTEffectNode* blend,
                         AOTNodeID expectedInput) {
  if (blend == nullptr || blend->kind != AOTEffectKind::Blend || blend->inputs.size() != 2) {
    return false;
  }
  auto blendParams = std::get_if<AOTBlendParameters>(&blend->parameters);
  // A two-child blend needs both operands evaluated, which a single const-operand slot cannot
  // express.
  if (blendParams == nullptr || blendParams->childType == 2) {
    return false;
  }
  auto* srcNode = graph.nodeAt(blend->inputs[0]);
  auto* dstNode = graph.nodeAt(blend->inputs[1]);
  bool srcIsConst = srcNode != nullptr && srcNode->kind == AOTEffectKind::ConstColor;
  bool dstIsConst = dstNode != nullptr && dstNode->kind == AOTEffectKind::ConstColor;
  AOTNodeID chained = srcIsConst ? blend->inputs[1] : blend->inputs[0];
  return srcIsConst != dstIsConst && chained == expectedInput &&
         AOTConstColorIsInputIndependent(graph, srcIsConst ? srcNode : dstNode);
}

bool IsInputDesignator(AOTEffectKind kind) {
  return kind == AOTEffectKind::GeometryColor || kind == AOTEffectKind::GeometryCoverage ||
         kind == AOTEffectKind::GeometryColorOpaqueInput ||
         kind == AOTEffectKind::GeometryWhiteInput;
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
