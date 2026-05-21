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

#include "ShapeBezierRasterizeDrawOp.h"
#include "core/utils/Log.h"
#include "gpu/ProxyProvider.h"
#include "gpu/RectsVertexProvider.h"
#include "gpu/processors/BezierCoverGeometryProcessor.h"
#include "gpu/processors/BezierStencilGeometryProcessor.h"
#include "tgfx/core/RenderFlags.h"

namespace tgfx {

// Bytes per stencil-pass vertex: position (Float2) + klm (Float3) = 5 floats.
static constexpr size_t kStencilVertexStride = 5 * sizeof(float);

namespace {

bool IsEvenOdd(PathFillType fillType) {
  return fillType == PathFillType::EvenOdd || fillType == PathFillType::InverseEvenOdd;
}

bool IsInverse(PathFillType fillType) {
  return fillType == PathFillType::InverseWinding || fillType == PathFillType::InverseEvenOdd;
}

DepthStencilDescriptor MakeStencilPassDS(PathFillType fillType) {
  DepthStencilDescriptor ds = {};
  ds.format = PixelFormat::DEPTH24_STENCIL8;
  ds.depthCompare = CompareFunction::Always;
  ds.depthWriteEnabled = false;
  ds.stencilReadMask = 0xFF;
  ds.stencilWriteMask = 0xFF;
  if (IsEvenOdd(fillType)) {
    StencilDescriptor s = {};
    s.compare = CompareFunction::Always;
    s.passOp = StencilOperation::Invert;
    s.failOp = StencilOperation::Keep;
    s.depthFailOp = StencilOperation::Keep;
    ds.stencilFront = s;
    ds.stencilBack = s;
  } else {
    StencilDescriptor sf = {};
    sf.compare = CompareFunction::Always;
    sf.passOp = StencilOperation::IncrementWrap;
    sf.failOp = StencilOperation::Keep;
    sf.depthFailOp = StencilOperation::Keep;
    ds.stencilFront = sf;
    StencilDescriptor sb = {};
    sb.compare = CompareFunction::Always;
    sb.passOp = StencilOperation::DecrementWrap;
    sb.failOp = StencilOperation::Keep;
    sb.depthFailOp = StencilOperation::Keep;
    ds.stencilBack = sb;
  }
  return ds;
}

DepthStencilDescriptor MakeCoverPassDS(PathFillType fillType) {
  DepthStencilDescriptor ds = {};
  ds.format = PixelFormat::DEPTH24_STENCIL8;
  ds.depthCompare = CompareFunction::Always;
  ds.depthWriteEnabled = false;
  ds.stencilReadMask = 0xFF;
  // Cover pass writes zero back so subsequent draws sharing the same render pass see a clean
  // stencil buffer. The cover quad is sized to fully contain the stencil pass output, so any
  // pixel that survived the test is reset to 0 here. For inverse fills the surviving pixels
  // are already zero (path outside region), so the write is a harmless no-op.
  ds.stencilWriteMask = 0xFF;
  StencilDescriptor s = {};
  s.passOp = StencilOperation::Zero;
  s.failOp = StencilOperation::Keep;
  s.depthFailOp = StencilOperation::Keep;
  // Inverse fill paints the "outside" of the path. Both inverse-evenodd and inverse-nonzero
  // boil down to "shade pixels whose stencil counter is zero", regardless of which counting
  // rule was used in the stencil pass. The non-inverse case still differs by fill rule
  // (evenodd uses Invert leaving 0xFF for the inside; nonzero uses Inc/Dec leaving any
  // non-zero value for the inside).
  if (IsInverse(fillType)) {
    s.compare = CompareFunction::Equal;  // ref will be 0
  } else {
    s.compare = IsEvenOdd(fillType) ? CompareFunction::Equal : CompareFunction::NotEqual;
  }
  ds.stencilFront = s;
  ds.stencilBack = s;
  return ds;
}

// Reference value passed to setStencilReference when issuing the cover draw call. Mirrors
// the compare-function logic in MakeCoverPassDS — see the comment there for why inverse
// fills always read against zero.
uint32_t CoverPassStencilReference(PathFillType fillType) {
  if (IsInverse(fillType)) {
    return 0;
  }
  return IsEvenOdd(fillType) ? 0xFF : 0;
}

}  // namespace

PlacementPtr<ShapeBezierRasterizeDrawOp> ShapeBezierRasterizeDrawOp::Make(
    std::shared_ptr<ShapeBezierRasterizeGeometryProxy> geometryProxy, PMColor color,
    const Matrix& viewMatrix, const Matrix& uvMatrix, const Rect& coverLocalBounds,
    PathFillType fillType) {
  if (geometryProxy == nullptr) {
    return nullptr;
  }
  auto context = geometryProxy->getContext();
  if (context == nullptr) {
    return nullptr;
  }
  auto allocator = context->drawingAllocator();
  return allocator->make<ShapeBezierRasterizeDrawOp>(
      allocator, std::move(geometryProxy), color, viewMatrix, uvMatrix, coverLocalBounds, fillType);
}

ShapeBezierRasterizeDrawOp::ShapeBezierRasterizeDrawOp(
    BlockAllocator* allocator, std::shared_ptr<ShapeBezierRasterizeGeometryProxy> geometryProxy,
    PMColor color, const Matrix& viewMatrix, const Matrix& uvMatrix, const Rect& coverLocalBounds,
    PathFillType fillType)
    : DrawOp(allocator, AAType::None), geometryProxy(std::move(geometryProxy)), color(color),
      viewMatrix(viewMatrix), uvMatrix(uvMatrix), coverLocalBounds(coverLocalBounds),
      fillType(fillType) {
  // The cover quad is built in local space — the cover GP applies the view matrix itself, so
  // pre-applying it here would double-transform. Inverse fill draws supply a clip-derived
  // local rect that already covers the inverse region.
  auto context = this->geometryProxy->getContext();
  if (context != nullptr) {
    auto provider = RectsVertexProvider::MakeFrom(allocator, coverLocalBounds, AAType::None);
    coverQuadBuffer = context->proxyProvider()->createVertexBufferProxy(
        std::move(provider), RenderFlags::DisableAsyncTask);
  }

  // Pre-compute the per-op state that only depends on construction-time inputs. This keeps
  // onDraw() limited to the work that genuinely needs the live RenderPass / RenderTarget.
  stencilGP = BezierStencilGeometryProcessor::Make(allocator, viewMatrix);
  stencilPassDS = MakeStencilPassDS(fillType);
  coverPassDS = MakeCoverPassDS(fillType);
  coverStencilRef = CoverPassStencilReference(fillType);
}

PlacementPtr<GeometryProcessor> ShapeBezierRasterizeDrawOp::onMakeGeometryProcessor(
    RenderTarget* /*renderTarget*/) {
  // The cover pass owns the brush FP chain via DrawOp::execute(); it is therefore the GP that
  // we hand back to the standard pipeline. The stencil pass GP is built on demand inside
  // onDraw() so it can run with its own ProgramInfo and stencil configuration.
  return BezierCoverGeometryProcessor::Make(allocator, color, viewMatrix, uvMatrix);
}

void ShapeBezierRasterizeDrawOp::onConfigureProgramInfo(ProgramInfo& programInfo) {
  // Inject the cover-pass depth/stencil state into the standard ProgramInfo so the pipeline
  // built by DrawOp::execute() reads the stencil buffer instead of ignoring it. Reuse the
  // descriptor we already cached at construction time.
  programInfo.setDepthStencil(coverPassDS);
}

void ShapeBezierRasterizeDrawOp::onDraw(RenderPass* renderPass) {
  if (geometryProxy == nullptr || coverQuadBuffer == nullptr) {
    return;
  }
  auto coverBuffer = coverQuadBuffer->getBuffer();
  if (coverBuffer == nullptr) {
    return;
  }
  // The base class wires currentRenderTarget for the duration of onDraw() so we can build our
  // own ProgramInfo for the stencil and cover passes without changing the virtual signature.
  auto* renderTarget = currentRenderTarget;
  if (renderTarget == nullptr) {
    return;
  }

  // ----- Stencil pass (skipped when there are no path vertices) -----
  // An empty vertex stream means the rasterizer produced nothing — either the path is empty
  // or the only thing to draw is an "everywhere" inverse fill. Either way the stencil buffer
  // can stay at its cleared zero state; the cover pass will read against zero and shade the
  // whole quad for inverse fills, or shade nothing for non-inverse empty paths.
  auto vertexBufferResource = geometryProxy->getVertexBuffer();
  std::shared_ptr<GPUBuffer> stencilGPUBuffer = nullptr;
  uint32_t stencilVertexCount = 0;
  if (vertexBufferResource != nullptr) {
    stencilGPUBuffer = vertexBufferResource->gpuBuffer();
    stencilVertexCount = static_cast<uint32_t>(vertexBufferResource->size() / kStencilVertexStride);
  }
  if (stencilGPUBuffer != nullptr && stencilVertexCount > 0) {
    if (stencilGP == nullptr) {
      return;
    }
    // Build a self-contained ProgramInfo: stencil GP, no FP chain, no xfer processor, colour
    // writes disabled. The pipeline reads the same depth/stencil attachment that the cover
    // pass uses; OpsRenderTask attached it because needsStencil() is true. The stencil GP
    // itself was built once at construction time; here we only build the per-frame
    // ProgramInfo wrapper around it.
    std::vector<FragmentProcessor*> stencilFPs = {};
    ProgramInfo stencilInfo(renderTarget, stencilGP.get(), std::move(stencilFPs),
                            /*numColorProcessors=*/0, /*xferProcessor=*/nullptr,
                            BlendMode::SrcOver);
    stencilInfo.setCullMode(cullMode);
    stencilInfo.setDepthStencil(stencilPassDS);
    // Drop colour writes — the stencil pass exists to update the stencil buffer only.
    stencilInfo.setColorWriteMask(0);
    auto stencilProgram = stencilInfo.getProgram();
    if (stencilProgram == nullptr) {
      LOGE("ShapeBezierRasterizeDrawOp::onDraw() Failed to build stencil pass program!");
      return;
    }
    renderPass->setPipeline(stencilProgram->getPipeline());
    stencilInfo.setUniformsAndSamplers(renderPass, stencilProgram.get());
    // Stencil reference is irrelevant for Always + Invert/Inc/Dec, but keep it deterministic.
    renderPass->setStencilReference(0);
    renderPass->setVertexBuffer(0, stencilGPUBuffer);
    renderPass->draw(PrimitiveType::Triangles, stencilVertexCount);
  }

  // ----- Cover pass -----
  // Always run the cover pass so inverse fills shade the path's exterior even when the
  // stencil pass was skipped. DrawOp::execute() already built and bound the cover pipeline +
  // uniforms before calling onDraw(); the stencil pass above (if any) replaced both, so we
  // re-attach them here. We reuse the cached Program / ProgramInfo via the protected
  // currentProgram / currentProgramInfo hooks exposed by the base class — that saves a full
  // ProgramInfo construction + getProgram() round-trip per op compared to rebuilding the
  // cover state from scratch.
  if (currentProgram == nullptr || currentProgramInfo == nullptr) {
    LOGE("ShapeBezierRasterizeDrawOp::onDraw() Missing cover program from base class!");
    return;
  }
  renderPass->setPipeline(currentProgram->getPipeline());
  currentProgramInfo->setUniformsAndSamplers(renderPass, currentProgram);
  renderPass->setStencilReference(coverStencilRef);
  renderPass->setVertexBuffer(0, coverBuffer->gpuBuffer(), coverQuadBuffer->offset());
  // RectsVertexProvider produces a triangle strip of 4 vertices for non-AA fills.
  renderPass->draw(PrimitiveType::TriangleStrip, 4);
}

}  // namespace tgfx
