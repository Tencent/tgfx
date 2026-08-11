/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "DrawOp.h"
#include <algorithm>
#include "gpu/AOTEffectDecomposer.h"
#include "gpu/AOTPlanExecutor.h"
#include "gpu/PermutationMatcher.h"
#include "gpu/PrecompiledShaderCache.h"
#include "gpu/Program.h"

namespace tgfx {
bool DrawOp::prepare(RenderTarget* renderTarget, ProgramLookupMode mode,
                     std::optional<ColorProcessorList> colorOverride) {
  if (!geometryProcessorInitialized) {
    geometryProcessorInitialized = true;
    geometryProcessor = onMakeGeometryProcessor(renderTarget);
  }
  if (geometryProcessor == nullptr) {
    return false;
  }

  preparedProgram = nullptr;
  preparedProgramInfo = nullptr;
  preparedRenderTarget = nullptr;
  preparedColors = std::move(colorOverride);
  auto& activeColors = preparedColors.has_value() ? *preparedColors : colors;

  std::vector<FragmentProcessor*> fragmentProcessors = {};
  fragmentProcessors.reserve(activeColors.size() + coverages.size());
  for (auto& color : activeColors) {
    fragmentProcessors.emplace_back(color.get());
  }
  for (auto& coverage : coverages) {
    fragmentProcessors.emplace_back(coverage.get());
  }
  preparedProgramInfo = std::make_unique<ProgramInfo>(
      renderTarget, geometryProcessor.get(), std::move(fragmentProcessors), activeColors.size(),
      xferProcessor.get(), blendMode);
  preparedProgramInfo->setCullMode(cullMode);
  preparedProgram = prepareDecomposedProgram(renderTarget, activeColors);
  if (preparedProgram == nullptr) {
    preparedProgram = preparedProgramInfo->getProgram(mode);
  }
  if (preparedProgram == nullptr) {
    LOGE("DrawOp::prepare() Failed to get the program!");
    return false;
  }
  preparedRenderTarget = renderTarget;
  return true;
}

// When the plain matcher cannot serve the color chain, the chain may still reduce onto the fused
// pointwise-chain kernel. Rewriting the processors before program lookup keeps the cached program and the
// per-draw uniform upload driven by the same ProgramInfo, which a post-hoc program swap cannot
// guarantee. The matcher probe runs first so draws the plain route already serves keep their
// original funnel accounting. A single coverage FP is folded into the chain (AARect clip as a
// coverage slot, a device-space alpha mask as the mask child, Compose(mask, rect) as both);
// other coverage forms stay on their original route.
std::shared_ptr<Program> DrawOp::prepareDecomposedProgram(RenderTarget* renderTarget,
                                                          const ColorProcessorList& activeColors) {
  if (!renderTarget->getContext()->precompiledShaderCache()->isLoaded()) {
    return nullptr;
  }
  if (MatchPermutation(preparedProgramInfo.get()).has_value()) {
    return nullptr;
  }
  std::vector<const FragmentProcessor*> coverageFPs = {};
  if (!coverages.empty()) {
    // Up to two coverage FPs fold into the chain: a single one lowers as the narrow forms or a
    // general subtree, and a pair folds as subtree + trailing device-mask child.
    if (coverages.size() > 2) {
      return nullptr;
    }
    coverageFPs.reserve(coverages.size());
    for (auto& coverage : coverages) {
      coverageFPs.push_back(coverage.get());
    }
  }
  std::vector<const FragmentProcessor*> colorProcessors = {};
  colorProcessors.reserve(activeColors.size());
  for (auto& color : activeColors) {
    colorProcessors.push_back(color.get());
  }
  AOTEffectGraph graph = {};
  AOTEffectPlan plan = {};
  bool decomposed =
      AOTEffectDecomposer::Lower(colorProcessors, &graph) &&
      AOTEffectDecomposer::ValidateForFusion(graph) &&
      AOTEffectDecomposer::Decompose(graph, AOTDecompositionMode::PreferFusion, &plan);
  // A single-pass pointwise-tail plan is a linear texture-plus-ops chain, and the tail ops are a
  // subset of the chain op set, so the chain kernel can evaluate it as-is. This route only runs
  // after the plain matcher missed, which includes the shapes the tail rule rejects (no UV
  // matrix, alpha-only source, per-quad subset); rewriting the kernel field lets them resolve to
  // the chain artifact instead of falling back to runtime compilation. Device-space sources and
  // other chain-incompatible shapes are still rejected by CanExecute/BuildChainProcessor below.
  if (decomposed && plan.passes.size() == 1 &&
      plan.passes[0].kernel == AOTKernelKind::PointwiseTail) {
    plan.passes[0].kernel = AOTKernelKind::PointwiseChain;
  }
  if (!decomposed || plan.passes.size() != 1 ||
      plan.passes[0].kernel != AOTKernelKind::PointwiseChain ||
      !AOTPlanExecutor::CanExecute(graph, plan)) {
    return nullptr;
  }
  auto chainFP =
      AOTPlanExecutor::BuildChainProcessor(allocator, graph, plan.passes[0], coverageFPs);
  if (chainFP == nullptr) {
    return nullptr;
  }
  std::vector<FragmentProcessor*> rewrittenProcessors = {chainFP.get()};
  auto rewrittenInfo = std::make_unique<ProgramInfo>(renderTarget, geometryProcessor.get(),
                                                     std::move(rewrittenProcessors), 1,
                                                     xferProcessor.get(), blendMode);
  rewrittenInfo->setCullMode(cullMode);
  // Probe before the strict lookup: a chain the matcher rejects (unsupported GP, perspective leaf
  // transforms) must fall back without recording a diagnostic miss against the rewritten tree,
  // which would double-count the draw alongside the original tree's own miss record.
  if (!MatchPermutation(rewrittenInfo.get()).has_value()) {
    return nullptr;
  }
  // Strict lookup: a rewritten draw must resolve to the precompiled chain kernel. Anything else
  // (cache not loaded, missing artifact) falls back to the original processors.
  auto rewrittenProgram = rewrittenInfo->getProgram(ProgramLookupMode::PrecompiledOnly);
  if (rewrittenProgram == nullptr) {
    return nullptr;
  }
  preparedColors = ColorProcessorList{};
  preparedColors->emplace_back(std::move(chainFP));
  preparedProgramInfo = std::move(rewrittenInfo);
  return rewrittenProgram;
}

void DrawOp::executePrepared(RenderPass* renderPass, bool recordDrawStats) {
  if (preparedProgramInfo == nullptr || preparedProgram == nullptr ||
      preparedRenderTarget == nullptr) {
    LOGE("DrawOp::executePrepared() DrawOp is not prepared!");
    return;
  }
  renderPass->setPipeline(preparedProgram->getPipeline());
  preparedProgramInfo->setUniformsAndSamplers(renderPass, preparedProgram.get());
  if (offscreenFillKey != InvalidOffscreenFillKey) {
    auto cache = preparedRenderTarget->getContext()->precompiledShaderCache();
    cache->recordOffscreenFillProgram(offscreenFillKey, preparedProgram->getProvenance().program);
  }

  if (scissorRect.isEmpty()) {
    renderPass->setScissorRect(0, 0, preparedRenderTarget->width(), preparedRenderTarget->height());
  } else {
    // Clamp scissor rect to render target bounds
    int scissorX = std::max(0, static_cast<int>(scissorRect.x()));
    int scissorY = std::max(0, static_cast<int>(scissorRect.y()));
    int scissorRight = std::min(preparedRenderTarget->width(),
                                static_cast<int>(scissorRect.x() + scissorRect.width()));
    int scissorBottom = std::min(preparedRenderTarget->height(),
                                 static_cast<int>(scissorRect.y() + scissorRect.height()));
    int scissorWidth = std::max(0, scissorRight - scissorX);
    int scissorHeight = std::max(0, scissorBottom - scissorY);
    renderPass->setScissorRect(scissorX, scissorY, scissorWidth, scissorHeight);
  }
  onDraw(renderPass);

  auto cache = preparedRenderTarget->getContext()->precompiledShaderCache();
  if (recordDrawStats && cache->diagnosticRecordingEnabled()) {
    AOTDrawStats drawDelta = {};
    drawDelta.kernelInvocations = 1;
    bool completeAOTDraw =
        preparedProgram->getProvenance().program == ProgramOrigin::PrecompiledArtifact;
    cache->recordDraw(drawDelta, completeAOTDraw);
  }
}

void DrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  if (prepare(renderTarget)) {
    executePrepared(renderPass);
  }
}
}  // namespace tgfx
