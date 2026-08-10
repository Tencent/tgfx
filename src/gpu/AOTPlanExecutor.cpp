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
#include <utility>
#include <vector>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/BackingFit.h"
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/AOTPointwiseChainProcessor.h"
#include "gpu/processors/AOTPointwiseTailProcessor.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/LumaFragmentProcessor.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/proxies/RenderTargetProxy.h"
#include "gpu/resources/RenderTarget.h"
#include "tgfx/gpu/Context.h"
#include "tgfx/gpu/RenderPass.h"

namespace tgfx {
namespace {
struct AOTIntermediatePass {
  std::shared_ptr<RenderTargetProxy> target = nullptr;
  PlacementPtr<DrawOp> drawOp = nullptr;
};

static bool ValidateTextureSource(const AOTEffectGraph& graph, AOTNodeID nodeID) {
  auto node = graph.nodeAt(nodeID);
  if (node == nullptr || node->kind != AOTEffectKind::TextureSource || node->inputs.size() != 1) {
    return false;
  }
  auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
  auto input = graph.nodeAt(node->inputs[0]);
  return parameters != nullptr && parameters->samplingKind == AOTTextureSamplingKind::Plain &&
         input != nullptr && input->kind == AOTEffectKind::GeometryColor;
}

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
      if (pass.kernel == AOTKernelKind::TextureFill) {
        if (pass.nodes.size() != 1 || !ValidateTextureSource(graph, pass.nodes[0])) {
          return false;
        }
      } else if (pass.kernel == AOTKernelKind::TextureColorMatrix) {
        if (pass.nodes.size() != 2 || !ValidateTextureSource(graph, pass.nodes[0])) {
          return false;
        }
        auto matrix = graph.nodeAt(pass.nodes[1]);
        if (matrix == nullptr || matrix->kind != AOTEffectKind::ColorMatrix ||
            matrix->inputs.size() != 1 || matrix->inputs[0] != pass.nodes[0]) {
          return false;
        }
      } else if (pass.kernel == AOTKernelKind::PerlinNoiseFill) {
        // One fused pass: a perlin source plus up to two pointwise-operator slots, matching the
        // two slot records the PerlinNoiseFillShader kernel carries.
        if (pass.nodes.empty() || pass.nodes.size() > 3 ||
            !ValidatePerlinNoiseSource(graph, pass.nodes[0])) {
          return false;
        }
        AOTNodeID expectedInput = pass.nodes[0];
        for (size_t opIndex = 1; opIndex < pass.nodes.size(); ++opIndex) {
          if (!ValidatePointwiseTailOp(graph, pass.nodes[opIndex], expectedInput)) {
            return false;
          }
          expectedInput = pass.nodes[opIndex];
        }
      } else {
        return false;
      }
      continue;
    }
    if (pass.dependencies.size() != 1 || pass.dependencies[0] != index - 1 ||
        pass.nodes.size() != 1) {
      return false;
    }
    auto node = graph.nodeAt(pass.nodes[0]);
    if (node == nullptr || node->inputs.size() != 1 ||
        node->inputs[0] != plan.passes[index - 1].output) {
      return false;
    }
    if (pass.kernel == AOTKernelKind::TexturedColorMatrix) {
      if (node->kind != AOTEffectKind::ColorMatrix) {
        return false;
      }
    } else if (pass.kernel == AOTKernelKind::TexturedLuma) {
      if (node->kind != AOTEffectKind::Luma) {
        return false;
      }
    } else {
      return false;
    }
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

static bool BuildPointwiseSlot(const AOTEffectNode* node, AOTPointwiseSlot* slot) {
  if (node == nullptr || slot == nullptr) {
    return false;
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
  if (pass.nodes.empty() || pass.nodes.size() > 3) {
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
    if (!BuildPointwiseSlot(graph.nodeAt(pass.nodes[index]), &slot)) {
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

// Maps a DAG input edge onto a chain slot index. GeometryColor (node 0) is not a slot: it maps to
// -1, the Color uniform. Anything unmapped is -2, which callers treat as a build error.
static int MapChainInput(const std::vector<size_t>& slotOf, AOTNodeID input) {
  if (input.index() == 0) {
    return -1;
  }
  auto slot = slotOf[input.index()];
  return slot == SIZE_MAX ? -2 : static_cast<int>(slot);
}

// Flattens a single-pass pointwise DAG into the fused chain processor. Texture leaves are placed
// in the leading slots so that slot k pairs with TextureSampler_k, which keeps sampler indexing
// static in the kernel; the remaining nodes follow in topological order. When rectEffect is given,
// an AARectCoverage slot multiplying the previous root is appended and becomes the new root.
static PlacementPtr<FragmentProcessor> BuildChainFP(BlockAllocator* allocator,
                                                    const AOTEffectGraph& graph,
                                                    const AOTPassDescriptor& pass,
                                                    const AARectEffect* rectEffect,
                                                    const DeviceSpaceTextureEffect* maskEffect) {
  std::vector<size_t> slotOf(graph.nodeCount(), SIZE_MAX);
  std::vector<AOTNodeID> ordered = {};
  ordered.reserve(pass.nodes.size());
  for (auto nodeID : pass.nodes) {
    auto node = graph.nodeAt(nodeID);
    if (node == nullptr) {
      return nullptr;
    }
    if (node->kind == AOTEffectKind::TextureSource) {
      slotOf[nodeID.index()] = ordered.size();
      ordered.push_back(nodeID);
    }
  }
  size_t leafCount = ordered.size();
  for (auto nodeID : pass.nodes) {
    auto node = graph.nodeAt(nodeID);
    if (node->kind != AOTEffectKind::TextureSource) {
      slotOf[nodeID.index()] = ordered.size();
      ordered.push_back(nodeID);
    }
  }
  std::vector<PlacementPtr<FragmentProcessor>> leaves = {};
  leaves.reserve(leafCount);
  int tiledLeafIndex = -1;
  AOTTiledTextureRecipe tiledRecipe = {};
  bool hasRectCoverage = false;
  std::vector<AOTChainSlot> slots(ordered.size());
  for (size_t index = 0; index < ordered.size(); ++index) {
    auto node = graph.nodeAt(ordered[index]);
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
        slot.textureModulate = !node->inputs.empty() && node->inputs[0] == AOTNodeID(0) ? 1 : 0;
        // Alpha-only leaves (e.g. shape masks) need the kernel to splat .r into all channels; the
        // raw sample would otherwise read alpha as constant 1.
        slot.textureAlphaOnly =
            static_cast<const TextureEffect*>(leaf.get())->isAlphaOnly() ? 1 : 0;
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
        slot.in0 = MapChainInput(slotOf, node->inputs[0]);
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
        if (!BuildPointwiseSlot(node, &pointwise)) {
          return nullptr;
        }
        // AOTPointwiseOpType and AOTChainOp share values 0..4 by ABI (both mirror the OP_*
        // constants), so the pointwise kinds convert directly.
        slot.op = static_cast<AOTChainOp>(static_cast<int>(pointwise.type));
        slot.colorMatrix = pointwise.colorMatrix;
        slot.luma = pointwise.luma;
        slot.alphaThreshold = pointwise.alphaThreshold;
        slot.colorSpaceXform = pointwise.colorSpaceXform;
        slot.in0 = MapChainInput(slotOf, node->inputs[0]);
        break;
      }
      case AOTEffectKind::Blend: {
        auto parameters = std::get_if<AOTBlendParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 2) {
          return nullptr;
        }
        slot.op = AOTChainOp::Blend;
        slot.blend = *parameters;
        slot.in0 = MapChainInput(slotOf, node->inputs[0]);
        slot.in1 = MapChainInput(slotOf, node->inputs[1]);
        break;
      }
      case AOTEffectKind::RectCoverage: {
        auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr || node->inputs.size() != 1) {
          return nullptr;
        }
        // The kernel carries one chain-wide CoverageRect uniform, so a second rect-coverage node
        // cannot be represented.
        if (hasRectCoverage) {
          return nullptr;
        }
        hasRectCoverage = true;
        slot.op = AOTChainOp::AARectCoverage;
        slot.rectCoverage = *parameters;
        slot.in0 = MapChainInput(slotOf, node->inputs[0]);
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
  auto rootIndex = slotOf[pass.output.index()];
  if (rootIndex == SIZE_MAX) {
    return nullptr;
  }
  if (rectEffect != nullptr) {
    if (slots.size() >= AOTPointwiseChainProcessor::MaxSlots) {
      return nullptr;
    }
    AOTChainSlot rectSlot = {};
    rectSlot.op = AOTChainOp::AARectCoverage;
    const auto& rect = rectEffect->getRect();
    rectSlot.rectCoverage.rect = {rect.left, rect.top, rect.right, rect.bottom};
    rectSlot.in0 = static_cast<int>(rootIndex);
    rootIndex = slots.size();
    slots.push_back(rectSlot);
  }
  PlacementPtr<FragmentProcessor> maskChild = nullptr;
  if (maskEffect != nullptr) {
    // The mask child soft-writes the base-name Subset uniform, which would clobber a leaf's real
    // subset rect; reject that combination so the draw keeps the plain route instead of sampling
    // with a wrong clamp. The leaves were verified to be TextureEffects above.
    for (const auto& leaf : leaves) {
      if (static_cast<const TextureEffect*>(leaf.get())->hasSubset()) {
        return nullptr;
      }
    }
    maskChild = DeviceSpaceTextureEffect::Make(allocator, maskEffect->getTextureProxy(),
                                               maskEffect->getUVMatrix());
    if (maskChild == nullptr) {
      return nullptr;
    }
  }
  const AOTTiledTextureRecipe* recipePtr = tiledLeafIndex >= 0 ? &tiledRecipe : nullptr;
  return AOTPointwiseChainProcessor::Make(allocator, std::move(leaves), slots, rootIndex,
                                          tiledLeafIndex, recipePtr, std::move(maskChild));
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
      if (!BuildPointwiseSlot(graph.nodeAt(pass.nodes[nodeIndex]), &slot)) {
        return nullptr;
      }
      slots.push_back(slot);
    }
    return AOTPointwiseTailProcessor::Make(allocator, std::move(current), slots);
  }
  if (pass.kernel == AOTKernelKind::PointwiseChain) {
    return BuildChainFP(allocator, graph, pass, nullptr, nullptr);
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

static bool ExecutePreparedPass(CommandEncoder* encoder, RenderTarget* renderTarget, DrawOp* drawOp,
                                LoadAction loadAction, const PMColor& clearColor) {
  auto resolveTexture =
      renderTarget->sampleCount() > 1 ? renderTarget->getSampleTexture() : nullptr;
  RenderPassDescriptor descriptor(renderTarget->getRenderTexture(), loadAction, StoreAction::Store,
                                  clearColor, resolveTexture);
  auto renderPass = encoder->beginRenderPass(descriptor);
  if (renderPass == nullptr) {
    LOGE("AOTPlanRenderTask::execute() Failed to initialize the render pass!");
    return false;
  }
  drawOp->executePrepared(renderPass.get(), false);
  renderPass->end();
  return true;
}

class AOTPlanRenderTask : public RenderTask {
 public:
  AOTPlanRenderTask(BlockAllocator* allocator,
                    std::vector<AOTIntermediatePass>&& intermediatePasses,
                    DrawOp::ColorProcessorList&& terminalColors,
                    std::shared_ptr<RenderTargetProxy> destination)
      : RenderTask(allocator), intermediatePasses(std::move(intermediatePasses)),
        terminalColors(std::move(terminalColors)), destination(std::move(destination)) {
  }

  void setOriginalDraw(PlacementPtr<DrawOp> drawOp) {
    originalDraw = std::move(drawOp);
  }

  void execute(CommandEncoder* encoder) override {
    std::vector<std::shared_ptr<RenderTarget>> renderTargets = {};
    renderTargets.reserve(intermediatePasses.size());
    bool targetsResolved = true;
    for (const auto& pass : intermediatePasses) {
      auto renderTarget = pass.target->getRenderTarget();
      targetsResolved = targetsResolved && renderTarget != nullptr;
      renderTargets.push_back(std::move(renderTarget));
    }
    auto finalTarget = destination->getRenderTarget();
    targetsResolved = targetsResolved && finalTarget != nullptr;
    if (!targetsResolved) {
      executeFallback(encoder, std::move(finalTarget));
      return;
    }

    bool prepared = true;
    for (size_t index = 0; index < intermediatePasses.size(); ++index) {
      if (!intermediatePasses[index].drawOp->prepare(renderTargets[index].get(),
                                                     ProgramLookupMode::PrecompiledOnly)) {
        prepared = false;
        break;
      }
    }
    if (prepared && !originalDraw->prepare(finalTarget.get(), ProgramLookupMode::PrecompiledOnly,
                                           std::move(terminalColors))) {
      prepared = false;
    }
    if (!prepared) {
      executeFallback(encoder, std::move(finalTarget));
      return;
    }

    AOTDrawStats drawStats = {};
    drawStats.kernelInvocations = intermediatePasses.size() + 1;
    drawStats.offscreenTargets = intermediatePasses.size();
    drawStats.materializedEdges = intermediatePasses.size();
    drawStats.renderTargetSwitches = intermediatePasses.size();
    for (const auto& renderTarget : renderTargets) {
      auto bytes = static_cast<uint64_t>(renderTarget->width()) *
                   static_cast<uint64_t>(renderTarget->height()) * 4;
      drawStats.intermediateReadBytes += bytes;
      drawStats.intermediateWriteBytes += bytes;
      drawStats.peakTemporaryBytes += bytes;
    }

    for (size_t index = 0; index < intermediatePasses.size(); ++index) {
      if (!ExecutePreparedPass(encoder, renderTargets[index].get(),
                               intermediatePasses[index].drawOp.get(), LoadAction::Clear,
                               PMColor::Transparent())) {
        return;
      }
    }
    if (!ExecutePreparedPass(encoder, finalTarget.get(), originalDraw.get(), LoadAction::Load,
                             PMColor::Transparent())) {
      return;
    }
    auto cache = finalTarget->getContext()->precompiledShaderCache();
    if (cache->diagnosticRecordingEnabled()) {
      cache->recordDraw(drawStats, true);
    }
  }

 private:
  std::vector<AOTIntermediatePass> intermediatePasses = {};
  DrawOp::ColorProcessorList terminalColors = {};
  std::shared_ptr<RenderTargetProxy> destination = nullptr;
  PlacementPtr<DrawOp> originalDraw = nullptr;

  void executeFallback(CommandEncoder* encoder, std::shared_ptr<RenderTarget> finalTarget) {
    if (finalTarget == nullptr) {
      LOGE("AOTPlanRenderTask::executeFallback() Final render target is null!");
      return;
    }
    if (!originalDraw->prepare(finalTarget.get(), ProgramLookupMode::AllowRuntimeFallback)) {
      return;
    }
    if (!ExecutePreparedPass(encoder, finalTarget.get(), originalDraw.get(), LoadAction::Load,
                             PMColor::Transparent())) {
      return;
    }
    auto cache = finalTarget->getContext()->precompiledShaderCache();
    if (cache->diagnosticRecordingEnabled()) {
      AOTDrawStats drawStats = {};
      drawStats.atomicFallbacks = 1;
      drawStats.kernelInvocations = 1;
      cache->recordDraw(drawStats, false);
    }
  }
};
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
            if (++shaderTiledLeaves > 1) {
              return false;
            }
          }
        } else if (parameters->samplingKind != AOTTextureSamplingKind::Plain) {
          return false;
        }
        ++plainLeaves;
      }
    }
    return plainLeaves == 0 || plainLeaves == 1 || plainLeaves == 2 || plainLeaves == 4;
  }
  return ValidateLinearPlan(graph, plan);
}

PlacementPtr<FragmentProcessor> AOTPlanExecutor::BuildChainProcessor(
    BlockAllocator* allocator, const AOTEffectGraph& graph, const AOTPassDescriptor& pass,
    const FragmentProcessor* coverageFP) {
  if (pass.kernel != AOTKernelKind::PointwiseChain) {
    return nullptr;
  }
  if (coverageFP == nullptr) {
    return BuildChainFP(allocator, graph, pass, nullptr, nullptr);
  }
  // Normalize the coverage FP into the forms the chain kernel carries: a bare AARectEffect folds
  // into an AARectCoverage slot, an alpha-only DeviceSpaceTextureEffect becomes the mask child,
  // and Compose(mask, rect) yields both. Anything else stays on the plain route.
  const AARectEffect* rectEffect = nullptr;
  const DeviceSpaceTextureEffect* maskEffect = nullptr;
  auto coverageName = coverageFP->name();
  if (coverageName == "AARectEffect") {
    rectEffect = static_cast<const AARectEffect*>(coverageFP);
  } else if (coverageName == "DeviceSpaceTextureEffect") {
    maskEffect = static_cast<const DeviceSpaceTextureEffect*>(coverageFP);
  } else if (coverageName == "ComposeFragmentProcessor" && coverageFP->numChildProcessors() == 2 &&
             coverageFP->childProcessor(0)->name() == "DeviceSpaceTextureEffect" &&
             coverageFP->childProcessor(1)->name() == "AARectEffect") {
    maskEffect = static_cast<const DeviceSpaceTextureEffect*>(coverageFP->childProcessor(0));
    rectEffect = static_cast<const AARectEffect*>(coverageFP->childProcessor(1));
  } else {
    return nullptr;
  }
  if (maskEffect != nullptr && (!maskEffect->isAlphaOnly() || maskEffect->hasPerspective())) {
    return nullptr;
  }
  return BuildChainFP(allocator, graph, pass, rectEffect, maskEffect);
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
