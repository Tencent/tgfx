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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "StandardDrawOp.h"
#include "core/utils/Log.h"
#include "gpu/ContextInternal.h"
#include "gpu/DrawingManager.h"
#include "gpu/Program.h"
#include "gpu/aot/bundle/PrecompiledShaderCache.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/select/AOTProgramInfo.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/Processor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "gpu/shaders/kernels/PointwiseChainShader.h"

namespace tgfx {

void StandardDrawOp::execute(RenderPass* renderPass, RenderTarget* renderTarget) {
  if (!onPrepare()) {
    return;
  }
  if (!prepare(renderTarget, renderPass->depthStencilFormat())) {
    return;
  }
  executePrepared(renderPass, renderTarget);
}

// Whether the shader the plain lookup resolved to is the chain interpreter — the AOT route's own
// direct serving. The perlin kernel serving a plain noise tree is the dedicated route, not the
// AOT entry's own.
static bool IsDirectAOTShader(const std::string& shaderName) {
  return shaderName == PointwiseChainShader::Name;
}

#if DEBUG
namespace {
// The texture-batch safety net: for the single-leaf color shape the texture rows read, the IR
// leaf's record-time facts must agree with the tree's one texture processor. A disagreement is
// a record-versus-prepare drift (a pending upload's instantiation, a proxy/texture disagreement)
// that would make the executor-table rows read different facts than the processor walk did.
// Multi-leaf shapes (blend DAGs, a mask-modulated shader) have no unambiguous correspondence
// here and are skipped — the rows' shapes are single-leaf.
void AssertSingleLeafFacts(const DrawIR& ir,
                           const std::vector<PlacementPtr<FragmentProcessor>>& colors) {
  const AOTTextureParameters* leaf = nullptr;
  size_t leafCount = 0;
  for (uint32_t index = 0; index < ir.color.nodeCount(); ++index) {
    auto* node = ir.color.nodeAt(AOTNodeID(index));
    if (node != nullptr && node->kind == AOTEffectKind::TextureSource) {
      leaf = std::get_if<AOTTextureParameters>(&node->parameters);
      leafCount++;
    }
  }
  if (leafCount != 1 || leaf == nullptr) {
    return;
  }
  const FragmentProcessor* texture = nullptr;
  size_t textureCount = 0;
  for (const auto& color : colors) {
    FragmentProcessor::Iter iter(color.get());
    while (auto* fp = iter.next()) {
      if (IsProcessor<TiledTextureEffect>(fp) || IsProcessor<DeviceSpaceTextureEffect>(fp) ||
          IsProcessor<TextureEffect>(fp)) {
        texture = fp;
        textureCount++;
      }
    }
  }
  if (textureCount != 1 || texture == nullptr) {
    return;
  }
  auto classOf = [](const FragmentProcessor* fp) {
    if (IsProcessor<TiledTextureEffect>(fp)) {
      return AOTTextureSamplingKind::Tiled;
    }
    if (IsProcessor<DeviceSpaceTextureEffect>(fp)) {
      return AOTTextureSamplingKind::Device;
    }
    return AOTTextureSamplingKind::Plain;
  };
  // A failure here means the leaf's record-time facts drifted from the tree's processor.
  DEBUG_ASSERT(classOf(texture) == leaf->samplingKind);
  if (leaf->samplingKind == AOTTextureSamplingKind::Plain) {
    auto* te = static_cast<const TextureEffect*>(texture);
    DEBUG_ASSERT(te->isYUV() == leaf->isYUV && te->isAlphaOnly() == leaf->isAlphaOnly &&
                 te->hasRGBAAA() == leaf->hasRGBAAA &&
                 te->hasPerspective() == leaf->hasPerspective);
  } else if (leaf->samplingKind == AOTTextureSamplingKind::Tiled) {
    auto* tiled = static_cast<const TiledTextureEffect*>(texture);
    DEBUG_ASSERT(tiled->isAlphaOnly() == leaf->isAlphaOnly &&
                 tiled->hasPerspective() == leaf->hasPerspective);
  }
}
}  // namespace
#endif

bool StandardDrawOp::prepare(RenderTarget* renderTarget, PixelFormat depthStencilFormat,
                             bool isFinalLookup, const DrawIR* passIR) {
  if (!geometryProcessorInitialized) {
    geometryProcessorInitialized = true;
    geometryProcessor = onMakeGeometryProcessor(renderTarget);
  }
  if (geometryProcessor == nullptr) {
    return false;
  }
#if DEBUG
  // The leaf twin runs while the carried IR is the one being served: the color list must be the
  // record-time one (a grown coverage list is appended to the IR, not compared here).
  const bool irServes = loweredIR() != nullptr && loweredIR()->colorFPCount == colors.size();
  if (irServes) {
    AssertSingleLeafFacts(*ir, colors);
  }
#endif

  preparedProgram = nullptr;
  preparedProgramInfo = nullptr;
  preparedRenderTarget = nullptr;
  preparedViaDecomposition = false;

  std::vector<FragmentProcessor*> fragmentProcessors = {};
  size_t colorCount = colors.size();
  // Plan-pass serving: the pass IR is the whole expression, so the AOT route resolves the draw
  // from the program it compiles onto — no processor travels through the info. A fill pass carries
  // the IR on itself; the terminal pass hands it in from the plan.
  const DrawIR* resolvedPassIR = passIR;
  if (resolvedPassIR == nullptr) {
    auto* lowered = loweredIR();
    if (lowered != nullptr && lowered->program.has_value()) {
      // A plan pass carries its compiled program from the pass build; a draw's own IR compiles on
      // its first match and then carries the program the same way.
      resolvedPassIR = lowered;
    }
  }
  if (resolvedPassIR != nullptr) {
    auto context = renderTarget->getContext();
    if (context == nullptr || context->drawingManager() == nullptr) {
      return false;
    }
    // The pass IR carries its program from the pass build, whose variant plus the pipeline state
    // form the key — everything resolves before any cache lookup. The pass info carries no
    // processors: the program is the whole expression, and the blend formula's coverage bit is
    // false (the kernel folds coverage itself).
    if (!resolvedPassIR->program.has_value()) {
      // A pass IR is compiled when the plan builds it; a missing program means the admission
      // promise was broken, so the draw is dropped instead of compiled on the spot. A pass probe
      // (isFinalLookup false) falls back to the original draw, so only the draw's own final
      // lookup records the drop.
      if (isFinalLookup) {
        LOGE(
            "StandardDrawOp::prepare() the pass carries no compiled program; the draw is dropped!");
        auto cache = ContextCacheAccess::Cache(renderTarget->getContext());
        if (cache->diagnosticRecordingEnabled()) {
          cache->recordDroppedDraw();
        }
      }
      return false;
    }
    auto passInfo = std::make_unique<ProgramInfo>(renderTarget, geometryProcessor.get(),
                                                  xferProcessor.get(), blendMode, false);
    passInfo->setCullMode(cullMode);
    if (depthStencilFormat != PixelFormat::Unknown) {
      DepthStencilDescriptor depthStencil = {};
      depthStencil.format = depthStencilFormat;
      passInfo->setDepthStencil(depthStencil);
    }
    onConfigureProgramInfo(*passInfo);
    // The AOT program resolution (and the specialization capture it runs) consults the carried
    // IR through the writer's canWrite, so the IR must ride the pass info before the resolution
    // — the second set below is then idempotent and keeps the ownership flow unchanged.
    passInfo->setCarriedIR(resolvedPassIR);
    AOTProgramInfo aotInfo(*passInfo, *resolvedPassIR->program);
    if (!aotInfo.valid()) {
      // The pass's shape has no variant, or the bundle lacks it: nothing can serve this draw.
      // As above, only the draw's own final lookup records the drop; a probe falls back.
      if (isFinalLookup) {
        LOGE(
            "StandardDrawOp::prepare() the pass program has no serving variant; the draw is "
            "dropped!");
        auto cache = ContextCacheAccess::Cache(renderTarget->getContext());
        if (cache->diagnosticRecordingEnabled()) {
          cache->recordDroppedDraw();
        }
      }
      return false;
    }
    passInfo->setCarriedIR(resolvedPassIR);
    preparedProgramInfo = std::move(passInfo);
    preparedProgram = aotInfo.program();
    preparedViaDecomposition = true;
    preparedRenderTarget = renderTarget;
    return true;
  } else {
    fragmentProcessors.reserve(colors.size() + coverages.size());
    for (auto& color : colors) {
      fragmentProcessors.emplace_back(color.get());
    }
    for (auto& coverage : coverages) {
      fragmentProcessors.emplace_back(coverage.get());
    }
  }
  preparedProgramInfo = std::make_unique<ProgramInfo>(renderTarget, geometryProcessor.get(),
                                                      std::move(fragmentProcessors), colorCount,
                                                      xferProcessor.get(), blendMode);
  preparedProgramInfo->setCullMode(cullMode);
  if (depthStencilFormat != PixelFormat::Unknown) {
    // Some backends reject a pipeline that declares no depth-stencil format while the pass has a
    // depth-stencil attachment. Inherit the pass format; the default no-op depth/stencil state
    // keeps the draw behaviour unchanged.
    DepthStencilDescriptor depthStencil = {};
    depthStencil.format = depthStencilFormat;
    preparedProgramInfo->setDepthStencil(depthStencil);
  }
  onConfigureProgramInfo(*preparedProgramInfo);
  // The draw's record-time IR is the expression every later stage reads. A coverage processor
  // attached only at prepare (a rasterized shape's mask, added by onMakeGeometryProcessor) is
  // appended to the IR's terms instead of dropping the IR — the color graph cannot change after
  // record time, so a color-count mismatch is the only case the IR does not describe.
  if (loweredIR() != nullptr && loweredIR()->colorFPCount == colors.size() &&
      appendGrownCoverage()) {
    preparedProgramInfo->setCarriedIR(loweredIR());
  }
  preparedProgram = preparedProgramInfo->getProgram();
  if (preparedProgram == nullptr) {
    if (isFinalLookup) {
      // The final lookup for this draw (plan-task pass probes fall back to the original draw):
      // nothing will be drawn, so the drop must be visible.
      LOGE("StandardDrawOp::prepare() No program for the draw; the draw is dropped!");
      auto cache = ContextCacheAccess::Cache(renderTarget->getContext());
      if (cache->diagnosticRecordingEnabled()) {
        cache->recordDroppedDraw();
      }
    }
    return false;
  }
  // The direct-chain accounting derives from the resolved program: the plain lookup served this
  // draw through the chain interpreter when the program it resolved to is the chain shader.
  preparedViaDecomposition = IsDirectAOTShader(preparedProgram->shaderName());
  preparedRenderTarget = renderTarget;
  return true;
}

bool StandardDrawOp::prepareForTask(RenderTarget* renderTarget, bool isFinalLookup,
                                    const DrawIR* passIR) {
  // Mirror execute()'s ordering: onPrepare() may veto the op (empty asynchronous buffers);
  // skipping it here would leave those ops drawing from null resources ().
  if (!onPrepare()) {
    return false;
  }
  return prepare(renderTarget, PixelFormat::Unknown, isFinalLookup, passIR);
}

bool StandardDrawOp::executePrepared(RenderPass* renderPass, RenderTarget* renderTarget,
                                     bool recordDrawStats) {
  if (preparedProgramInfo == nullptr || preparedProgram == nullptr ||
      preparedRenderTarget == nullptr) {
    LOGE("StandardDrawOp::executePrepared() DrawOp is not prepared!");
    return false;
  }
  renderPass->setPipeline(preparedProgram->getPipeline());
  if (!preparedProgramInfo->setUniformsAndSamplers(renderPass, preparedProgram.get())) {
    // No uniform backing store: submitting would draw with unwritten (or garbage) uniforms.
    return false;
  }
  applyScissor(renderPass, renderTarget);
  onDraw(renderPass, renderTarget);

  auto cache = ContextCacheAccess::Cache(preparedRenderTarget->getContext());
  if (recordDrawStats && cache->diagnosticRecordingEnabled()) {
    AOTDrawStats drawDelta = {};
    drawDelta.kernelInvocations = 1;
    if (preparedViaDecomposition) {
      drawDelta.directChainDraws = 1;
    }
    cache->recordDraw(drawDelta, true);
  }
  return true;
}
}  // namespace tgfx
