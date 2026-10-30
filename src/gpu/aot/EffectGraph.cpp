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

#include "gpu/aot/EffectGraph.h"
#include <cmath>
#include <cstring>
#include "gpu/ColorSpaceXformHelper.h"
#include "gpu/resources/TextureView.h"

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
  nodes.push_back(std::move(node));
  *output = AOTNodeID(0);
  return true;
}

bool AOTNodeBuilder::addGeometryColorOpaqueInput(AOTNodeID* output) {
  if (output == nullptr || nodes.empty()) {
    return false;
  }
  AOTEffectNode node = {};
  node.kind = AOTEffectKind::GeometryColorOpaqueInput;
  *output = AOTNodeID(static_cast<uint32_t>(nodes.size()));
  nodes.push_back(std::move(node));
  return true;
}

bool AOTNodeBuilder::addGeometryWhiteInput(AOTNodeID* output) {
  if (output == nullptr || nodes.empty()) {
    return false;
  }
  AOTEffectNode node = {};
  node.kind = AOTEffectKind::GeometryWhiteInput;
  *output = AOTNodeID(static_cast<uint32_t>(nodes.size()));
  nodes.push_back(std::move(node));
  return true;
}

bool AOTNodeBuilder::addTextureSource(AOTNodeID input, const AOTTextureParameters& parameters,
                                      AOTNodeID* output) {
  // An isIntermediate stand-in carries no texture yet (planning admits a cut's replacement before
  // the producing pass's target exists); every other leaf needs its proxy.
  if (parameters.textureProxy == nullptr && !parameters.isIntermediate) {
    return false;
  }
  return addUnaryNode(AOTEffectKind::TextureSource, input, parameters, output);
}

bool AOTNodeBuilder::addColorMatrix(AOTNodeID input, const AOTColorMatrixParameters& parameters,
                                    AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::ColorMatrix, input, parameters, output);
}

bool AOTNodeBuilder::addLuma(AOTNodeID input, const AOTLumaParameters& parameters,
                             AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::Luma, input, parameters, output);
}

bool AOTNodeBuilder::addAlphaThreshold(AOTNodeID input,
                                       const AOTAlphaThresholdParameters& parameters,
                                       AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::AlphaThreshold, input, parameters, output);
}

bool AOTNodeBuilder::addColorSpaceXform(AOTNodeID input,
                                        const AOTColorSpaceXformParameters& parameters,
                                        AOTNodeID* output) {
  // The shared admission gate (single source with the matcher and the owned-slot factories): an
  // unsupported transfer-function kind fails the whole lowering here — before any tree rewrite or
  // program creation — instead of surfacing at the upload path, which can only log and render the
  // sRGB branch.
  if (!ColorSpaceXformStepsSupported(parameters.steps.get())) {
    return false;
  }
  return addUnaryNode(AOTEffectKind::ColorSpaceXform, input, parameters, output);
}

bool AOTNodeBuilder::addConstColor(AOTNodeID input, const AOTConstColorParameters& parameters,
                                   AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::ConstColor, input, parameters, output);
}

bool AOTNodeBuilder::addBlend(AOTNodeID src, AOTNodeID dst, const AOTBlendParameters& parameters,
                              AOTNodeID* output) {
  return addBinaryNode(AOTEffectKind::Blend, src, dst, parameters, output);
}

static bool PerlinLookupViewsCompatible(const std::shared_ptr<TextureView>& first,
                                        const std::shared_ptr<TextureView>& second) {
  if (first == nullptr || second == nullptr || first->getContext() == nullptr ||
      first->getContext() != second->getContext() || first->isYUV() || second->isYUV()) {
    return false;
  }
  const auto firstTexture = first->getTexture();
  const auto secondTexture = second->getTexture();
  return firstTexture != nullptr && secondTexture != nullptr &&
         firstTexture->type() == TextureType::TwoD && secondTexture->type() == TextureType::TwoD &&
         firstTexture->format() == secondTexture->format() && firstTexture->sampleCount() == 1 &&
         secondTexture->sampleCount() == 1 && first->width() == second->width() &&
         first->height() == second->height() && first->origin() == second->origin() &&
         !first->hasMipmaps() && !second->hasMipmaps();
}

bool AOTPerlinNoiseParametersSupported(const AOTPerlinNoiseParameters& parameters) {
  const auto& permutation = parameters.permutationsView;
  const auto& noise = parameters.noiseView;
  if (!PerlinLookupViewsCompatible(permutation, permutation) ||
      !PerlinLookupViewsCompatible(noise, noise) ||
      permutation->getContext() != noise->getContext() || permutation->width() != 256 ||
      permutation->height() != 1 || !permutation->isAlphaOnly() || noise->width() != 256 ||
      noise->height() != 4 || noise->getTexture()->format() != PixelFormat::RGBA_8888 ||
      permutation->origin() != ImageOrigin::TopLeft || noise->origin() != ImageOrigin::TopLeft ||
      parameters.noiseType < 0 || parameters.noiseType > 1 || parameters.numOctaves < 1 ||
      parameters.numOctaves > 255 || !std::isfinite(parameters.baseFrequencyX) ||
      !std::isfinite(parameters.baseFrequencyY) ||
      (parameters.stitchTiles && (parameters.stitchWidth <= 0 || parameters.stitchHeight <= 0))) {
    return false;
  }
  for (int index = 0; index < 9; ++index) {
    if (!std::isfinite(parameters.uvMatrix[index])) {
      return false;
    }
  }
  return true;
}

bool AOTPerlinNoiseLookupsEquivalent(const AOTPerlinNoiseParameters& first,
                                     const AOTPerlinNoiseParameters& second) {
  if (!PerlinLookupViewsCompatible(first.permutationsView, second.permutationsView) ||
      !PerlinLookupViewsCompatible(first.noiseView, second.noiseView) ||
      first.permutationsView->getContext() != first.noiseView->getContext()) {
    return false;
  }
  if (first.permutationsView == second.permutationsView && first.noiseView == second.noiseView) {
    return true;
  }
  return first.lookupData != nullptr && second.lookupData != nullptr &&
         first.permutationsView->width() == 256 && first.permutationsView->height() == 1 &&
         first.permutationsView->isAlphaOnly() && first.noiseView->width() == 256 &&
         first.noiseView->height() == 4 &&
         first.noiseView->getTexture()->format() == PixelFormat::RGBA_8888 &&
         first.lookupData->permutations == second.lookupData->permutations &&
         first.lookupData->noise == second.lookupData->noise;
}

static bool PerlinFloatBitsEqual(float first, float second) {
  return std::isfinite(first) && std::isfinite(second) &&
         std::memcmp(&first, &second, sizeof(float)) == 0;
}

bool AOTPerlinNoiseComputationsEquivalent(const AOTPerlinNoiseParameters& first,
                                          const AOTPerlinNoiseParameters& second) {
  if (!AOTPerlinNoiseLookupsEquivalent(first, second) || first.noiseType != second.noiseType ||
      first.numOctaves != second.numOctaves || first.stitchTiles != second.stitchTiles ||
      first.stitchWidth != second.stitchWidth || first.stitchHeight != second.stitchHeight ||
      !PerlinFloatBitsEqual(first.baseFrequencyX, second.baseFrequencyX) ||
      !PerlinFloatBitsEqual(first.baseFrequencyY, second.baseFrequencyY)) {
    return false;
  }
  for (int index = 0; index < 9; ++index) {
    if (!PerlinFloatBitsEqual(first.uvMatrix[index], second.uvMatrix[index])) {
      return false;
    }
  }
  return true;
}

bool AOTNodeBuilder::addPerlinNoiseSource(AOTNodeID input,
                                          const AOTPerlinNoiseParameters& parameters,
                                          AOTNodeID* output) {
  if (output == nullptr || !contains(input) || parameters.permutationsView == nullptr ||
      parameters.noiseView == nullptr) {
    return false;
  }
  auto canonical = parameters;
  for (size_t index = 0; index < nodes.size(); ++index) {
    const auto& node = nodes[index];
    const auto* existing = std::get_if<AOTPerlinNoiseParameters>(&node.parameters);
    if (node.kind != AOTEffectKind::PerlinNoiseSource || existing == nullptr) {
      continue;
    }
    if (AOTPerlinNoiseComputationsEquivalent(*existing, canonical)) {
      *output = AOTNodeID(static_cast<uint32_t>(index));
      return true;
    }
    if (AOTPerlinNoiseLookupsEquivalent(*existing, canonical)) {
      canonical.permutationsView = existing->permutationsView;
      canonical.noiseView = existing->noiseView;
      canonical.lookupData = existing->lookupData;
    }
  }
  return addUnaryNode(AOTEffectKind::PerlinNoiseSource, input, std::move(canonical), output);
}

bool AOTNodeBuilder::addRectCoverage(AOTNodeID input, const AOTRectCoverageParameters& parameters,
                                     AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::RectCoverage, input, parameters, output);
}

bool AOTNodeBuilder::addRRectCoverage(AOTNodeID input, const AOTRRectCoverageParameters& parameters,
                                      AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::RRectCoverage, input, parameters, output);
}

bool AOTNodeBuilder::addGradientSource(AOTNodeID input, const AOTGradientParameters& parameters,
                                       AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::GradientSource, input, parameters, output);
}

bool AOTNodeBuilder::addInputOpaque(AOTNodeID input, AOTNodeID* output) {
  return addUnaryNode(AOTEffectKind::InputOpaque, input, {}, output);
}

bool AOTNodeBuilder::addMulAlpha(AOTNodeID value, AOTNodeID alphaSource, AOTNodeID* output) {
  return addBinaryNode(AOTEffectKind::MulAlpha, value, alphaSource, {}, output);
}

bool AOTNodeBuilder::addGeometryCoverage(AOTNodeID* output) {
  if (output == nullptr || !nodes.empty()) {
    return false;
  }
  // The unit input of a coverage subtree: the GP's output coverage, which is what the runtime
  // coverage chain starts from.
  AOTEffectNode node = {};
  node.kind = AOTEffectKind::GeometryCoverage;
  nodes.push_back(std::move(node));
  *output = AOTNodeID(0);
  return true;
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

bool AOTNodeBuilder::addUnaryNode(AOTEffectKind kind, AOTNodeID input,
                                  AOTEffectParameters parameters, AOTNodeID* output) {
  if (output == nullptr || !contains(input)) {
    return false;
  }
  AOTEffectNode node = {};
  node.kind = kind;
  node.inputs.push_back(input);
  node.parameters = std::move(parameters);
  auto nodeID = AOTNodeID(static_cast<uint32_t>(nodes.size()));
  nodes.push_back(std::move(node));
  *output = nodeID;
  return true;
}

bool AOTNodeBuilder::addBinaryNode(AOTEffectKind kind, AOTNodeID first, AOTNodeID second,
                                   AOTEffectParameters parameters, AOTNodeID* output) {
  if (output == nullptr || !contains(first) || !contains(second)) {
    return false;
  }
  AOTEffectNode node = {};
  node.kind = kind;
  node.inputs.push_back(first);
  node.inputs.push_back(second);
  node.parameters = std::move(parameters);
  auto nodeID = AOTNodeID(static_cast<uint32_t>(nodes.size()));
  nodes.push_back(std::move(node));
  *output = nodeID;
  return true;
}

bool AOTNodeBuilder::contains(AOTNodeID nodeID) const {
  return nodeID.isValid() && nodeID.index() < nodes.size();
}

bool AOTConstColorIsInputIndependent(const AOTEffectGraph& graph, const AOTEffectNode* node) {
  if (node == nullptr || node->kind != AOTEffectKind::ConstColor || node->inputs.size() != 1) {
    return false;
  }
  auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
  if (parameters == nullptr) {
    return false;
  }
  // AOTConstColorParameters::inputMode mirrors tgfx::InputMode, where 0 is Ignore.
  if (parameters->inputMode == 0) {
    return true;
  }
  auto input = graph.nodeAt(node->inputs[0]);
  return input != nullptr && input->kind == AOTEffectKind::GeometryWhiteInput;
}

}  // namespace tgfx
