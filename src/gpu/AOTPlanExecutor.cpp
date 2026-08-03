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
#include "gpu/AOTMaterializationPolicy.h"
#include "gpu/BackingFit.h"
#include "gpu/DrawingManager.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/processors/AOTPointwiseTailProcessor.h"
#include "gpu/processors/AlphaThresholdFragmentProcessor.h"
#include "gpu/processors/ColorMatrixFragmentProcessor.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/LumaFragmentProcessor.h"
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
  return ValidateLinearPlan(graph, plan);
}

PlacementPtr<RenderTask> AOTPlanExecutor::Make(Context* context, uint32_t renderFlags,
                                               const AOTEffectGraph& graph,
                                               const AOTEffectPlan& plan, const Rect& deviceBounds,
                                               std::shared_ptr<RenderTargetProxy> destination,
                                               PlacementPtr<DrawOp>* originalDraw) {
  if (context == nullptr || destination == nullptr || destination->getContext() != context ||
      originalDraw == nullptr || *originalDraw == nullptr || deviceBounds.isEmpty() ||
      !CanExecute(graph, plan)) {
    return nullptr;
  }
  auto bounds = deviceBounds;
  bounds.roundOut();
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
                              ? Matrix::MakeTrans(-bounds.left, -bounds.top)
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

    auto materialized = AOTMaterializationPolicy::PrepareTarget(context, deviceBounds, 0.0f);
    if (materialized.renderTarget == nullptr) {
      return nullptr;
    }
    auto target = std::move(materialized.renderTarget);
    auto drawOp = drawingManager->makeFillDrawOp(target, std::move(processor), renderFlags,
                                                 materialized.coordOffset);
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
