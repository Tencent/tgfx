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

OpsCompositor::OpsCompositor(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags)
    : context(proxy->getContext()), renderTarget(std::move(proxy)), renderFlags(renderFlags) {
  DEBUG_ASSERT(renderTarget != nullptr);
}

void OpsCompositor::fillImage(std::shared_ptr<Image> image, const Rect& rect,
                              const SamplingOptions& sampling, const MCState& state,
                              const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(!rect.isEmpty());
  if (!canAppend(PendingOpType::Image, state.clip, fill) || pendingImage != image ||
      pendingSampling != sampling) {
    flushPendingOps(PendingOpType::Image, state.clip, fill);
    pendingImage = std::move(image);
    pendingSampling = sampling;
  }
  auto rectPaint =
      drawingBuffer()->makeNode<RectPaint>(rect, state.matrix, fill.color.premultiply());
  pendingRects.append(std::move(rectPaint));
}

void OpsCompositor::fillRect(const Rect& rect, const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(!rect.isEmpty());
  if (!canAppend(PendingOpType::Rect, state.clip, fill)) {
    flushPendingOps(PendingOpType::Rect, state.clip, fill);
  }
  auto rectPaint =
      drawingBuffer()->makeNode<RectPaint>(rect, state.matrix, fill.color.premultiply());
  pendingRects.append(std::move(rectPaint));
}

void OpsCompositor::fillRRect(const RRect& rRect, const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(!rRect.rect.isEmpty());
  auto rectFill = fill.makeWithMatrix(state.matrix);
  if (!canAppend(PendingOpType::RRect, state.clip, rectFill)) {
    flushPendingOps(PendingOpType::RRect, state.clip, rectFill);
  }
  auto rectPaint =
      drawingBuffer()->makeNode<RRectPaint>(rRect, state.matrix, rectFill.color.premultiply());
  pendingRRects.append(std::move(rectPaint));
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

static Rect ClipLocalBounds(const Rect& localBounds, const Matrix& viewMatrix,
                            const Rect& clipBounds) {
  auto result = ToLocalBounds(clipBounds, viewMatrix);
  if (!result.intersect(localBounds)) {
    return Rect::MakeEmpty();
  }
  return result;
}

void OpsCompositor::fillShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const Fill& fill) {
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
  auto [needLocalBounds, needDeviceBounds] = needComputeBounds(fill);
  auto& clip = state.clip;
  auto clipBounds = getClipBounds(clip);
  if (needLocalBounds) {
    if (shape->isInverseFillType()) {
      localBounds = ToLocalBounds(clipBounds, state.matrix);
    } else {
      localBounds = shape->getBounds(maxScale);
      localBounds = ClipLocalBounds(localBounds, state.matrix, clipBounds);
    }
  }
  shape = Shape::ApplyMatrix(std::move(shape), state.matrix);
  if (needDeviceBounds) {
    deviceBounds = shape->isInverseFillType() ? clipBounds : shape->getBounds();
  }
  auto aaType = getAAType(fill);
  auto shapeProxy = proxyProvider()->createGpuShapeProxy(shape, aaType, clipBounds, renderFlags);
  auto drawOp =
      ShapeDrawOp::Make(std::move(shapeProxy), fill.color.premultiply(), uvMatrix, aaType);
  addDrawOp(std::move(drawOp), clip, fill, localBounds, deviceBounds);
}

void OpsCompositor::discardAll() {
  ops.clear();
  if (pendingType != PendingOpType::Unknown) {
    pendingType = PendingOpType::Unknown;
    pendingClip = {};
    pendingFill = {};
    pendingImage = nullptr;
    pendingSampling = {};
    pendingRects.clear();
    pendingRRects.clear();
  }
}

static bool CompareFill(const Fill& a, const Fill& b) {
  // Ignore the color differences.
  if (a.antiAlias != b.antiAlias || a.blendMode != b.blendMode) {
    return false;
  }
  if (a.shader != b.shader) {
    if (!a.shader || !b.shader || !Caster::Compare(a.shader.get(), b.shader.get())) {
      return false;
    }
  }
  if (a.maskFilter != b.maskFilter) {
    if (!a.maskFilter || !b.maskFilter ||
        !Caster::Compare(a.maskFilter.get(), b.maskFilter.get())) {
      return false;
    }
  }
  if (a.colorFilter != b.colorFilter) {
    if (!a.colorFilter || !b.colorFilter ||
        !Caster::Compare(a.colorFilter.get(), b.colorFilter.get())) {
      return false;
    }
  }
  return true;
}

bool OpsCompositor::canAppend(PendingOpType type, const Path& clip, const Fill& fill) const {
  if (pendingType != type || !pendingClip.isSame(clip) || !CompareFill(pendingFill, fill)) {
    return false;
  }
  switch (pendingType) {
    case PendingOpType::Rect:
      if (fill.antiAlias) {
        return pendingRects.size() < RectDrawOp::MaxNumAARects;
      }
      return pendingRects.size() < RectDrawOp::MaxNumNonAARects;
    case PendingOpType::RRect:
      return pendingRRects.size() < RRectDrawOp::MaxNumRRects;
    default:
      break;
  }
  return true;
}

void OpsCompositor::flushPendingOps(PendingOpType type, Path clip, Fill fill) {
  if (pendingType == PendingOpType::Unknown) {
    if (type != PendingOpType::Unknown) {
      pendingType = type;
      pendingClip = clip;
      pendingFill = fill;
    }
    return;
  }
  std::swap(pendingType, type);
  std::swap(pendingClip, clip);
  std::swap(pendingFill, fill);
  PlacementNode<DrawOp> drawOp = nullptr;
  auto localBounds = Rect::MakeEmpty();
  auto deviceBounds = Rect::MakeEmpty();
  auto [needLocalBounds, needDeviceBounds] = needComputeBounds(fill, type == PendingOpType::Image);
  auto aaType = getAAType(fill);
  auto clipBounds = Rect::MakeEmpty();
  if (needLocalBounds) {
    clipBounds = getClipBounds(clip);
  }
  switch (type) {
    case PendingOpType::Rect:
      if (pendingRects.size() == 1) {
        auto paint = pendingRects.front();
        if (drawAsClear(paint.rect, {paint.viewMatrix, clip}, fill)) {
          pendingRects.clear();
          return;
        }
      }
    // fallthrough
    case PendingOpType::Image: {
      if (needLocalBounds) {
        for (auto& rect : pendingRects) {
          localBounds.join(ClipLocalBounds(rect.rect, rect.viewMatrix, clipBounds));
        }
      }
      if (needDeviceBounds) {
        for (auto& rectPaint : pendingRects) {
          auto rect = rectPaint.viewMatrix.mapRect(rectPaint.rect);
          deviceBounds.join(rect);
        }
      }
      drawOp =
          RectDrawOp::Make(context, std::move(pendingRects), needLocalBounds, aaType, renderFlags);
    } break;
    case PendingOpType::RRect: {
      if (needLocalBounds || needDeviceBounds) {
        for (auto& rRectPaint : pendingRRects) {
          auto rect = rRectPaint.viewMatrix.mapRect(rRectPaint.rRect.rect);
          deviceBounds.join(rect);
        }
        localBounds = deviceBounds;
        if (!localBounds.intersect(clipBounds)) {
          localBounds = Rect::MakeEmpty();
        }
      }
      drawOp = RRectDrawOp::Make(context, std::move(pendingRRects), aaType, renderFlags);
    } break;
    default:
      break;
  }

  if (type == PendingOpType::Image) {
    FPArgs args = {context, renderFlags, localBounds};
    auto processor = FragmentProcessor::Make(std::move(pendingImage), args, pendingSampling);
    if (processor == nullptr) {
      return;
    }
    drawOp->addColorFP(std::move(processor));
  }
  addDrawOp(std::move(drawOp), clip, fill, localBounds, deviceBounds);
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

static bool HasColorOnly(const Fill& fill) {
  return !fill.shader && !fill.maskFilter && !fill.colorFilter;
}

bool OpsCompositor::drawAsClear(const Rect& rect, const MCState& state, const Fill& fill) {
  if (!HasColorOnly(fill) || !fill.isOpaque() || !state.matrix.rectStaysRect()) {
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
  auto caps = context->caps();
  const auto& writeSwizzle = caps->getWriteSwizzle(format);
  auto color = writeSwizzle.applyTo(fill.color.premultiply());
  auto op = ClearOp::Make(context, color, bounds);
  if (op != nullptr) {
    ops.append(std::move(op));
  }
  return true;
}

void OpsCompositor::makeClosed() {
  if (renderTarget == nullptr) {
    return;
  }
  flushPendingOps();
  auto drawingManager = context->drawingManager();
  drawingManager->addOpsRenderTask(std::move(renderTarget), std::move(ops));
  // Remove the compositor from the list, so it won't be flushed again.
  drawingManager->compositors.erase(cachedPosition);
}

AAType OpsCompositor::getAAType(const Fill& fill) const {
  if (renderTarget->sampleCount() > 1) {
    return AAType::MSAA;
  }
  if (fill.antiAlias) {
    return AAType::Coverage;
  }
  return AAType::None;
}

std::pair<bool, bool> OpsCompositor::needComputeBounds(const Fill& fill, bool hasImageFill) {
  bool needLocalBounds = hasImageFill || fill.shader != nullptr || fill.maskFilter != nullptr;
  bool needDeviceBounds = false;
  if (!BlendModeAsCoeff(fill.blendMode)) {
    auto caps = context->caps();
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
  auto bounds = clip.getBounds();
  if (!bounds.intersect(renderTarget->bounds())) {
    return Rect::MakeEmpty();
  }
  return bounds;
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
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto rasterizeMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  if (PathTriangulator::ShouldTriangulatePath(clip)) {
    auto clipBounds = Rect::MakeWH(width, height);
    auto shape = Shape::MakeFrom(clip);
    shape = Shape::ApplyMatrix(std::move(shape), rasterizeMatrix);
    auto shapeProxy = proxyProvider()->createGpuShapeProxy(shape, aaType, clipBounds, renderFlags);
    auto uvMatrix = Matrix::MakeTrans(bounds.left, bounds.top);
    auto drawOp = ShapeDrawOp::Make(std::move(shapeProxy), Color::White(), uvMatrix, aaType);
    auto clipRenderTarget = RenderTargetProxy::MakeFallback(context, width, height, true);
    if (clipRenderTarget == nullptr) {
      return nullptr;
    }
    clipTexture = clipRenderTarget->getTextureProxy();
    auto clearOp = ClearOp::Make(context, Color::Transparent(), clipRenderTarget->bounds());
    PlacementList<Op> ops = {std::move(clearOp)};
    ops.append(std::move(drawOp));
    context->drawingManager()->addOpsRenderTask(std::move(clipRenderTarget), std::move(ops));
  } else {
    auto rasterizer =
        Rasterizer::MakeFrom(width, height, clip, aaType != AAType::None, rasterizeMatrix);
    clipTexture = proxyProvider()->createTextureProxy({}, rasterizer, false, renderFlags);
  }
  clipKey = uniqueKey;
  return clipTexture;
}

PlacementPtr<FragmentProcessor> OpsCompositor::getClipMaskFP(const Path& clip, AAType aaType,
                                                             Rect* scissorRect) {
  if (clip.isEmpty() && clip.isInverseFillType()) {
    return nullptr;
  }
  auto buffer = context->drawingBuffer();
  auto [rect, useScissor] = getClipRect(clip);
  if (rect.has_value()) {
    if (!rect->isEmpty()) {
      *scissorRect = *rect;
      if (!useScissor) {
        scissorRect->roundOut();
        return AARectEffect::Make(buffer, *rect);
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
  auto processor = DeviceSpaceTextureEffect::Make(buffer, std::move(textureProxy), uvMatrix);
  return FragmentProcessor::MulInputByChildAlpha(buffer, std::move(processor));
}

DstTextureInfo OpsCompositor::makeDstTextureInfo(const Rect& deviceBounds, AAType aaType) {
  auto caps = context->caps();
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
      auto resolveOp = ResolveOp::Make(context, bounds);
      if (resolveOp) {
        ops.append(std::move(resolveOp));
      }
    }
    dstTextureInfo.textureProxy = std::move(textureProxy);
    dstTextureInfo.requiresBarrier = true;
    return dstTextureInfo;
  }
  dstTextureInfo.offset = {bounds.x(), bounds.y()};
  textureProxy = proxyProvider()->createTextureProxy(
      {}, static_cast<int>(bounds.width()), static_cast<int>(bounds.height()),
      renderTarget->format(), false, renderTarget->origin());
  auto dstTextureCopyOp = DstTextureCopyOp::Make(textureProxy, static_cast<int>(bounds.x()),
                                                 static_cast<int>(bounds.y()));
  if (dstTextureCopyOp == nullptr) {
    return {};
  }
  ops.append(std::move(dstTextureCopyOp));
  dstTextureInfo.textureProxy = std::move(textureProxy);
  return dstTextureInfo;
}

void OpsCompositor::addDrawOp(PlacementNode<DrawOp> op, const Path& clip, const Fill& fill,
                              const Rect& localBounds, const Rect& deviceBounds) {
  if (op == nullptr || fill.nothingToDraw()) {
    return;
  }
  DEBUG_ASSERT(renderTarget != nullptr);
  FPArgs args = {context, renderFlags, localBounds};
  if (fill.shader) {
    if (auto processor = FragmentProcessor::Make(fill.shader, args)) {
      op->addColorFP(std::move(processor));
    } else {
      // The shader is the main source of color, so if it fails to create a processor, we can't
      // draw anything.
      return;
    }
  }
  if (fill.colorFilter) {
    if (auto processor = fill.colorFilter->asFragmentProcessor(context)) {
      op->addColorFP(std::move(processor));
    }
  }
  if (fill.maskFilter) {
    if (auto processor = fill.maskFilter->asFragmentProcessor(args, nullptr)) {
      op->addCoverageFP(std::move(processor));
    } else {
      // if mask is empty, nothing to draw
      return;
    }
  }
  Rect scissorRect = Rect::MakeEmpty();
  auto aaType = getAAType(fill);
  auto clipMask = getClipMaskFP(clip, aaType, &scissorRect);
  if (clipMask) {
    op->addCoverageFP(std::move(clipMask));
  }
  op->setScissorRect(scissorRect);
  op->setBlendMode(fill.blendMode);
  if (!BlendModeAsCoeff(fill.blendMode)) {
    auto dstTextureInfo = makeDstTextureInfo(deviceBounds, aaType);
    auto xferProcessor =
        PorterDuffXferProcessor::Make(drawingBuffer(), fill.blendMode, std::move(dstTextureInfo));
    op->setXferProcessor(std::move(xferProcessor));
  }
  ops.append(std::move(op));
}
}  // namespace tgfx