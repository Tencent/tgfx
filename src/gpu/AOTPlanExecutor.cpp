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

#include "AOTPlanExecutor.h"
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/BackingFit.h"
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/ops/StandardDrawOp.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AOTPointwiseTailProcessor.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/resources/RenderTarget.h"
#include "gpu/tasks/AOTPlanRenderTask.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
namespace {

static bool ValidatePointwiseTailSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  if (node == nullptr || node->kind != AOTEffectKind::TextureSource || node->inputs.size() != 1) {
    return false;
  }
  auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
  auto input = graph.nodeAt(node->inputs[0]);
  return parameters != nullptr && input != nullptr && input->kind == AOTEffectKind::GeometryColor &&
         (parameters->samplingKind == AOTTextureSamplingKind::Plain ||
          parameters->samplingKind == AOTTextureSamplingKind::Device) &&
         !parameters->isYUV && !parameters->isAlphaOnly && !parameters->hasRGBAAA &&
         !parameters->hasPerspective;
}

static bool ValidatePointwiseTailOp(const AOTEffectGraph& graph, AOTNodeID nodeID,
                                    AOTNodeID expectedInput) {
  auto node = graph.nodeAt(nodeID);
  return node != nullptr && node->inputs.size() == 1 && node->inputs[0] == expectedInput &&
         (node->kind == AOTEffectKind::ColorMatrix || node->kind == AOTEffectKind::Luma ||
          node->kind == AOTEffectKind::AlphaThreshold ||
          node->kind == AOTEffectKind::ColorSpaceXform);
}

static bool ValidatePerlinNoiseSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  if (node == nullptr || node->kind != AOTEffectKind::PerlinNoiseSource ||
      node->inputs.size() != 1) {
    return false;
  }
  auto parameters = std::get_if<AOTPerlinNoiseParameters>(&node->parameters);
  auto input = graph.nodeAt(node->inputs[0]);
  return parameters != nullptr && parameters->permutationsView != nullptr &&
         parameters->noiseView != nullptr && input != nullptr &&
         input->kind == AOTEffectKind::GeometryColor;
}

static bool ValidateLinearPlan(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  if (plan.passes.empty() || !plan.output.isValid() || plan.output != graph.root()) {
    return false;
  }
  for (size_t index = 0; index < plan.passes.size(); ++index) {
    const auto& pass = plan.passes[index];
    if (!pass.output.isValid() || pass.nodes.empty() || pass.output != pass.nodes.back() ||
        pass.materializesOutput != (index + 1 < plan.passes.size())) {
      return false;
    }
    if (pass.kernel == AOTKernelKind::PointwiseTail) {
      size_t opIndex = 0;
      AOTNodeID expectedInput = AOTNodeID::Invalid();
      if (index == 0) {
        if (!pass.dependencies.empty() || pass.nodes.size() > 3 ||
            !ValidatePointwiseTailSource(graph, pass.nodes[0])) {
          return false;
        }
        expectedInput = pass.nodes[0];
        opIndex = 1;
      } else {
        if (pass.dependencies.size() != 1 || pass.dependencies[0] != index - 1 ||
            pass.nodes.size() > 2) {
          return false;
        }
        expectedInput = plan.passes[index - 1].output;
      }
      for (; opIndex < pass.nodes.size(); ++opIndex) {
        if (!ValidatePointwiseTailOp(graph, pass.nodes[opIndex], expectedInput)) {
          return false;
        }
        expectedInput = pass.nodes[opIndex];
      }
      continue;
    }
    if (index == 0) {
      if (!pass.dependencies.empty()) {
        return false;
      }
      if (pass.kernel == AOTKernelKind::PerlinNoiseFill) {
        // One fused pass: a perlin source plus up to three pointwise-operator slots, matching the
        // three slot records the PerlinNoiseFillShader kernel carries. A slot may also be a
        // const-color op or a blend with a constant side operand.
        if (pass.nodes.empty() || pass.nodes.size() > 4 ||
            !ValidatePerlinNoiseSource(graph, pass.nodes[0])) {
          return false;
        }
        AOTNodeID expectedInput = pass.nodes[0];
        for (size_t opIndex = 1; opIndex < pass.nodes.size(); ++opIndex) {
          auto node = graph.nodeAt(pass.nodes[opIndex]);
          if (node == nullptr) {
            return false;
          }
          if (ValidatePointwiseTailOp(graph, pass.nodes[opIndex], expectedInput)) {
            expectedInput = pass.nodes[opIndex];
            continue;
          }
          if (node->kind == AOTEffectKind::ConstColor && node->inputs.size() == 1 &&
              node->inputs[0] == expectedInput) {
            expectedInput = pass.nodes[opIndex];
            continue;
          }
          if (node->kind == AOTEffectKind::Blend && node->inputs.size() == 2) {
            auto blendParams = std::get_if<AOTBlendParameters>(&node->parameters);
            auto* srcNode = graph.nodeAt(node->inputs[0]);
            auto* dstNode = graph.nodeAt(node->inputs[1]);
            bool srcIsConst = srcNode != nullptr && srcNode->kind == AOTEffectKind::ConstColor;
            bool dstIsConst = dstNode != nullptr && dstNode->kind == AOTEffectKind::ConstColor;
            AOTNodeID chained = srcIsConst ? node->inputs[1] : node->inputs[0];
            if (blendParams == nullptr || blendParams->childType == 2 || srcIsConst == dstIsConst ||
                chained != expectedInput) {
              return false;
            }
            expectedInput = pass.nodes[opIndex];
            continue;
          }
          return false;
        }
      } else {
        return false;
      }
      continue;
    }
    // The legacy linear TextureFill/TextureColorMatrix/TexturedColorMatrix/TexturedLuma kernels
    // are no longer produced by the decomposer's planners, so any non-first pass that is not a
    // PointwiseTail segment is unreachable and rejected outright.
    return false;
  }
  return plan.output == plan.passes.back().output;
}

// The chain kernel's tiled path implements the same single-tap wrap modes as tiled_sample.inc:
// Clamp(1), RepeatNearest/LinearNone(2,3), MirrorRepeat(6) and ClampToBorderNearest/Linear(7,8).
// The mipmap-repeat modes (4,5) need a 4-tap seam blend that the kernel does not provide.
static bool IsChainCompatibleTiledMode(TiledTextureShaderMode mode) {
  return mode == TiledTextureShaderMode::None || mode == TiledTextureShaderMode::Clamp ||
         mode == TiledTextureShaderMode::RepeatNearestNone ||
         mode == TiledTextureShaderMode::RepeatLinearNone ||
         mode == TiledTextureShaderMode::MirrorRepeat ||
         mode == TiledTextureShaderMode::ClampToBorderNearest ||
         mode == TiledTextureShaderMode::ClampToBorderLinear;
}

static PlacementPtr<FragmentProcessor> BuildFPForNode(BlockAllocator* allocator,
                                                      const AOTEffectNode* node,
                                                      PlacementPtr<FragmentProcessor> input) {
  switch (node->kind) {
    case AOTEffectKind::TextureSource: {
      auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
      if (parameters == nullptr) {
        return nullptr;
      }
      if (parameters->samplingKind == AOTTextureSamplingKind::Device) {
        return DeviceSpaceTextureEffect::Make(allocator, parameters->textureProxy,
                                              parameters->uvMatrix);
      }
      if (parameters->samplingKind == AOTTextureSamplingKind::Tiled) {
        const auto& recipe = parameters->tiledRecipe;
        if (!recipe.has_value() || !IsChainCompatibleTiledMode(recipe->shaderModeX) ||
            !IsChainCompatibleTiledMode(recipe->shaderModeY)) {
          return nullptr;
        }
        // The hardware sampler performs all tiling (wrap modes and clamp-to-border), so the leaf
        // is a plain TextureEffect carrying the resolved sampler state. The huge sample area makes
        // the chain kernel's always-on Subset clamp a no-op, which the wrap/border modes rely on.
        SamplingOptions sampling(recipe->hardwareSampler.minFilterMode,
                                 recipe->hardwareSampler.magFilterMode,
                                 recipe->hardwareSampler.mipmapMode);
        SamplingArgs args = {recipe->hardwareSampler.tileModeX, recipe->hardwareSampler.tileModeY,
                             sampling, SrcRectConstraint::Fast};
        args.sampleArea = Rect::MakeLTRB(-1e9f, -1e9f, 1e9f, 1e9f);
        auto uvMatrix = parameters->uvMatrix;
        return TextureEffect::Make(allocator, parameters->textureProxy, args, &uvMatrix);
      }
      if (parameters->samplingKind != AOTTextureSamplingKind::Plain) {
        return nullptr;
      }
      SamplingOptions sampling(parameters->samplerState.minFilterMode,
                               parameters->samplerState.magFilterMode,
                               parameters->samplerState.mipmapMode);
      SamplingArgs args = {parameters->samplerState.tileModeX, parameters->samplerState.tileModeY,
                           sampling, parameters->constraint};
      args.sampleArea = parameters->subset;
      auto uvMatrix = parameters->uvMatrix;
      if (parameters->hasRGBAAA) {
        return TextureEffect::MakeRGBAAA(allocator, parameters->textureProxy, args,
                                         parameters->alphaStart, &uvMatrix);
      }
      return TextureEffect::Make(allocator, parameters->textureProxy, args, &uvMatrix);
    }
    case AOTEffectKind::ColorMatrix: {
      auto parameters = std::get_if<AOTColorMatrixParameters>(&node->parameters);
      if (parameters == nullptr || input == nullptr) {
        return nullptr;
      }
      auto matrix = ColorMatrixFragmentProcessor::Make(allocator, parameters->matrix);
      return FragmentProcessor::Compose(allocator, std::move(input), std::move(matrix));
    }
    case AOTEffectKind::Luma: {
      auto parameters = std::get_if<AOTLumaParameters>(&node->parameters);
      if (parameters == nullptr || input == nullptr) {
        return nullptr;
      }
      auto luma =
          LumaFragmentProcessor::Make(allocator, parameters->kr, parameters->kg, parameters->kb);
      return FragmentProcessor::Compose(allocator, std::move(input), std::move(luma));
    }
    case AOTEffectKind::AlphaThreshold: {
      auto parameters = std::get_if<AOTAlphaThresholdParameters>(&node->parameters);
      if (parameters == nullptr || input == nullptr) {
        return nullptr;
      }
      auto step = AlphaThresholdFragmentProcessor::Make(allocator, parameters->threshold);
      return FragmentProcessor::Compose(allocator, std::move(input), std::move(step));
    }
    case AOTEffectKind::ColorSpaceXform: {
      auto parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
      if (parameters == nullptr || parameters->steps == nullptr || input == nullptr) {
        return nullptr;
      }
      auto xform = ColorSpaceXformEffect::Make(allocator, parameters->steps);
      return FragmentProcessor::Compose(allocator, std::move(input), std::move(xform));
    }
    default:
      return nullptr;
  }
}

// Rebuilds the fused perlin-source processor of a PerlinNoiseFill pass: the noise source plus up
// to two pointwise operators folded into the processor's own slot records, so the whole pass maps
// onto one PerlinNoiseFillShader program with no Compose wrapper and no materialization.
static PlacementPtr<FragmentProcessor> BuildPerlinNoiseFillFP(BlockAllocator* allocator,
                                                              const AOTEffectGraph& graph,
                                                              const AOTPassDescriptor& pass);

static bool BuildPointwiseSlot(const AOTEffectGraph& graph, const AOTEffectNode* node,
                               AOTPointwiseSlot* slot) {
  if (node == nullptr || slot == nullptr) {
    return false;
  }
  if (node->kind == AOTEffectKind::ConstColor) {
    auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
    if (parameters == nullptr || node->inputs.size() != 1) {
      return false;
    }
    slot->type = AOTPointwiseOpType::ConstColor;
    slot->constColor = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::Blend) {
    auto parameters = std::get_if<AOTBlendParameters>(&node->parameters);
    if (parameters == nullptr || parameters->childType == 2 || node->inputs.size() != 2) {
      return false;
    }
    auto* srcNode = graph.nodeAt(node->inputs[0]);
    auto* dstNode = graph.nodeAt(node->inputs[1]);
    const AOTEffectNode* constNode = nullptr;
    if (srcNode != nullptr && srcNode->kind == AOTEffectKind::ConstColor) {
      constNode = srcNode;
    } else if (dstNode != nullptr && dstNode->kind == AOTEffectKind::ConstColor) {
      constNode = dstNode;
    } else {
      return false;
    }
    auto constParams = std::get_if<AOTConstColorParameters>(&constNode->parameters);
    if (constParams == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::Blend;
    slot->blend = *parameters;
    slot->constColor = *constParams;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorMatrix) {
    auto parameters = std::get_if<AOTColorMatrixParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::ColorMatrix;
    slot->colorMatrix = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::Luma) {
    auto parameters = std::get_if<AOTLumaParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::Luma;
    slot->luma = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::AlphaThreshold) {
    auto parameters = std::get_if<AOTAlphaThresholdParameters>(&node->parameters);
    if (parameters == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::AlphaThreshold;
    slot->alphaThreshold = *parameters;
    return true;
  }
  if (node->kind == AOTEffectKind::ColorSpaceXform) {
    auto parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
    if (parameters == nullptr || parameters->steps == nullptr) {
      return false;
    }
    slot->type = AOTPointwiseOpType::ColorSpaceXform;
    slot->colorSpaceXform = *parameters;
    return true;
  }
  return false;
}

static PlacementPtr<FragmentProcessor> BuildPerlinNoiseFillFP(BlockAllocator* allocator,
                                                              const AOTEffectGraph& graph,
                                                              const AOTPassDescriptor& pass) {
  if (pass.nodes.empty() || pass.nodes.size() > 4) {
    return nullptr;
  }
  auto perlinNode = graph.nodeAt(pass.nodes[0]);
  auto parameters = perlinNode != nullptr
                        ? std::get_if<AOTPerlinNoiseParameters>(&perlinNode->parameters)
                        : nullptr;
  if (perlinNode == nullptr || perlinNode->kind != AOTEffectKind::PerlinNoiseSource ||
      parameters == nullptr) {
    return nullptr;
  }
  std::vector<AOTPointwiseSlot> slots = {};
  slots.reserve(pass.nodes.size() - 1);
  for (size_t index = 1; index < pass.nodes.size(); ++index) {
    AOTPointwiseSlot slot = {};
    if (!BuildPointwiseSlot(graph, graph.nodeAt(pass.nodes[index]), &slot)) {
      return nullptr;
    }
    slots.push_back(slot);
  }
  auto paintingData = std::make_unique<PerlinNoiseShader::PaintingData>(
      parameters->baseFrequencyX, parameters->baseFrequencyY, parameters->stitchWidth,
      parameters->stitchHeight);
  auto uvMatrix = parameters->uvMatrix;
  return PerlinNoiseFragmentProcessor::MakeFromViews(
      allocator, static_cast<PerlinNoiseType>(parameters->noiseType), parameters->numOctaves,
      parameters->stitchTiles, std::move(paintingData), parameters->permutationsView,
      parameters->noiseView, &uvMatrix, slots);
}

// Maps a DAG input edge onto a chain slot index. GeometryColor is not a slot: it maps to -1, the
// geometry color. GeometryCoverage (the coverage subtree's unit input) maps to -3 when consumed
// by the coverage root (the true GP coverage, matching the runtime coverage-chain origin) and to
// -4 otherwise (nested blend children receive an opaque input in the runtime emission). Anything
// unmapped is -2, which callers treat as a build error.
static int MapChainInput(const std::vector<const AOTEffectNode*>& nodes,
                         const std::vector<size_t>& slotOf, size_t nodeIndex, bool isCoverageRoot) {
  auto* node = nodes[nodeIndex];
  if (node->kind == AOTEffectKind::GeometryColor) {
    return -1;
  }
  if (node->kind == AOTEffectKind::GeometryCoverage) {
    return isCoverageRoot ? -3 : -4;
  }
  if (node->kind == AOTEffectKind::GeometryColorOpaqueInput) {
    return -5;
  }
  auto slot = slotOf[nodeIndex];
  return slot == SIZE_MAX ? -2 : static_cast<int>(slot);
}

// A RectEffect folds into the chain's AARectCoverage slot only in its device-space AA form:
// a transformed rect cannot be expressed by the device-space rect parameters, and the NonAA
// hard edge is not implemented by the analytic coverage kernel.
static bool IsChainFoldableRectEffect(const FragmentProcessor* fp) {
  if (fp == nullptr || fp->name() != "RectEffect" || fp->numChildProcessors() != 0) {
    return false;
  }
  auto rectEffect = static_cast<const RectEffect*>(fp);
  return rectEffect->isDeviceSpaceRect() && rectEffect->isAntiAlias();
}

// Analytic clip FPs folded onto the chain as color-root coverage slots (the two-FP coverage
// forms). The device-space AA rect keeps the existing chain-wide CoverageRect uniform
// (AARectCoverage); the local-space AA rect and the rrects use the dedicated LocalRectCoverage /
// RRectCoverage slot ops. The chain kernel carries one chain-wide rect parameter set per space and
// four rrect array elements, so a coverage exceeding those caps keeps the runtime route.
struct ChainClipSlots {
  const RectEffect* deviceRect = nullptr;
  const RectEffect* localRect = nullptr;
  std::vector<const RRectEffect*> rrects = {};
};

// Collects one bare analytic clip leaf (no Compose here — composed coverage lowers through the
// AOT graph instead). Returns false for anything the chain cannot represent.
static bool CollectChainClipSlot(const FragmentProcessor* fp, ChainClipSlots* slots) {
  if (fp == nullptr || slots == nullptr || fp->numChildProcessors() != 0) {
    return false;
  }
  if (fp->name() == "RectEffect") {
    auto* rectEffect = static_cast<const RectEffect*>(fp);
    if (!rectEffect->isAntiAlias()) {
      return false;
    }
    if (rectEffect->isDeviceSpaceRect()) {
      if (slots->deviceRect != nullptr) {
        return false;
      }
      slots->deviceRect = rectEffect;
    } else {
      if (slots->localRect != nullptr) {
        return false;
      }
      slots->localRect = rectEffect;
    }
    return true;
  }
  if (fp->name() == "RRectEffect") {
    if (slots->rrects.size() >= 4) {
      return false;
    }
    slots->rrects.push_back(static_cast<const RRectEffect*>(fp));
    return true;
  }
  return false;
}

// Whether a Matrix::get9 row-major array is the identity matrix.
static bool IsIdentityMatrix(const std::array<float, 9>& values) {
  return values[0] == 1.0f && values[1] == 0.0f && values[2] == 0.0f && values[3] == 0.0f &&
         values[4] == 1.0f && values[5] == 0.0f && values[6] == 0.0f && values[7] == 0.0f &&
         values[8] == 1.0f;
}

// Flattens a single-pass pointwise DAG into the fused chain processor. Texture leaves are placed
// in the leading slots so that slot k pairs with TextureSampler_k, which keeps sampler indexing
// static in the kernel. When clipSlots carries analytic clip FPs, their coverage slots multiply
// the previous root and the last one becomes the new root. When coverageGraph is given, its nodes
// (lowered from the coverage FP with the GeometryCoverage unit as origin) are merged after the
// color nodes: coverage leaves extend the leading texture block, coverage ops trail the color ops,
// and coverageRoot becomes the chain's coverage root.
static PlacementPtr<FragmentProcessor> BuildChainFP(
    BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
    const ChainClipSlots& clipSlots, const DeviceSpaceTextureEffect* maskEffect,
    const AOTEffectGraph* coverageGraph = nullptr, AOTNodeID coverageRoot = AOTNodeID(),
    bool coverageLeafFromUVCoord = false) {
  // Combined node space: color-graph nodes occupy [0, colorCount), coverage-graph nodes follow.
  const size_t colorCount = graph.nodeCount();
  const size_t covCount = coverageGraph != nullptr ? coverageGraph->nodeCount() : 0;
  std::vector<const AOTEffectNode*> nodes = {};
  nodes.reserve(colorCount + covCount);
  for (size_t index = 0; index < colorCount; ++index) {
    auto* node = graph.nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return nullptr;
    }
    nodes.push_back(node);
  }
  for (size_t index = 0; index < covCount; ++index) {
    auto* node = coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
    if (node == nullptr) {
      return nullptr;
    }
    nodes.push_back(node);
  }
  std::vector<size_t> slotOf(nodes.size(), SIZE_MAX);
  // The runtime emits a blend's child processors with the default solid-white input (see
  // XfermodeFragmentProcessor::emitChild), never the paint color: a two-child blend passes
  // (inputColor.rgb, 1.0) and a single-child xfer passes vec4(1.0). A texture operand therefore
  // always samples raw (no paint-alpha modulation), and a single-child operand's geometry-color
  // edge means opaque white (-4) instead of the paint color (-1).
  std::vector<bool> opaqueBlendOperand(nodes.size(), false);
  std::vector<bool> whiteInputOperand(nodes.size(), false);
  for (size_t index = 0; index < nodes.size(); ++index) {
    auto* consumer = nodes[index];
    if (consumer->kind != AOTEffectKind::Blend) {
      continue;
    }
    auto blendParams = std::get_if<AOTBlendParameters>(&consumer->parameters);
    if (blendParams == nullptr) {
      continue;
    }
    // Only the child side gets the white input; the pass-through operand is the xfer's own
    // input (the upstream chain output) and keeps its normal semantics. DstChild lowers as
    // inputs=[input, child], SrcChild as inputs=[child, input].
    size_t inputBase = index < colorCount ? 0 : colorCount;
    auto markChild = [&](size_t operand) {
      opaqueBlendOperand[inputBase + consumer->inputs[operand].index()] = true;
      if (blendParams->childType != 2) {
        whiteInputOperand[inputBase + consumer->inputs[operand].index()] = true;
      }
    };
    if (blendParams->childType == 0) {
      markChild(1);
    } else if (blendParams->childType == 1) {
      markChild(0);
    } else {
      markChild(0);
      markChild(1);
    }
  }
  std::vector<size_t> ordered = {};
  ordered.reserve(pass.nodes.size() + covCount);
  // Texture leaves first (color pass, then coverage subtree), then the remaining nodes in
  // topological order (color ops, then coverage ops). The two subtrees never reference each
  // other, so this layout keeps every input ahead of its consumer.
  auto collect = [&](bool texturePhase) {
    for (auto nodeID : pass.nodes) {
      auto* node = graph.nodeAt(nodeID);
      if ((node->kind == AOTEffectKind::TextureSource) == texturePhase) {
        slotOf[nodeID.index()] = ordered.size();
        ordered.push_back(nodeID.index());
      }
    }
    if (coverageGraph != nullptr) {
      // Index 0 of the coverage graph is the GeometryCoverage unit, which never becomes a slot.
      for (size_t index = 1; index < covCount; ++index) {
        auto* node = coverageGraph->nodeAt(AOTNodeID(static_cast<uint32_t>(index)));
        if ((node->kind == AOTEffectKind::TextureSource) == texturePhase) {
          slotOf[colorCount + index] = ordered.size();
          ordered.push_back(colorCount + index);
        }
      }
    }
  };
  collect(true);
  size_t leafCount = ordered.size();
  collect(false);
  std::vector<PlacementPtr<FragmentProcessor>> leaves = {};
  leaves.reserve(leafCount);
  int tiledLeafIndex = -1;
  AOTTiledTextureRecipe tiledRecipe = {};
  bool hasRectCoverage = false;
  bool hasLocalRectCoverage = false;
  bool hasGradient = false;
  std::vector<AOTChainSlot> slots(ordered.size());
  // All-ones reproduces the legacy behavior (every target reads the uvCoord attribute when the GP
  // carries one). The atlas-text route clears it and sets only the coverage leaves' bits.
  uint32_t coordSourceMask = coverageLeafFromUVCoord ? 0u : ~0u;
  const size_t covRootCombined =
      coverageGraph != nullptr ? colorCount + coverageRoot.index() : SIZE_MAX;
  for (size_t index = 0; index < ordered.size(); ++index) {
    const size_t combined = ordered[index];
    auto node = nodes[combined];
    const size_t inputBase = combined < colorCount ? 0 : colorCount;
    const bool isCoverageRoot = combined == covRootCombined;
    auto mapInput = [&](AOTNodeID input) {
      int mapped = MapChainInput(nodes, slotOf, inputBase + input.index(), isCoverageRoot);
      return mapped == -1 && whiteInputOperand[combined] ? -4 : mapped;
    };
    auto& slot = slots[index];
    switch (node->kind) {
      case AOTEffectKind::TextureSource: {
        auto leaf = BuildFPForNode(allocator, node, nullptr);
        if (leaf == nullptr || leaf->name() != "TextureEffect") {
          return nullptr;
        }
        slot.op = AOTChainOp::Texture;
        // A texture fed directly by the geometry color is a color source and gets the paint-alpha
        // modulation folded into its read (as the runtime's SrcIn wrap does). Any other texture —
        // a coverage mask or a blend operand — must sample raw, matching the runtime emission.
        slot.textureModulate = !node->inputs.empty() && inputBase == 0 &&
                                       node->inputs[0] == AOTNodeID(0) &&
                                       !opaqueBlendOperand[combined]
                                   ? 1
                                   : 0;
        // A leaf that is the coverage subtree's root modulates by the coverage unit's alpha
        // (bit 2), matching the runtime coverage-FP readback (tex * coverageIn.a).
        if (isCoverageRoot && !node->inputs.empty() && node->inputs[0].index() == 0) {
          slot.textureModulateUnit = 1;
        }
        // Alpha-only leaves (e.g. shape masks) need the kernel to splat .r into all channels; the
        // raw sample would otherwise read alpha as constant 1.
        slot.textureAlphaOnly =
            static_cast<const TextureEffect*>(leaf.get())->isAlphaOnly() ? 1 : 0;
        if (coverageLeafFromUVCoord && inputBase != 0) {
          // Atlas text: the coverage leaf sources its coordinates from the maskCoord attribute
          // (the uvCoord slot), while color leaves and gradients keep the position source.
          coordSourceMask |= 1u << static_cast<uint32_t>(leaves.size());
        }
        auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
        if (parameters != nullptr && parameters->samplingKind == AOTTextureSamplingKind::Tiled &&
            parameters->tiledRecipe.has_value() &&
            (parameters->tiledRecipe->shaderModeX != TiledTextureShaderMode::None ||
             parameters->tiledRecipe->shaderModeY != TiledTextureShaderMode::None)) {
          // At most one shader-tiled leaf per chain; a second one cannot be represented.
          if (tiledLeafIndex >= 0) {
            return nullptr;
          }
          tiledLeafIndex = static_cast<int>(leaves.size());
          tiledRecipe = *parameters->tiledRecipe;
        }
        leaves.push_back(std::move(leaf));
        break;
      }
      case AOTEffectKind::ConstColor: {
        auto parameters = std::get_if<AOTConstColorParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return nullptr;
        }
        slot.op = AOTChainOp::ConstColor;
        slot.constColor = *parameters;
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      case AOTEffectKind::ColorMatrix:
      case AOTEffectKind::Luma:
      case AOTEffectKind::AlphaThreshold:
      case AOTEffectKind::ColorSpaceXform: {
        if (node->inputs.size() != 1) {
          return nullptr;
        }
        AOTPointwiseSlot pointwise = {};
        if (!BuildPointwiseSlot(graph, node, &pointwise)) {
          return nullptr;
        }
        // AOTPointwiseOpType and AOTChainOp share values 0..4 by ABI (both mirror the OP_*
        // constants), so the pointwise kinds convert directly.
        slot.op = static_cast<AOTChainOp>(static_cast<int>(pointwise.type));
        slot.colorMatrix = pointwise.colorMatrix;
        slot.luma = pointwise.luma;
        slot.alphaThreshold = pointwise.alphaThreshold;
        slot.colorSpaceXform = pointwise.colorSpaceXform;
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      case AOTEffectKind::Blend: {
        auto parameters = std::get_if<AOTBlendParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 2) {
          return nullptr;
        }
        if (parameters->childType == 2 && inputBase != 0) {
          // Two-child blends lower their children with the opaque-alpha geometry input, which
          // only the color graph carries.
          return nullptr;
        }
        slot.op = AOTChainOp::Blend;
        slot.blend = *parameters;
        slot.in0 = mapInput(node->inputs[0]);
        slot.in1 = mapInput(node->inputs[1]);
        break;
      }
      case AOTEffectKind::RectCoverage: {
        auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return nullptr;
        }
        if (IsIdentityMatrix(parameters->deviceToLocal)) {
          // The kernel carries one chain-wide CoverageRect uniform, so a second device-space
          // rect-coverage node cannot be represented.
          if (hasRectCoverage) {
            return nullptr;
          }
          hasRectCoverage = true;
          slot.op = AOTChainOp::AARectCoverage;
          slot.rectCoverage = *parameters;
        } else {
          // Same for the local-space form through the CoverageLocalRect uniform set.
          if (hasLocalRectCoverage) {
            return nullptr;
          }
          hasLocalRectCoverage = true;
          slot.op = AOTChainOp::LocalRectCoverage;
          slot.localRectCoverage = *parameters;
        }
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      case AOTEffectKind::RRectCoverage: {
        auto parameters = std::get_if<AOTRRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return nullptr;
        }
        // The kernel carries four CoverageRRect* array elements; AOTPointwiseChainProcessor::Make
        // rejects a fifth rrect slot.
        slot.op = AOTChainOp::RRectCoverage;
        slot.rrectCoverage = *parameters;
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      case AOTEffectKind::GradientSource: {
        auto parameters = std::get_if<AOTGradientParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return nullptr;
        }
        // The kernel carries one chain-wide gradient parameter block and one gradient coordinate
        // varying, so a second gradient node cannot be represented.
        if (hasGradient) {
          return nullptr;
        }
        hasGradient = true;
        slot.op = AOTChainOp::Gradient;
        slot.gradient = *parameters;
        slot.in0 = mapInput(node->inputs[0]);
        break;
      }
      default:
        return nullptr;
    }
    if (slot.op != AOTChainOp::Texture &&
        (slot.in0 == -2 || (slot.op == AOTChainOp::Blend && slot.in1 == -2))) {
      return nullptr;
    }
  }
  // A LUT gradient carries its baked texture as a sampler-only child appended after the DAG
  // leaves; the OP_GRADIENT LUT branch reads it through the GradientLUTLeaf uniform.
  PlacementPtr<FragmentProcessor> lutChild = nullptr;
  int lutLeafIndex = -1;
  for (auto& slot : slots) {
    if (slot.op == AOTChainOp::Gradient && slot.gradient.colorizerKind == 3) {
      // The LUT counts as a leaf for sampler binding, and TEXTURE_COUNT only exists for 1 or 2
      // leaves here, so at most one DAG leaf can coexist with it.
      if (leaves.size() >= 2) {
        return nullptr;
      }
      SamplingOptions lutSampling(FilterMode::Linear, MipmapMode::None);
      SamplingArgs lutArgs = {TileMode::Clamp, TileMode::Clamp, lutSampling,
                              SrcRectConstraint::Fast};
      auto lutMatrix = Matrix::I();
      lutChild = TextureEffect::Make(allocator, slot.gradient.lutProxy, lutArgs, &lutMatrix);
      if (lutChild == nullptr) {
        return nullptr;
      }
      lutLeafIndex = static_cast<int>(leaves.size());
    }
  }
  // TEXTURE_COUNT exists only as 0 or 4: chains with 1-3 sampler children ride the four-leaf
  // artifacts by binding phantom children that re-use the first leaf's texture. Their DAG slots
  // are never OP_TEXTURE, so the kernel's runtime guard never samples them. The phantom must
  // mirror the first leaf's sampler state: on OpenGL the wrap/filter modes are texture-object
  // state (not per-unit), so a default-state phantom bound after the real leaf would overwrite
  // e.g. a decal clamp-to-border with clamp-to-edge for the whole draw.
  std::vector<PlacementPtr<FragmentProcessor>> samplerPadding = {};
  std::shared_ptr<TextureProxy> paddingProxy = nullptr;
  SamplerState paddingState = {};
  for (size_t index = 0; index < nodes.size() && paddingProxy == nullptr; ++index) {
    if (nodes[index]->kind == AOTEffectKind::TextureSource) {
      auto* textureParams = std::get_if<AOTTextureParameters>(&nodes[index]->parameters);
      if (textureParams != nullptr) {
        paddingProxy = textureParams->textureProxy;
        if (textureParams->samplingKind == AOTTextureSamplingKind::Tiled &&
            textureParams->tiledRecipe.has_value()) {
          paddingState = textureParams->tiledRecipe->hardwareSampler;
        } else {
          paddingState = textureParams->samplerState;
        }
      }
    } else if (nodes[index]->kind == AOTEffectKind::GradientSource) {
      // A LUT-gradient-only chain has no TextureSource node; its baked LUT texture serves as
      // the padding texture just as it served as the phantom before.
      auto* gradientParams = std::get_if<AOTGradientParameters>(&nodes[index]->parameters);
      if (gradientParams != nullptr && gradientParams->lutProxy != nullptr) {
        paddingProxy = gradientParams->lutProxy;
      }
    }
  }
  SamplingOptions paddingSampling(paddingState.minFilterMode, paddingState.magFilterMode,
                                  paddingState.mipmapMode);
  SamplingArgs paddingArgs = {paddingState.tileModeX, paddingState.tileModeY, paddingSampling,
                              SrcRectConstraint::Fast};
  size_t samplerChildren = leaves.size() + (lutChild != nullptr ? 1 : 0);
  if (samplerChildren > 0 && samplerChildren < 4) {
    if (paddingProxy == nullptr) {
      return nullptr;
    }
    while (samplerChildren < 4) {
      auto phantomMatrix = Matrix::I();
      auto phantom = TextureEffect::Make(allocator, paddingProxy, paddingArgs, &phantomMatrix);
      if (phantom == nullptr) {
        return nullptr;
      }
      samplerPadding.push_back(std::move(phantom));
      ++samplerChildren;
    }
  }
  auto rootIndex = slotOf[pass.output.index()];
  if (rootIndex == SIZE_MAX) {
    return nullptr;
  }
  int coverageRootSlot = -1;
  if (coverageGraph != nullptr) {
    // Acceptance gate, kept tight to the byte-verified shapes. Accepted roots: a single blend
    // consuming the unit input (the xfer-dst coverage family), a bare texture leaf consuming the
    // unit (a local mask fill, modulated by the unit alpha through selector bit 2), or an analytic
    // coverage node (RectCoverage / RRectCoverage) closing a chain that starts at the unit. The
    // unit may otherwise feed only texture leaves (their input is unused — blend operands sample
    // raw), the gradient (a blend child, so it reads the opaque -4 designator), and analytic
    // coverage nodes. Analytic clip slots may multiply a blend-rooted subtree: the scalar coverage
    // commutes with the pointwise blend result, so the product matches the runtime chain order
    // (verified differentially). Two-child blends keep the plain route rather than risking a wrong
    // value when the GP coverage is below 1.0.
    auto* covRootNode = nodes[covRootCombined];
    const bool rootIsBlend = covRootNode->kind == AOTEffectKind::Blend;
    const bool rootIsTexture = covRootNode->kind == AOTEffectKind::TextureSource;
    const bool rootIsAnalytic = covRootNode->kind == AOTEffectKind::RectCoverage ||
                                covRootNode->kind == AOTEffectKind::RRectCoverage;
    if (!rootIsBlend && !rootIsTexture && !rootIsAnalytic) {
      return nullptr;
    }
    int coverageBlendCount = 0;
    bool rejected = false;
    bool rootConsumesUnit = false;
    for (size_t j = 1; j < covCount && !rejected; ++j) {
      auto* node = nodes[colorCount + j];
      if (node->kind == AOTEffectKind::Blend) {
        ++coverageBlendCount;
      }
      for (auto input : node->inputs) {
        if (input.index() != 0) {
          continue;
        }
        if (colorCount + j == covRootCombined) {
          rootConsumesUnit = true;
          continue;
        }
        if (node->kind != AOTEffectKind::TextureSource &&
            node->kind != AOTEffectKind::GradientSource &&
            node->kind != AOTEffectKind::RectCoverage &&
            node->kind != AOTEffectKind::RRectCoverage) {
          rejected = true;
          break;
        }
      }
    }
    // An analytic root consumes the previous analytic node rather than the unit, but the chain
    // still transmits the unit coverage transitively, so the root-consumes-unit requirement only
    // applies to the blend and texture roots (the blend-count rule already rejects blends under
    // an analytic root).
    if (rejected || coverageBlendCount != (rootIsBlend ? 1 : 0) ||
        (!rootConsumesUnit && !rootIsAnalytic)) {
      return nullptr;
    }
    auto covSlot = slotOf[covRootCombined];
    if (covSlot == SIZE_MAX) {
      return nullptr;
    }
    coverageRootSlot = static_cast<int>(covSlot);
  }
  // The narrow clip slots multiply the color root; each appended slot becomes the new root.
  if (clipSlots.deviceRect != nullptr) {
    if (slots.size() >= AOTPointwiseChainProcessor::MaxSlots) {
      return nullptr;
    }
    AOTChainSlot rectSlot = {};
    rectSlot.op = AOTChainOp::AARectCoverage;
    const auto& rect = clipSlots.deviceRect->getRect();
    rectSlot.rectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    rectSlot.in0 = static_cast<int>(rootIndex);
    rootIndex = slots.size();
    slots.push_back(rectSlot);
  }
  if (clipSlots.localRect != nullptr) {
    if (slots.size() >= AOTPointwiseChainProcessor::MaxSlots) {
      return nullptr;
    }
    AOTChainSlot localRectSlot = {};
    localRectSlot.op = AOTChainOp::LocalRectCoverage;
    const auto& rect = clipSlots.localRect->getRect();
    localRectSlot.localRectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    const auto& matrix = clipSlots.localRect->getDeviceToLocal();
    localRectSlot.localRectCoverage.deviceToLocal = {matrix[0], matrix[1], matrix[2],
                                                     matrix[3], matrix[4], matrix[5],
                                                     matrix[6], matrix[7], matrix[8]};
    localRectSlot.in0 = static_cast<int>(rootIndex);
    rootIndex = slots.size();
    slots.push_back(localRectSlot);
  }
  for (auto* rrectEffect : clipSlots.rrects) {
    if (slots.size() >= AOTPointwiseChainProcessor::MaxSlots) {
      return nullptr;
    }
    AOTChainSlot rrectSlot = {};
    rrectSlot.op = AOTChainOp::RRectCoverage;
    const auto& rect = rrectEffect->getLocalRect();
    rrectSlot.rrectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    const auto& radii = rrectEffect->getRadii();
    for (size_t i = 0; i < 4; ++i) {
      rrectSlot.rrectCoverage.radiiX[i] = radii[i].x;
      rrectSlot.rrectCoverage.radiiY[i] = radii[i].y;
    }
    const auto& matrix = rrectEffect->getDeviceToLocal();
    rrectSlot.rrectCoverage.deviceToLocal = {matrix[0], matrix[1], matrix[2], matrix[3], matrix[4],
                                             matrix[5], matrix[6], matrix[7], matrix[8]};
    rrectSlot.rrectCoverage.antiAlias = rrectEffect->isAntiAlias() ? 1.0f : 0.0f;
    rrectSlot.in0 = static_cast<int>(rootIndex);
    rootIndex = slots.size();
    slots.push_back(rrectSlot);
  }
  PlacementPtr<FragmentProcessor> maskChild = nullptr;
  if (maskEffect != nullptr) {
    // The mask child's Subset write routes to the kernel's DeviceMaskSubset decoy, so a leaf's
    // real subset rect survives in the shared uniform block.
    maskChild = DeviceSpaceTextureEffect::Make(allocator, maskEffect->getTextureProxy(),
                                               maskEffect->getUVMatrix());
    if (maskChild == nullptr) {
      return nullptr;
    }
  }
  // Four-leaf artifacts declare the mask sampler unconditionally, so a mask-free chain binds a
  // phantom there (re-using the same padding texture) and clears HasMaskTexture at runtime.
  bool maskChildIsPhantom = false;
  if (samplerChildren == 4 && maskChild == nullptr) {
    if (paddingProxy == nullptr) {
      return nullptr;
    }
    auto phantomMatrix = Matrix::I();
    maskChild = TextureEffect::Make(allocator, paddingProxy, paddingArgs, &phantomMatrix);
    if (maskChild == nullptr) {
      return nullptr;
    }
    maskChildIsPhantom = true;
  }
  const AOTTiledTextureRecipe* recipePtr = tiledLeafIndex >= 0 ? &tiledRecipe : nullptr;
  return AOTPointwiseChainProcessor::Make(
      allocator, std::move(leaves), slots, rootIndex, tiledLeafIndex, recipePtr,
      std::move(maskChild), coverageRootSlot, coordSourceMask, std::move(lutChild), lutLeafIndex,
      std::move(samplerPadding), maskChildIsPhantom);
}

static PlacementPtr<FragmentProcessor> BuildFPForPass(
    BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
    PlacementPtr<FragmentProcessor> sourceOverride) {
  PlacementPtr<FragmentProcessor> current = std::move(sourceOverride);
  if (pass.kernel == AOTKernelKind::PointwiseTail) {
    size_t nodeIndex = 0;
    if (current == nullptr) {
      auto sourceNode = graph.nodeAt(pass.nodes[nodeIndex++]);
      current = BuildFPForNode(allocator, sourceNode, nullptr);
    }
    if (current == nullptr) {
      return nullptr;
    }
    std::vector<AOTPointwiseSlot> slots = {};
    slots.reserve(pass.nodes.size() - nodeIndex);
    for (; nodeIndex < pass.nodes.size(); ++nodeIndex) {
      AOTPointwiseSlot slot = {};
      if (!BuildPointwiseSlot(graph, graph.nodeAt(pass.nodes[nodeIndex]), &slot)) {
        return nullptr;
      }
      slots.push_back(slot);
    }
    return AOTPointwiseTailProcessor::Make(allocator, std::move(current), slots);
  }
  if (pass.kernel == AOTKernelKind::PointwiseChain) {
    ChainClipSlots emptyClipSlots = {};
    return BuildChainFP(allocator, graph, pass, emptyClipSlots, nullptr);
  }
  if (pass.kernel == AOTKernelKind::PerlinNoiseFill) {
    // A PerlinNoiseFill pass is always the plan's first pass and consumes no upstream texture: the
    // planner only emits it for a GeometryColor-fed noise source, so an incoming override would
    // have nowhere to attach.
    if (current != nullptr) {
      return nullptr;
    }
    return BuildPerlinNoiseFillFP(allocator, graph, pass);
  }
  for (size_t index = 0; index < pass.nodes.size(); ++index) {
    auto node = graph.nodeAt(pass.nodes[index]);
    if (node == nullptr) {
      return nullptr;
    }
    if (index == 0 && current == nullptr) {
      current = BuildFPForNode(allocator, node, nullptr);
    } else if (node->kind != AOTEffectKind::TextureSource) {
      current = BuildFPForNode(allocator, node, std::move(current));
    }
    if (current == nullptr) {
      return nullptr;
    }
  }
  return current;
}

}  // namespace

bool AOTPlanExecutor::CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  // A PointwiseChain plan is a single fused pass over the whole DAG; the linear-pass invariants do
  // not apply to it. Its structural checks live in BuildChainFP and AOTPointwiseChainProcessor::Make.
  if (plan.passes.size() == 1 && plan.passes[0].kernel == AOTKernelKind::PointwiseChain) {
    const auto& pass = plan.passes[0];
    if (!plan.output.isValid() || plan.output != graph.root() || pass.output != plan.output ||
        pass.materializesOutput || pass.nodes.empty() ||
        pass.nodes.size() > AOTPointwiseChainProcessor::MaxSlots) {
      return false;
    }
    // The fused kernel binds one sampler per texture leaf and exists for 0, 1, 2 or 4 leaves (a
    // zero-leaf chain evaluates const-color and blend ops against the geometry color): plain
    // leaves, or tiled leaves whose wrap modes are fully resolved by the hardware sampler (no
    // shader-mode emulation). Anything else must stay on the plain route.
    size_t plainLeaves = 0;
    size_t shaderTiledLeaves = 0;
    for (auto nodeID : pass.nodes) {
      auto node = graph.nodeAt(nodeID);
      if (node == nullptr) {
        return false;
      }
      if (node->kind == AOTEffectKind::TextureSource) {
        auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
        if (parameters == nullptr) {
          return false;
        }
        if (parameters->samplingKind == AOTTextureSamplingKind::Tiled) {
          const auto& recipe = parameters->tiledRecipe;
          if (!recipe.has_value() || !IsChainCompatibleTiledMode(recipe->shaderModeX) ||
              !IsChainCompatibleTiledMode(recipe->shaderModeY)) {
            return false;
          }
          if (recipe->shaderModeX != TiledTextureShaderMode::None ||
              recipe->shaderModeY != TiledTextureShaderMode::None) {
            // PointwiseChainShader carries exactly one shared tiled-sampling uniform block.
            // BuildChainFP cannot represent a second shader-tiled leaf, so reject before planning
            // execution rather than letting construction fail after CanExecute promised success.
            if (++shaderTiledLeaves > AOTPointwiseChainProcessor::MaxShaderTiledChainLeaves) {
              return false;
            }
          }
        } else if (parameters->samplingKind != AOTTextureSamplingKind::Plain) {
          return false;
        }
        ++plainLeaves;
      }
    }
    return AOTPointwiseChainProcessor::HasChainKernelVariant(plainLeaves);
  }
  return ValidateLinearPlan(graph, plan);
}

PlacementPtr<FragmentProcessor> AOTPlanExecutor::BuildPerlinNoiseFP(BlockAllocator* allocator,
                                                                    const AOTEffectGraph& graph,
                                                                    const AOTPassDescriptor& pass) {
  if (pass.kernel != AOTKernelKind::PerlinNoiseFill) {
    return nullptr;
  }
  return BuildPerlinNoiseFillFP(allocator, graph, pass);
}

// Single-FP analytic coverage beyond the narrow forms: a bare AA RectEffect (local space), a bare
// RRectEffect, or a flat Compose of AA rect / rrect leaves with at most one alpha-only device-space
// mask (extracted as the mask child — the kernel cannot sample a device-space leaf inside the
// coverage subtree). The analytic leaves lower through the AOT graph as chained RectCoverage /
// RRectCoverage nodes rooted at the GP coverage unit, which is what makes composed multi-clip
// coverage representable at all. Returns false when the coverage carries anything else.
static bool LowerAnalyticCoverageFP(const FragmentProcessor* fp,
                                    const DeviceSpaceTextureEffect** maskEffect,
                                    AOTNodeBuilder* covBuilder, AOTNodeID* covRoot) {
  if (fp == nullptr || maskEffect == nullptr || covBuilder == nullptr || covRoot == nullptr) {
    return false;
  }
  std::vector<const FragmentProcessor*> leaves = {};
  if (fp->name() == "ComposeFragmentProcessor") {
    for (size_t i = 0; i < fp->numChildProcessors(); ++i) {
      leaves.push_back(fp->childProcessor(i));
    }
  } else {
    leaves.push_back(fp);
  }
  std::vector<const FragmentProcessor*> analytic = {};
  for (auto* leaf : leaves) {
    if (leaf->name() == "DeviceSpaceTextureEffect") {
      auto* dste = static_cast<const DeviceSpaceTextureEffect*>(leaf);
      if (*maskEffect != nullptr || !dste->isAlphaOnly() || dste->hasPerspective()) {
        return false;
      }
      *maskEffect = dste;
      continue;
    }
    analytic.push_back(leaf);
  }
  if (analytic.empty()) {
    return false;
  }
  AOTNodeID unit = AOTNodeID::Invalid();
  if (!covBuilder->addGeometryCoverage(&unit)) {
    return false;
  }
  AOTNodeID current = unit;
  for (auto* leaf : analytic) {
    if ((leaf->name() != "RectEffect" && leaf->name() != "RRectEffect") ||
        leaf->numChildProcessors() != 0) {
      return false;
    }
    AOTNodeID next = AOTNodeID::Invalid();
    if (!leaf->lowerToAOT(covBuilder, current, &next)) {
      return false;
    }
    current = next;
  }
  if (current == unit) {
    return false;
  }
  *covRoot = current;
  return true;
}

PlacementPtr<FragmentProcessor> AOTPlanExecutor::BuildChainProcessor(
    BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
    const std::vector<const FragmentProcessor*>& coverageFPs, bool coverageLeafFromUVCoord) {
  if (pass.kernel != AOTKernelKind::PointwiseChain) {
    return nullptr;
  }
  ChainClipSlots clipSlots = {};
  if (coverageFPs.empty()) {
    return BuildChainFP(allocator, graph, pass, clipSlots, nullptr);
  }
  if (coverageFPs.size() > 2) {
    return nullptr;
  }
  // Normalize the coverage into the forms the chain kernel carries. The narrow forms come first:
  // a bare device-space AA RectEffect folds into an AARectCoverage slot, an alpha-only
  // DeviceSpaceTextureEffect becomes the mask child, and Compose(mask, rect) yields both.
  // Analytic coverage the narrow forms cannot express — rrects, local-space rects, composed
  // multi-clip chains — lowers through the AOT graph as RectCoverage / RRectCoverage nodes rooted
  // at the GP coverage unit. Anything else lowers as a general coverage subtree. A two-FP coverage
  // folds as [analytic slots / mask, texture subtree] or [texture subtree, analytic slots / mask]
  // depending on which position carries the subtree.
  const DeviceSpaceTextureEffect* maskEffect = nullptr;
  const FragmentProcessor* subtreeFP = nullptr;
  if (coverageFPs.size() == 1) {
    auto* coverage = coverageFPs.front();
    auto coverageName = coverage->name();
    if (coverageName == "RectEffect" && IsChainFoldableRectEffect(coverage)) {
      clipSlots.deviceRect = static_cast<const RectEffect*>(coverage);
      return BuildChainFP(allocator, graph, pass, clipSlots, nullptr);
    }
    if (coverageName == "DeviceSpaceTextureEffect") {
      maskEffect = static_cast<const DeviceSpaceTextureEffect*>(coverage);
      if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
        return nullptr;
      }
      return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect);
    }
    if (coverageName == "ComposeFragmentProcessor" && coverage->numChildProcessors() == 2 &&
        coverage->childProcessor(0)->name() == "DeviceSpaceTextureEffect" &&
        IsChainFoldableRectEffect(coverage->childProcessor(1))) {
      maskEffect = static_cast<const DeviceSpaceTextureEffect*>(coverage->childProcessor(0));
      clipSlots.deviceRect = static_cast<const RectEffect*>(coverage->childProcessor(1));
      if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
        return nullptr;
      }
      return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect);
    }
    AOTNodeBuilder covBuilder = {};
    AOTNodeID covRoot = AOTNodeID::Invalid();
    if (LowerAnalyticCoverageFP(coverage, &maskEffect, &covBuilder, &covRoot)) {
      AOTEffectGraph covGraph = {};
      if (!covBuilder.finish(covRoot, &covGraph)) {
        return nullptr;
      }
      return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect, &covGraph, covRoot,
                          coverageLeafFromUVCoord);
    }
    subtreeFP = coverage;
  } else {
    // Two-FP coverage: the terminal FP decides the form. A trailing alpha-only device mask keeps
    // the mask-child role and the first FP lowers as the subtree (or folds as an analytic slot);
    // a trailing local texture is the subtree root (a bare local mask, accepted by the gate only
    // when the texture is already instantiated), with the first FP folding as analytic clip slots
    // or the mask child; a trailing analytic clip leaf folds as clip slots with the first FP
    // lowering as the subtree.
    auto* last = coverageFPs.back();
    auto* first = coverageFPs.front();
    if (last->name() == "DeviceSpaceTextureEffect") {
      maskEffect = static_cast<const DeviceSpaceTextureEffect*>(last);
      if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
        return nullptr;
      }
      if (first->name() == "RectEffect" || first->name() == "RRectEffect") {
        if (!CollectChainClipSlot(first, &clipSlots)) {
          return nullptr;
        }
      } else {
        subtreeFP = first;
      }
    } else if (last->name() == "TextureEffect") {
      subtreeFP = last;
      if (first->name() == "RectEffect" || first->name() == "RRectEffect") {
        if (!CollectChainClipSlot(first, &clipSlots)) {
          return nullptr;
        }
      } else if (first->name() == "DeviceSpaceTextureEffect") {
        maskEffect = static_cast<const DeviceSpaceTextureEffect*>(first);
        if (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective()) {
          return nullptr;
        }
      } else {
        return nullptr;
      }
    } else if (last->name() == "RectEffect" || last->name() == "RRectEffect") {
      if (!CollectChainClipSlot(last, &clipSlots)) {
        return nullptr;
      }
      subtreeFP = first;
    } else {
      return nullptr;
    }
  }
  if (subtreeFP == nullptr) {
    // A two-FP [analytic slots, device mask] coverage carries no texture subtree; the slots and
    // the mask child are the whole coverage.
    if (clipSlots.deviceRect == nullptr && clipSlots.localRect == nullptr &&
        clipSlots.rrects.empty()) {
      return nullptr;
    }
    return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect);
  }
  // General coverage subtree: lower the FP with the GP coverage unit as its input. BuildChainFP
  // applies the acceptance gate (single root blend consuming the unit) before merging.
  AOTNodeBuilder covBuilder = {};
  AOTNodeID unit = AOTNodeID::Invalid();
  AOTNodeID covRoot = AOTNodeID::Invalid();
  // Strict lowering: a texture whose view is not yet instantiated (a generated mask) fails here,
  // which keeps the draw on the runtime route whose program builder emits a zero stub for such
  // textures — the AOT path must not serve content the runtime would have dropped.
  if (!covBuilder.addGeometryCoverage(&unit) ||
      !subtreeFP->lowerToAOT(&covBuilder, unit, &covRoot) || !covRoot.isValid() ||
      covRoot == unit) {
    return nullptr;
  }
  AOTEffectGraph covGraph = {};
  if (!covBuilder.finish(covRoot, &covGraph)) {
    return nullptr;
  }
  return BuildChainFP(allocator, graph, pass, clipSlots, maskEffect, &covGraph, covRoot,
                      coverageLeafFromUVCoord);
}

PlacementPtr<RenderTask> AOTPlanExecutor::Make(Context* context, uint32_t renderFlags,
                                               const AOTEffectGraph& graph,
                                               const AOTEffectPlan& plan, const Rect& deviceBounds,
                                               std::shared_ptr<RenderTargetProxy> destination,
                                               PlacementPtr<DrawOp>* originalDraw,
                                               const Point& fpCoordOffset) {
  if (context == nullptr || destination == nullptr || destination->getContext() != context ||
      originalDraw == nullptr || *originalDraw == nullptr || deviceBounds.isEmpty() ||
      !CanExecute(graph, plan)) {
    return nullptr;
  }
  // One geometry for the whole plan. Every intermediate pass renders into a target of exactly this
  // size and the terminal draw samples the last one back with its top-left, so the capture area and
  // the sampling translation are the same value by construction. The apron is zero here: all six
  // kernel kinds are pointwise, so a pass reads its input at the output coordinate only, and each
  // intermediate texture is pixel-aligned with the next target (uvMatrix is identity). A non-zero
  // apron would break that alignment instead of fixing a sampling error.
  auto geometry = AOTMaterializationPolicy::PrepareGeometry(deviceBounds, 0.0f);
  if (geometry.isEmpty()) {
    return nullptr;
  }
  // Intermediate fills must present the rebuilt chain with the same local coordinates the original
  // processors see: the draw's own offset plus any FP-space translation the caller's geometry
  // applies (zero for OpsCompositor draws; the coordOffset of an offscreen fill). The terminal
  // draw samples the last intermediate back with trans(-geometry.coordOffset), which is exact for
  // both cases: fragCoord + fpCoordOffset - (geometry.coordOffset + fpCoordOffset) maps a
  // destination pixel to its intermediate texel with no leftover term.
  auto intermediateOffset = geometry.coordOffset + fpCoordOffset;
  auto drawingManager = context->drawingManager();
  auto allocator = drawingManager->drawingAllocator();
  std::vector<AOTIntermediatePass> intermediatePasses = {};
  intermediatePasses.reserve(plan.passes.size() - 1);
  std::shared_ptr<TextureProxy> previousTexture = nullptr;
  DrawOp::ColorProcessorList terminalColors = {};

  for (size_t index = 0; index < plan.passes.size(); ++index) {
    PlacementPtr<FragmentProcessor> source = nullptr;
    if (previousTexture != nullptr) {
      // Intermediate consumers run in offscreen-local coordinates. Only the terminal draw samples
      // that texture from the destination's device coordinates and needs the bounds translation.
      auto deviceMatrix = index + 1 == plan.passes.size()
                              ? Matrix::MakeTrans(-geometry.bounds.left, -geometry.bounds.top)
                              : Matrix::I();
      source = DeviceSpaceTextureEffect::Make(allocator, previousTexture, deviceMatrix);
      if (source == nullptr) {
        return nullptr;
      }
    }
    auto processor = BuildFPForPass(allocator, graph, plan.passes[index], std::move(source));
    if (processor == nullptr) {
      return nullptr;
    }
    if (index + 1 == plan.passes.size()) {
      terminalColors.emplace_back(std::move(processor));
      break;
    }

    auto materialized = AOTMaterializationPolicy::AllocateTarget(context, geometry);
    if (materialized.renderTarget == nullptr) {
      return nullptr;
    }
    auto target = std::move(materialized.renderTarget);
    auto drawOp = drawingManager->makeFillDrawOp(target, std::move(processor), renderFlags,
                                                 intermediateOffset);
    if (drawOp == nullptr) {
      return nullptr;
    }
    previousTexture = target->asTextureProxy();
    if (previousTexture == nullptr) {
      return nullptr;
    }
    intermediatePasses.push_back({std::move(target), std::move(drawOp)});
  }

  auto task = allocator->make<AOTPlanRenderTask>(allocator, std::move(intermediatePasses),
                                                 std::move(terminalColors), std::move(destination));
  if (task == nullptr) {
    return nullptr;
  }
  task->setOriginalDraw(std::move(*originalDraw));
  return task;
}
}  // namespace tgfx
