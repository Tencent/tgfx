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
#include "gpu/aot/select/PrecompiledDiagnostics.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/Processor.h"
#include "gpu/processors/TextureEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

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
  return shaderName == "Chain";
}

#if DEBUG
namespace {}  // namespace
#endif

void StandardDrawOp::configureProgramInfo(ProgramInfo* info, PixelFormat depthStencilFormat) {
  info->setCullMode(cullMode);
  if (depthStencilFormat != PixelFormat::Unknown) {
    // Some backends reject a pipeline that declares no depth-stencil format while the pass has a
    // depth-stencil attachment. Inherit the pass format; the default no-op depth/stencil state
    // keeps the draw behaviour unchanged.
    DepthStencilDescriptor depthStencil = {};
    depthStencil.format = depthStencilFormat;
    info->setDepthStencil(depthStencil);
  }
  onConfigureProgramInfo(*info);
}

bool StandardDrawOp::failLookup(RenderTarget* renderTarget, bool isFinalLookup,
                                const char* reason) {
  if (isFinalLookup) {
    // The final lookup for this draw (plan-task pass probes fall back to the original draw):
    // nothing will be drawn, so the drop must be visible.
    LOGE("StandardDrawOp::prepare() %s; the draw is dropped!", reason);
    auto cache = ContextCacheAccess::Cache(renderTarget->getContext());
    if (cache->diagnosticRecordingEnabled()) {
      cache->recordDroppedDraw();
    }
  }
  return false;
}

bool StandardDrawOp::prepare(RenderTarget* renderTarget, PixelFormat depthStencilFormat,
                             bool isFinalLookup) {
  // The FP-tree route: an op that spells its vertex stage through a GeometryProcessor serves the
  // whole draw from the processor tree — the geometry processor, the color/coverage fragment
  // processors and the xfer processor assembled here — with the runtime GLSL emission. The route
  // is GL-only (see FPRecordingEnabled); a null geometry processor also keeps a draw on the
  // precompiled route below. A plan-task pass is the precompiled route's own product and always
  // stays on it.
  const bool fpTreeRoute = FPRecordingEnabled(renderTarget->getContext());
  auto geometryProcessor = !fpTreeRoute ? nullptr : onMakeGeometryProcessor(renderTarget);
  if (geometryProcessor != nullptr) {
    preparedProgram = nullptr;
    preparedProgramInfo = nullptr;
    preparedRenderTarget = nullptr;
    std::vector<FragmentProcessor*> fragmentProcessors = {};
    fragmentProcessors.reserve(colors.size() + coverages.size());
    for (auto& color : colors) {
      fragmentProcessors.emplace_back(color.get());
    }
    for (auto& coverage : coverages) {
      fragmentProcessors.emplace_back(coverage.get());
    }
    // The processor's placement outlives prepare(): executePrepared() reads the tree again for
    // the uniform upload, so the op owns it until the draw is executed.
    preparedGeometryProcessor = std::move(geometryProcessor);
    preparedProgramInfo = std::make_unique<ProgramInfo>(
        renderTarget, preparedGeometryProcessor.get(), std::move(fragmentProcessors), colors.size(),
        xferProcessor.get(), blendMode);
    configureProgramInfo(preparedProgramInfo.get(), depthStencilFormat);
    preparedProgram = preparedProgramInfo->getProgram();
    if (preparedProgram == nullptr) {
      preparedGeometryProcessor = nullptr;
      preparedProgramInfo = nullptr;
      return failLookup(renderTarget, isFinalLookup, "the FP-tree program is unavailable");
    }
    preparedRenderTarget = renderTarget;
    return true;
  }
  if (!geometryInitialized) {
    geometryInitialized = true;
    geometryLayout = onMakeGeometry(renderTarget);
  }
  if (!geometryLayout.has_value()) {
    return false;
  }
  // The precompiled variant and plan-pass routes that carried a DrawIR are retired: only the
  // FP-tree branch above serves a draw, so a draw without a geometry processor has no program.
  return failLookup(renderTarget, isFinalLookup, "the draw carries no expression");
}

bool StandardDrawOp::prepareForTask(RenderTarget* renderTarget, bool isFinalLookup) {
  // Mirror execute()'s ordering: onPrepare() may veto the op (empty asynchronous buffers);
  // skipping it here would leave those ops drawing from null resources ().
  if (!onPrepare()) {
    return false;
  }
  return prepare(renderTarget, PixelFormat::Unknown, isFinalLookup);
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
    // The direct-chain accounting derives from the resolved program: a pass resolves through the
    // chain scope and a plain lookup is direct when the program it resolved to is the chain
    // shader, so one test covers both branches.
    if (IsDirectAOTShader(preparedProgram->shaderName())) {
      drawDelta.directChainDraws = 1;
    }
    cache->recordDraw(drawDelta, true);
  }
  return true;
}
}  // namespace tgfx
