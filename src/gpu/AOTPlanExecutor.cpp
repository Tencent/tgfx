
#include "AOTPlanExecutor.h"
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>
#include "core/shaders/PerlinNoiseShader.h"
#include "gpu/AOTChainBuilder.h"
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
      if (pass.kernel == AOTKernelKind::YUVTextureFill) {
        // One fused pass: a YUV source plus up to three unary pointwise-operator slots,
        // matching the three slot records the YUVTextureFillShader kernel carries. Blends and
        // deeper tails are rejected at planning time and never reach here.
        if (pass.nodes.empty() || pass.nodes.size() > 4) {
          return false;
        }
        auto sourceNode = graph.nodeAt(pass.nodes[0]);
        auto parameters = sourceNode != nullptr
                              ? std::get_if<AOTTextureParameters>(&sourceNode->parameters)
                              : nullptr;
        if (sourceNode == nullptr || sourceNode->kind != AOTEffectKind::TextureSource ||
            sourceNode->inputs.size() != 1 || sourceNode->inputs[0] != AOTNodeID(0) ||
            parameters == nullptr || !parameters->isYUV || parameters->hasRGBAAA ||
            parameters->samplingKind != AOTTextureSamplingKind::Plain) {
          return false;
        }
        AOTNodeID expectedInput = pass.nodes[0];
        for (size_t opIndex = 1; opIndex < pass.nodes.size(); ++opIndex) {
          if (!ValidatePointwiseTailOp(graph, pass.nodes[opIndex], expectedInput)) {
            return false;
          }
          expectedInput = pass.nodes[opIndex];
        }
        continue;
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

}  // namespace

bool AOTPlanExecutor::CanExecute(const AOTEffectGraph& graph, const AOTEffectPlan& plan) {
  // A PointwiseChain plan is a single fused pass over the whole DAG; the linear-pass invariants do
  // not apply to it. Its structural checks live in BuildChainFP and AOTPointwiseChainProcessor::Make.
  if (plan.passes.size() == 1 && plan.passes[0].kernel == AOTKernelKind::PointwiseChain) {
    const auto& pass = plan.passes[0];
    if (!plan.output.isValid() || plan.output != graph.root() || pass.output != plan.output ||
        pass.materializesOutput || pass.nodes.empty() || !pass.dependencies.empty()) {
      return false;
    }
    // Final encoded-instruction budget (): the node count underestimates what the
    // builder emits. A computed-input texture occupies TWO slots (a raw sampling slot plus a
    // TEX_MODULATE instruction following its producer), so the admission check must count the
    // expansion — a graph of exactly MaxSlots nodes with one computed-input texture would pass
    // the old check and fail inside BuildChainProcessor after CanExecute promised success.
    // Appended clip slots (from the draw's coverage FPs, not the plan) remain invisible here;
    // the builder's own MaxSlots check rejects those at construction and the caller falls back.
    size_t encodedInstructions = pass.nodes.size();
    for (auto nodeID : pass.nodes) {
      auto node = graph.nodeAt(nodeID);
      if (node == nullptr) {
        return false;
      }
      if (node->kind == AOTEffectKind::TextureSource && !node->inputs.empty()) {
        auto* inputNode = graph.nodeAt(node->inputs[0]);
        if (inputNode != nullptr && inputNode->kind != AOTEffectKind::GeometryColor &&
            inputNode->kind != AOTEffectKind::GeometryColorOpaqueInput &&
            inputNode->kind != AOTEffectKind::GeometryWhiteInput &&
            inputNode->kind != AOTEffectKind::GeometryCoverage) {
          ++encodedInstructions;
        }
      }
    }
    if (encodedInstructions > AOTPointwiseChainProcessor::MaxSlots) {
      return false;
    }
    // Check kernel-wide parameter budgets before accepting a DAG candidate. Logical texture
    // leaves are padded to the existing four-sampler artifacts by the chain builder.
    size_t plainLeaves = 0;
    size_t shaderTiledLeaves = 0;
    std::optional<AOTTiledTextureRecipe> firstShaderTiledRecipe = {};
    size_t colorSpaceXforms = 0;
    size_t gradients = 0;
    size_t deviceRects = 0;
    size_t localRects = 0;
    size_t rrects = 0;
    bool hasLUT = false;
    for (auto nodeID : pass.nodes) {
      auto node = graph.nodeAt(nodeID);
      if (node == nullptr) {
        return false;
      }
      if (node->kind == AOTEffectKind::TextureSource) {
        auto parameters = std::get_if<AOTTextureParameters>(&node->parameters);
        if (parameters == nullptr || parameters->isYUV || parameters->hasRGBAAA) {
          return false;
        }
        if (parameters->samplingKind == AOTTextureSamplingKind::Tiled) {
          const auto& recipe = parameters->tiledRecipe;
          if (!recipe.has_value() ||
              !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeX) ||
              !AOTChainBuilder::IsChainCompatibleTiledMode(recipe->shaderModeY)) {
            return false;
          }
          if (recipe->shaderModeX != TiledTextureShaderMode::None ||
              recipe->shaderModeY != TiledTextureShaderMode::None) {
            // PointwiseChainShader carries exactly one shared tiled-sampling uniform block; up to
            // MaxShaderTiledChainLeaves leaves may ride it when their recipes are identical (the
            // image-filter shape: source and shadow children sample the same filter domain). A
            // leaf with a different recipe beyond the first cannot be represented — reject before
            // planning execution rather than letting construction fail after CanExecute promised
            // success.
            if (shaderTiledLeaves == 0) {
              firstShaderTiledRecipe = recipe;
            } else if (shaderTiledLeaves >= AOTPointwiseChainProcessor::MaxShaderTiledChainLeaves ||
                       !AOTChainBuilder::SameTiledShaderRecipe(*firstShaderTiledRecipe, *recipe)) {
              return false;
            }
            ++shaderTiledLeaves;
          }
        } else if (parameters->samplingKind != AOTTextureSamplingKind::Plain) {
          return false;
        }
        ++plainLeaves;
      } else if (node->kind == AOTEffectKind::ColorSpaceXform) {
        auto parameters = std::get_if<AOTColorSpaceXformParameters>(&node->parameters);
        if (parameters == nullptr || parameters->steps == nullptr ||
            ++colorSpaceXforms > AOTPointwiseChainProcessor::MaxColorSpaceXformSlots) {
          return false;
        }
      } else if (node->kind == AOTEffectKind::GradientSource) {
        auto parameters = std::get_if<AOTGradientParameters>(&node->parameters);
        if (parameters == nullptr || ++gradients > AOTPointwiseChainProcessor::MaxGradientSlots) {
          return false;
        }
        hasLUT = parameters->colorizerKind == 3;
        if (hasLUT && parameters->lutProxy == nullptr) {
          return false;
        }
      } else if (node->kind == AOTEffectKind::RectCoverage) {
        auto parameters = std::get_if<AOTRectCoverageParameters>(&node->parameters);
        if (parameters == nullptr) {
          return false;
        }
        const std::array<float, 9> identity = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        auto& count = parameters->deviceToLocal == identity ? deviceRects : localRects;
        if (++count > (parameters->deviceToLocal == identity
                           ? AOTPointwiseChainProcessor::MaxDeviceRectCoverageSlots
                           : AOTPointwiseChainProcessor::MaxLocalRectCoverageSlots)) {
          return false;
        }
      } else if (node->kind == AOTEffectKind::RRectCoverage &&
                 ++rrects > AOTPointwiseChainProcessor::MaxRRectCoverageSlots) {
        return false;
      }
    }
    // The LUT branch addresses sampler 0 or 1 only, even though bindings are padded to four.
    if (hasLUT && plainLeaves > 1) {
      return false;
    }
    return AOTPointwiseChainProcessor::HasChainKernelVariant(plainLeaves + (hasLUT ? 1u : 0u));
  }
  return ValidateLinearPlan(graph, plan);
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
    auto processor =
        AOTChainBuilder::BuildFPForPass(allocator, graph, plan.passes[index], std::move(source));
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
