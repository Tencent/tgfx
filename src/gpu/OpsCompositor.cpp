/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "OpsCompositor.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/Rasterizer.h"
#include "core/utils/Caster.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ResourceProvider.h"
#include "gpu/ops/ClearOp.h"
#include "gpu/ops/DstTextureCopyOp.h"
#include "gpu/ops/ResolveOp.h"
#include "gpu/ops/ShapeDrawOp.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "processors/PorterDuffXferProcessor.h"

namespace tgfx {
/**
 * Defines the maximum distance a draw can extend beyond a clip's boundary and still be considered
 * 'on the other side'. This tolerance accounts for potential floating point rounding errors. The
 * value of 1e-3 is chosen because, in the coverage case, as long as coverage stays within
 * 0.5 * 1/256 of its intended value, it shouldn't affect the final pixel values.
 */
static constexpr float BOUNDS_TOLERANCE = 1e-3f;

OpsCompositor::OpsCompositor(DrawingManager* drawingManager,
                             std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags)
    : drawingManager(drawingManager), renderTarget(std::move(proxy)), renderFlags(renderFlags) {
  DEBUG_ASSERT(drawingManager != nullptr);
  DEBUG_ASSERT(renderTarget != nullptr);
}

void OpsCompositor::fillImage(std::shared_ptr<Image> image, const Rect& rect,
                              const SamplingOptions& sampling, const MCState& state,
                              const FillStyle& style) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(!rect.isEmpty());

  auto clipRect = getClipBounds(state.clip);
  auto invertMatrix = Matrix::I();
  if (!state.matrix.invert(&invertMatrix)) {
    return;
  }

  invertMatrix.mapRect(&clipRect);
  auto finalRect = rect;
  if (!finalRect.intersect(clipRect)) {
    return;
  }

  if (!canAppend(PendingOpType::Image, state.clip, style) || pendingImage != image ||
      pendingSampling != sampling) {
    flushPendingOps(PendingOpType::Image, state.clip, style);
    pendingImage = std::move(image);
    pendingSampling = sampling;
  }
  pendingRects.emplace_back(finalRect, state.matrix, style.color.premultiply());
}

void OpsCompositor::fillRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(!rect.isEmpty());
  if (!canAppend(PendingOpType::Rect, state.clip, style)) {
    flushPendingOps(PendingOpType::Rect, state.clip, style);
  }
  pendingRects.emplace_back(rect, state.matrix, style.color.premultiply());
}

void OpsCompositor::fillRRect(const RRect& rRect, const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(!rRect.rect.isEmpty());
  auto fillStyle = style.makeWithMatrix(state.matrix);
  if (!canAppend(PendingOpType::RRect, state.clip, fillStyle)) {
    flushPendingOps(PendingOpType::RRect, state.clip, fillStyle);
  }
  pendingRRects.emplace_back(rRect, state.matrix, fillStyle.color.premultiply());
}

static Rect ToLocalBounds(const Rect& bounds, const Matrix& viewMatrix) {
  Matrix invertMatrix = {};
  if (!viewMatrix.invert(&invertMatrix)) {
    return Rect::MakeEmpty();
  }
  auto localBounds = bounds;
  invertMatrix.mapRect(&localBounds);
  return localBounds;
}

void OpsCompositor::fillShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const FillStyle& style) {
  DEBUG_ASSERT(shape != nullptr);
  auto maxScale = state.matrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return;
  }
  flushPendingOps();
  auto uvMatrix = Matrix::I();
  if (!state.matrix.invert(&uvMatrix)) {
    return;
  }
  auto localBounds = Rect::MakeEmpty();
  auto deviceBounds = Rect::MakeEmpty();
  auto [needLocalBounds, needDeviceBounds] = needComputeBounds(style);
  auto& clip = state.clip;
  auto clipBounds = getClipBounds(clip);
  if (needLocalBounds) {
    localBounds = shape->isInverseFillType() ? ToLocalBounds(clipBounds, state.matrix)
                                             : shape->getBounds(maxScale);
  }
  shape = Shape::ApplyMatrix(std::move(shape), state.matrix);
  if (needDeviceBounds) {
    deviceBounds = shape->isInverseFillType() ? clipBounds : shape->getBounds();
  }
  auto aaType = getAAType(style);
  auto proxyProvider = renderTarget->getContext()->proxyProvider();
  auto shapeProxy = proxyProvider->createGpuShapeProxy(shape, aaType, clipBounds, renderFlags);
  auto drawOp =
      ShapeDrawOp::Make(std::move(shapeProxy), style.color.premultiply(), uvMatrix, aaType);
  addDrawOp(std::move(drawOp), clip, style, localBounds, deviceBounds);
}

void OpsCompositor::discardAll() {
  ops.clear();
  if (pendingType != PendingOpType::Unknown) {
    pendingType = PendingOpType::Unknown;
    pendingClip = {};
    pendingStyle = {};
    pendingImage = nullptr;
    pendingSampling = {};
    pendingRects.clear();
    pendingRRects.clear();
  }
}

bool OpsCompositor::canAppend(PendingOpType type, const Path& clip, const FillStyle& style) const {
  if (pendingType != type || pendingClip != clip || !pendingStyle.isEqual(style, true)) {
    return false;
  }
  auto maxRecords = static_cast<size_t>(style.antiAlias ? ResourceProvider::MaxNumAAQuads()
                                                        : ResourceProvider::MaxNumNonAAQuads());
  auto size = pendingType == PendingOpType::RRect ? pendingRRects.size() : pendingRects.size();
  return size < maxRecords;
}

void OpsCompositor::flushPendingOps(PendingOpType type, Path clip, FillStyle style) {
  if (pendingType == PendingOpType::Unknown) {
    if (type != PendingOpType::Unknown) {
      pendingType = type;
      pendingClip = clip;
      pendingStyle = style;
    }
    return;
  }
  std::swap(pendingType, type);
  std::swap(pendingClip, clip);
  std::swap(pendingStyle, style);
  std::unique_ptr<DrawOp> drawOp = nullptr;
  auto localBounds = Rect::MakeEmpty();
  auto deviceBounds = Rect::MakeEmpty();
  auto [needLocalBounds, needDeviceBounds] = needComputeBounds(style, type == PendingOpType::Image);
  auto context = renderTarget->getContext();
  auto aaType = getAAType(style);
  switch (type) {
    case PendingOpType::Rect:
      if (pendingRects.size() == 1) {
        auto& paint = pendingRects.front();
        if (drawAsClear(paint.rect, {paint.viewMatrix, clip}, style)) {
          pendingRects.clear();
          return;
        }
      }
    // fallthrough
    case PendingOpType::Image:
      drawOp = RectDrawOp::Make(context, pendingRects, aaType, renderFlags);
      if (needLocalBounds) {
        for (auto& rect : pendingRects) {
          localBounds.join(rect.rect);
        }
      }
      if (needDeviceBounds) {
        for (auto& rectPaint : pendingRects) {
          auto rect = rectPaint.viewMatrix.mapRect(rectPaint.rect);
          deviceBounds.join(rect);
        }
      }
      pendingRects.clear();
      break;
    case PendingOpType::RRect:
      drawOp = RRectDrawOp::Make(context, pendingRRects, aaType, renderFlags);
      if (needLocalBounds || needDeviceBounds) {
        for (auto& rRectPaint : pendingRRects) {
          auto rect = rRectPaint.viewMatrix.mapRect(rRectPaint.rRect.rect);
          deviceBounds.join(rect);
        }
        localBounds = deviceBounds;
      }
      pendingRRects.clear();
      break;
    default:
      break;
  }

  if (type == PendingOpType::Image) {
    FPArgs args = {renderTarget->getContext(), renderFlags, localBounds};
    auto processor = FragmentProcessor::Make(std::move(pendingImage), args, pendingSampling);
    if (processor == nullptr) {
      return;
    }
    drawOp->addColorFP(std::move(processor));
  }
  addDrawOp(std::move(drawOp), clip, style, localBounds, deviceBounds);
}

/**
 * Returns true if the given rect counts as aligned with pixel boundaries.
 */
static bool IsPixelAligned(const Rect& rect) {
  return fabsf(roundf(rect.left) - rect.left) <= BOUNDS_TOLERANCE &&
         fabsf(roundf(rect.top) - rect.top) <= BOUNDS_TOLERANCE &&
         fabsf(roundf(rect.right) - rect.right) <= BOUNDS_TOLERANCE &&
         fabsf(roundf(rect.bottom) - rect.bottom) <= BOUNDS_TOLERANCE;
}

static void FlipYIfNeeded(Rect* rect, std::shared_ptr<RenderTargetProxy> renderTarget) {
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    auto height = rect->height();
    rect->top = static_cast<float>(renderTarget->height()) - rect->bottom;
    rect->bottom = rect->top + height;
  }
}

bool OpsCompositor::drawAsClear(const Rect& rect, const MCState& state, const FillStyle& style) {
  if (!style.hasOnlyColor() || !style.isOpaque() || !state.matrix.rectStaysRect()) {
    return false;
  }
  auto deviceBounds = renderTarget->bounds();
  auto& clip = state.clip;
  auto clipRect = Rect::MakeEmpty();
  if (clip.isInverseFillType()) {
    if (clip.isEmpty()) {
      clipRect = deviceBounds;
    } else {
      return false;
    }
  } else if (!clip.isRect(&clipRect)) {
    return false;
  }
  auto bounds = rect;
  state.matrix.mapRect(&bounds);
  if (!bounds.intersect(clipRect) || !IsPixelAligned(bounds)) {
    return false;
  }
  bounds.round();
  FlipYIfNeeded(&bounds, renderTarget);
  if (bounds == deviceBounds) {
    // discard all previous ops if the clear rect covers the entire render target.
    ops.clear();
  }
  auto format = renderTarget->format();
  auto caps = renderTarget->getContext()->caps();
  const auto& writeSwizzle = caps->getWriteSwizzle(format);
  auto color = writeSwizzle.applyTo(style.color.premultiply());
  auto op = ClearOp::Make(color, bounds);
  if (op != nullptr) {
    ops.push_back(std::move(op));
  }
  return true;
}

void OpsCompositor::makeClosed() {
  if (drawingManager == nullptr) {
    return;
  }
  DEBUG_ASSERT(renderTarget != nullptr);
  flushPendingOps();
  drawingManager->addOpsRenderTask(std::move(renderTarget), std::move(ops));
  drawingManager = nullptr;
}

AAType OpsCompositor::getAAType(const FillStyle& style) const {
  if (renderTarget->sampleCount() > 1) {
    return AAType::MSAA;
  }
  if (style.antiAlias) {
    return AAType::Coverage;
  }
  return AAType::None;
}

std::pair<bool, bool> OpsCompositor::needComputeBounds(const FillStyle& style, bool hasImageFill) {
  bool needLocalBounds = hasImageFill || style.shader != nullptr || style.maskFilter != nullptr;
  bool needDeviceBounds = false;
  if (!BlendModeAsCoeff(style.blendMode)) {
    auto caps = renderTarget->getContext()->caps();
    if (!caps->frameBufferFetchSupport &&
        (!caps->textureBarrierSupport || renderTarget->getTextureProxy() == nullptr ||
         renderTarget->sampleCount() > 1)) {
      needDeviceBounds = true;
    }
  }
  return {needLocalBounds, needDeviceBounds};
}

Rect OpsCompositor::getClipBounds(const Path& clip) {
  if (clip.isInverseFillType()) {
    return renderTarget->bounds();
  }
  return clip.isEmpty() ? Rect::MakeEmpty() : clip.getBounds();
}

std::pair<std::optional<Rect>, bool> OpsCompositor::getClipRect(const Path& clip) {
  auto rect = Rect::MakeEmpty();
  if (clip.isInverseFillType() || !clip.isRect(&rect)) {
    return {std::nullopt, false};
  }
  FlipYIfNeeded(&rect, renderTarget);
  if (IsPixelAligned(rect)) {
    rect.round();
    if (rect != renderTarget->bounds()) {
      return {rect, true};
    }
    return {Rect::MakeEmpty(), false};
  }
  return {rect, false};
}

std::shared_ptr<TextureProxy> OpsCompositor::getClipTexture(const Path& clip, AAType aaType) {
  auto uniqueKey = PathRef::GetUniqueKey(clip);
  if (aaType != AAType::None) {
    static const auto AntialiasFlag = UniqueID::Next();
    uniqueKey = UniqueKey::Append(uniqueKey, &AntialiasFlag, 1);
  }
  if (uniqueKey == clipKey) {
    return clipTexture;
  }
  auto bounds = getClipBounds(clip);
  if (bounds.isEmpty()) {
    return nullptr;
  }
  auto context = renderTarget->getContext();
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto rasterizeMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  if (PathTriangulator::ShouldTriangulatePath(clip)) {
    auto clipBounds = Rect::MakeWH(width, height);
    auto shape = Shape::MakeFrom(clip);
    shape = Shape::ApplyMatrix(std::move(shape), rasterizeMatrix);
    auto proxyProvider = renderTarget->getContext()->proxyProvider();
    auto shapeProxy = proxyProvider->createGpuShapeProxy(shape, aaType, clipBounds, renderFlags);
    auto uvMatrix = Matrix::MakeTrans(bounds.left, bounds.top);
    auto drawOp = ShapeDrawOp::Make(std::move(shapeProxy), Color::White(), uvMatrix, aaType);
    auto clipRenderTarget = RenderTargetProxy::MakeFallback(context, width, height, true);
    if (clipRenderTarget == nullptr) {
      return nullptr;
    }
    clipTexture = clipRenderTarget->getTextureProxy();
    std::vector<std::unique_ptr<Op> > ops = {};
    auto clearOp = ClearOp::Make(Color::Transparent(), clipRenderTarget->bounds());
    ops.push_back(std::move(clearOp));
    ops.push_back(std::move(drawOp));
    drawingManager->addOpsRenderTask(std::move(clipRenderTarget), std::move(ops));
  } else {
    auto rasterizer =
        Rasterizer::MakeFrom(width, height, clip, aaType != AAType::None, rasterizeMatrix);
    clipTexture = context->proxyProvider()->createTextureProxy({}, rasterizer, false, renderFlags);
  }
  clipKey = uniqueKey;
  return clipTexture;
}

std::unique_ptr<FragmentProcessor> OpsCompositor::getClipMaskFP(const Path& clip, AAType aaType,
                                                                Rect* scissorRect) {
  if (clip.isEmpty() && clip.isInverseFillType()) {
    return nullptr;
  }
  auto [rect, useScissor] = getClipRect(clip);
  if (rect.has_value()) {
    if (!rect->isEmpty()) {
      *scissorRect = *rect;
      if (!useScissor) {
        scissorRect->roundOut();
        return AARectEffect::Make(*rect);
      }
    }
    return nullptr;
  }
  auto clipBounds = getClipBounds(clip);
  *scissorRect = clipBounds;
  FlipYIfNeeded(scissorRect, renderTarget);
  scissorRect->roundOut();
  auto textureProxy = getClipTexture(clip, aaType);
  auto uvMatrix = Matrix::MakeTrans(-clipBounds.left, -clipBounds.top);
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    auto flipYMatrix = Matrix::MakeScale(1.0f, -1.0f);
    flipYMatrix.postTranslate(0, -static_cast<float>(renderTarget->height()));
    uvMatrix.preConcat(flipYMatrix);
  }
  return FragmentProcessor::MulInputByChildAlpha(
      DeviceSpaceTextureEffect::Make(std::move(textureProxy), uvMatrix));
}

DstTextureInfo OpsCompositor::makeDstTextureInfo(const Rect& deviceBounds, AAType aaType) {
  auto caps = renderTarget->getContext()->caps();
  if (caps->frameBufferFetchSupport) {
    return {};
  }
  auto bounds = Rect::MakeEmpty();
  auto textureProxy = caps->textureBarrierSupport ? renderTarget->getTextureProxy() : nullptr;
  if (textureProxy == nullptr || renderTarget->sampleCount() > 1) {
    bounds = deviceBounds;
    if (aaType != AAType::None) {
      bounds.outset(1.0f, 1.0f);
    }
    bounds.roundOut();
    if (!bounds.intersect(renderTarget->bounds())) {
      return {};
    }
    FlipYIfNeeded(&bounds, renderTarget);
  }
  DstTextureInfo dstTextureInfo = {};
  if (textureProxy != nullptr) {
    if (renderTarget->sampleCount() > 1) {
      auto resolveOp = ResolveOp::Make(bounds);
      if (resolveOp) {
        ops.push_back(std::move(resolveOp));
      }
    }
    dstTextureInfo.textureProxy = std::move(textureProxy);
    dstTextureInfo.requiresBarrier = true;
    return dstTextureInfo;
  }
  dstTextureInfo.offset = {bounds.x(), bounds.y()};
  auto proxyProvider = renderTarget->getContext()->proxyProvider();
  textureProxy = proxyProvider->createTextureProxy(
      {}, static_cast<int>(bounds.width()), static_cast<int>(bounds.height()),
      renderTarget->format(), false, renderTarget->origin());
  auto dstTextureCopyOp = DstTextureCopyOp::Make(textureProxy, static_cast<int>(bounds.x()),
                                                 static_cast<int>(bounds.y()));
  if (dstTextureCopyOp == nullptr) {
    return {};
  }
  ops.push_back(std::move(dstTextureCopyOp));
  dstTextureInfo.textureProxy = std::move(textureProxy);
  return dstTextureInfo;
}

void OpsCompositor::addDrawOp(std::unique_ptr<DrawOp> op, const Path& clip, const FillStyle& style,
                              const Rect& localBounds, const Rect& deviceBounds) {
  if (op == nullptr) {
    return;
  }
  DEBUG_ASSERT(renderTarget != nullptr);
  FPArgs args = {renderTarget->getContext(), renderFlags, localBounds};
  if (style.shader) {
    if (auto processor = FragmentProcessor::Make(style.shader, args)) {
      op->addColorFP(std::move(processor));
    } else {
      // The shader is the main source of color, so if it fails to create a processor, we can't
      // draw anything.
      return;
    }
  }
  if (style.colorFilter) {
    if (auto processor = style.colorFilter->asFragmentProcessor()) {
      op->addColorFP(std::move(processor));
    }
  }
  if (style.maskFilter) {
    if (auto processor = style.maskFilter->asFragmentProcessor(args, nullptr)) {
      op->addCoverageFP(std::move(processor));
    } else if (auto shaderMaskFilter = Caster::AsShaderMaskFilter(style.maskFilter.get())) {
      if (!shaderMaskFilter->isInverted()) {
        return;
      }
    }
  }
  Rect scissorRect = Rect::MakeEmpty();
  auto aaType = getAAType(style);
  auto clipMask = getClipMaskFP(clip, aaType, &scissorRect);
  if (clipMask) {
    op->addCoverageFP(std::move(clipMask));
  }
  op->setScissorRect(scissorRect);
  op->setBlendMode(style.blendMode);
  if (!BlendModeAsCoeff(style.blendMode)) {
    auto dstTextureInfo = makeDstTextureInfo(deviceBounds, aaType);
    auto xferProcessor = PorterDuffXferProcessor::Make(style.blendMode, std::move(dstTextureInfo));
    op->setXferProcessor(std::move(xferProcessor));
  }
  ops.push_back(std::move(op));
}
}  // namespace tgfx