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

bool AOTNodeBuilder::addAlphaThreshold(AOTNodeID input,
                                       const AOTAlphaThresholdParameters& parameters,
                                       AOTNodeID* output) {
  // A step on the input alpha. It unpremultiplies, thresholds alpha, then leaves the result
  // unpremultiplied-by-the-new-alpha, so it does not preserve the alpha representation of its input.
  EffectTraits traits = {EffectDomain::Pointwise, EffectInputUsage::ColorRGBA, false, false, true};
  return addUnaryNode(AOTEffectKind::AlphaThreshold, input, traits, parameters, output);
}

bool AOTNodeBuilder::addColorSpaceXform(AOTNodeID input,
                                        const AOTColorSpaceXformParameters& parameters,
                                        AOTNodeID* output) {
  if (parameters.steps == nullptr) {
    return false;
  }
  // The transform moves the color between color spaces and may unpremultiply/repremultiply around
  // the transfer functions, so it preserves neither the alpha representation nor the color space.
  EffectTraits traits = {EffectDomain::Pointwise, EffectInputUsage::ColorRGBA, false, false, false};
  return addUnaryNode(AOTEffectKind::ColorSpaceXform, input, traits, parameters, output);
}

bool AOTNodeBuilder::addConstColor(AOTNodeID input, const AOTConstColorParameters& parameters,
                                   AOTNodeID* output) {
  // A constant color operand. Ignore mode is a self-contained source (it disregards its input);
  // the modulate modes multiply the input, so conservatively mark it as consuming the input color
  // and neither preserving alpha representation nor color space. It is not fusion-safe on its own
  // (Ignore produces non-zero output from transparent-black input), which the fusion validator and
  // the eventual pointwise kernel account for; recording accurate traits here keeps those decisions
  // sound.
  auto inputUsage = parameters.inputMode == 0
                        ? EffectInputUsage::Ignore
                        : (parameters.inputMode == 2 ? EffectInputUsage::ColorAlpha
                                                     : EffectInputUsage::ColorRGBA);
  bool selfContained = parameters.inputMode == 0;
  EffectTraits traits = {EffectDomain::Pointwise, inputUsage, selfContained, false, false};
  return addUnaryNode(AOTEffectKind::ConstColor, input, traits, parameters, output);
}

bool AOTNodeBuilder::addBlend(AOTNodeID src, AOTNodeID dst, const AOTBlendParameters& parameters,
                              AOTNodeID* output) {
  // A binary blend is a Composite node: both operands are sampled at the output coordinate. It does
  // not preserve alpha representation or color space (the blend math mixes premultiplied operands),
  // so downstream fusion/materialization decisions must treat it as a boundary.
  EffectTraits traits = {EffectDomain::Composite, EffectInputUsage::SameCoordinateChild, false,
                         false, false};
  return addBinaryNode(AOTEffectKind::Blend, src, dst, traits, parameters, output);
}

bool AOTNodeBuilder::addPerlinNoiseSource(AOTNodeID input,
                                          const AOTPerlinNoiseParameters& parameters,
                                          AOTNodeID* output) {
  if (parameters.permutationsView == nullptr || parameters.noiseView == nullptr) {
    return false;
  }
  // The kernel ignores its input color entirely (perlin_noise.frag has no Color uniform), so this
  // is a self-contained source, matching ConstColor's Ignore-mode traits.
  EffectTraits traits = {EffectDomain::Pointwise, EffectInputUsage::Ignore, true, false, false};
  return addUnaryNode(AOTEffectKind::PerlinNoiseSource, input, traits, parameters, output);
}

bool AOTNodeBuilder::addRectCoverage(AOTNodeID input, const AOTRectCoverageParameters& parameters,
                                     AOTNodeID* output) {
  // Analytic coverage read from gl_FragCoord: pointwise and consumes the input color, but
  // multiplying alpha by the rect falloff changes the alpha representation, so mark it as
  // preserving neither that nor the color space.
  EffectTraits traits = {EffectDomain::Pointwise, EffectInputUsage::ColorRGBA, false, false, false};
  return addUnaryNode(AOTEffectKind::RectCoverage, input, traits, parameters, output);
}

bool AOTNodeBuilder::addGradientSource(AOTNodeID input, const AOTGradientParameters& parameters,
                                       AOTNodeID* output) {
  // The gradient ignores its input's color but multiplies by the input alpha at the end
  // (gradColor *= inputColor.a in the runtime emission). The premultiply step means the output
  // alpha representation differs from the input, so it is not self-contained and does not
  // preserve the alpha representation.
  EffectTraits traits = {EffectDomain::Pointwise, EffectInputUsage::ColorAlpha, false, false, true};
  return addUnaryNode(AOTEffectKind::GradientSource, input, traits, parameters, output);
}

bool AOTNodeBuilder::addGeometryCoverage(AOTNodeID* output) {
  if (output == nullptr || !nodes.empty()) {
    return false;
  }
  // The unit input of a coverage subtree: the GP's output coverage, which is what the runtime
  // coverage chain starts from. A self-contained source like the geometry color.
  AOTEffectNode node = {};
  node.kind = AOTEffectKind::GeometryCoverage;
  node.traits = {EffectDomain::Pointwise, EffectInputUsage::Ignore, true, true, true};
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

bool AOTNodeBuilder::addBinaryNode(AOTEffectKind kind, AOTNodeID first, AOTNodeID second,
                                   EffectTraits traits, AOTEffectParameters parameters,
                                   AOTNodeID* output) {
  if (output == nullptr || !contains(first) || !contains(second)) {
    return false;
  }
  AOTEffectNode node = {};
  node.kind = kind;
  node.inputs.push_back(first);
  node.inputs.push_back(second);
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
