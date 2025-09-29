/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/utils/MathExtra.h"
#include "core/utils/RectToRectMatrix.h"
#include "core/utils/ShapeUtils.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/AtlasTextOp.h"
#include "gpu/ops/ShapeDrawOp.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "inspect/InspectorMark.h"
#include "processors/PorterDuffXferProcessor.h"

namespace tgfx {
/**
 * Defines the maximum distance a draw can extend beyond a clip's boundary and still be considered
 * 'on the other side'. This tolerance accounts for potential floating point rounding errors. The
 * value of 1e-3 is chosen because, in the coverage case, as long as coverage stays within
 * 0.5 * 1/256 of its intended value, it shouldn't affect the final pixel values.
 */
static constexpr float BOUNDS_TOLERANCE = 1e-3f;

static bool HasDifferentViewMatrix(const std::vector<PlacementPtr<RectRecord>>& rects) {
  if (rects.size() <= 1) {
    return false;
  }
  auto& firstMatrix = rects.front()->viewMatrix;
  for (auto& record : rects) {
    if (record->viewMatrix != firstMatrix) {
      return true;
    }
  }
  return false;
}

OpsCompositor::OpsCompositor(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                             std::optional<Color> clearColor)
    : context(proxy->getContext()), renderTarget(std::move(proxy)), renderFlags(renderFlags),
      clearColor(clearColor) {
  DEBUG_ASSERT(renderTarget != nullptr);
}

void OpsCompositor::fillImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                              const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  auto imageRect = Rect::MakeWH(image->width(), image->height());
  if (!canAppend(PendingOpType::Image, state.clip, fill) || pendingImage != image ||
      pendingSampling != sampling || pendingConstraint != SrcRectConstraint::Fast) {
    flushPendingOps(PendingOpType::Image, state.clip, fill);
    pendingImage = std::move(image);
    pendingSampling = sampling;
    pendingConstraint = SrcRectConstraint::Fast;
  }
  auto record =
      drawingBuffer()->make<RectRecord>(imageRect, state.matrix, fill.color.premultiply());
  pendingRects.emplace_back(std::move(record));
  pendingUVRects.emplace_back(drawingBuffer()->make<Rect>(imageRect));
}

void OpsCompositor::fillImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                  const Rect& dstRect, const SamplingOptions& sampling,
                                  const MCState& state, const Fill& fill,
                                  SrcRectConstraint constraint) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(!srcRect.isEmpty());
  DEBUG_ASSERT(!dstRect.isEmpty());
  auto fillInLocal = fill.makeWithMatrix(MakeRectToRectMatrix(dstRect, srcRect));
  if (!canAppend(PendingOpType::Image, state.clip, fillInLocal) || pendingImage != image ||
      pendingSampling != sampling || pendingConstraint != constraint) {
    flushPendingOps(PendingOpType::Image, state.clip, fillInLocal);
    pendingImage = std::move(image);
    pendingSampling = sampling;
    pendingConstraint = constraint;
  }
  auto record =
      drawingBuffer()->make<RectRecord>(dstRect, state.matrix, fillInLocal.color.premultiply());
  pendingRects.emplace_back(std::move(record));
  pendingUVRects.emplace_back(drawingBuffer()->make<Rect>(srcRect));
  if (!hasRectToRectDraw && srcRect != dstRect) {
    hasRectToRectDraw = true;
  }
}

void OpsCompositor::fillRect(const Rect& rect, const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(!rect.isEmpty());
  if (!canAppend(PendingOpType::Rect, state.clip, fill)) {
    flushPendingOps(PendingOpType::Rect, state.clip, fill);
  }
  auto record = drawingBuffer()->make<RectRecord>(rect, state.matrix, fill.color.premultiply());
  pendingRects.emplace_back(std::move(record));
}

void OpsCompositor::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                              const Stroke* stroke) {
  DEBUG_ASSERT(!rRect.rect.isEmpty());
  auto rectFill = fill.makeWithMatrix(state.matrix);
  if (!canAppend(PendingOpType::RRect, state.clip, rectFill) ||
      (pendingStrokes.empty() != (stroke == nullptr))) {
    flushPendingOps(PendingOpType::RRect, state.clip, rectFill);
  }
  auto record =
      drawingBuffer()->make<RRectRecord>(rRect, state.matrix, rectFill.color.premultiply());
  pendingRRects.emplace_back(std::move(record));
  if (stroke) {
    auto strokeRecord = drawingBuffer()->make<Stroke>(*stroke);
    pendingStrokes.emplace_back(std::move(strokeRecord));
  }
}

static Rect ToLocalBounds(const Rect& bounds, const Matrix& viewMatrix) {
  Matrix invertMatrix = {};
  if (!viewMatrix.invert(&invertMatrix)) {
    return {};
  }
  auto localBounds = bounds;
  invertMatrix.mapRect(&localBounds);
  return localBounds;
}

static Rect ClipLocalBounds(const Rect& localBounds, const Matrix& viewMatrix,
                            const Rect& clipBounds) {
  auto result = ToLocalBounds(clipBounds, viewMatrix);
  if (!result.intersect(localBounds)) {
    return {};
  }
  return result;
}

void OpsCompositor::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const Fill& fill) {
  DEBUG_ASSERT(shape != nullptr);
  flushPendingOps();
  Matrix uvMatrix = {};
  if (!state.matrix.invert(&uvMatrix)) {
    return;
  }
  std::optional<Rect> localBounds = std::nullopt;
  std::optional<Rect> deviceBounds = std::nullopt;
  float drawScale = 1.0f;
  auto [needLocalBounds, needDeviceBounds] = needComputeBounds(fill, true);
  auto& clip = state.clip;
  auto clipBounds = getClipBounds(clip);
  if (needLocalBounds) {
    if (shape->isInverseFillType()) {
      localBounds = ToLocalBounds(clipBounds, state.matrix);
    } else {
      localBounds = shape->getBounds();
      localBounds = ClipLocalBounds(*localBounds, state.matrix, clipBounds);
    }
    drawScale = std::min(state.matrix.getMaxScale(), 1.0f);
  }
  shape = Shape::ApplyMatrix(std::move(shape), state.matrix);
  if (needDeviceBounds) {
    deviceBounds = shape->isInverseFillType() ? clipBounds : shape->getBounds();
  }
  auto aaType = getAAType(fill);
  auto color = fill.color;
  color.alpha *= ShapeUtils::CalculateAlphaReduceFactorIfHairline(shape);
  auto shapeProxy = proxyProvider()->createGPUShapeProxy(shape, aaType, clipBounds, renderFlags);
  auto drawOp = ShapeDrawOp::Make(std::move(shapeProxy), color.premultiply(), uvMatrix, aaType);
  CAPUTRE_SHAPE_MESH(drawOp.get(), shape, aaType, clipBounds);
  addDrawOp(std::move(drawOp), clip, fill, localBounds, deviceBounds, drawScale);
}

void OpsCompositor::discardAll() {
  drawOps.clear();
  clearColor.reset();
  if (pendingType != PendingOpType::Unknown) {
    resetPendingOps();
  }
}

void OpsCompositor::resetPendingOps(PendingOpType type, Path clip, Fill fill) {
  hasRectToRectDraw = false;
  pendingType = type;
  pendingClip = std::move(clip);
  pendingFill = std::move(fill);
  pendingImage = nullptr;
  pendingSampling = {};
  pendingConstraint = SrcRectConstraint::Fast;
  pendingRects.clear();
  pendingUVRects.clear();
  pendingRRects.clear();
  pendingStrokes.clear();
  pendingAtlasTexture = nullptr;
}

bool OpsCompositor::CompareFill(const Fill& a, const Fill& b) {
  // Ignore the color differences.
  if (a.antiAlias != b.antiAlias || a.blendMode != b.blendMode) {
    return false;
  }
  if (a.shader != b.shader) {
    if (!a.shader || !b.shader || !a.shader->isEqual(b.shader.get())) {
      return false;
    }
  }
  if (a.maskFilter != b.maskFilter) {
    if (!a.maskFilter || !b.maskFilter || !a.maskFilter->isEqual(b.maskFilter.get())) {
      return false;
    }
  }
  if (a.colorFilter != b.colorFilter) {
    if (!a.colorFilter || !b.colorFilter || !a.colorFilter->isEqual(b.colorFilter.get())) {
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
    case PendingOpType::Image:
    case PendingOpType::Atlas:
      return pendingRects.size() < RectDrawOp::MaxNumRects;
    case PendingOpType::RRect:
      return pendingRRects.size() < RRectDrawOp::MaxNumRRects;
    default:
      break;
  }
  return true;
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

static bool RRectUseScale(Context* context) {
  return !context->caps()->shaderCaps()->floatIs32Bits;
}

class PendingOpsAutoReset {
 public:
  PendingOpsAutoReset(OpsCompositor* compositor, PendingOpType type, Path clip, Fill fill)
      : compositor(compositor), type(type), clip(std::move(clip)), fill(std::move(fill)) {
  }

  ~PendingOpsAutoReset() {
    compositor->resetPendingOps(type, std::move(clip), std::move(fill));
  }

 private:
  OpsCompositor* compositor;
  PendingOpType type;
  Path clip;
  Fill fill;
};

void OpsCompositor::flushPendingOps(PendingOpType type, Path clip, Fill fill) {
  if (pendingType == PendingOpType::Unknown) {
    if (type != PendingOpType::Unknown) {
      pendingType = type;
      pendingClip = std::move(clip);
      pendingFill = std::move(fill);
    }
    return;
  }
  PendingOpsAutoReset autoReset(this, type, std::move(clip), std::move(fill));
  PlacementPtr<DrawOp> drawOp = nullptr;
  std::optional<Rect> localBounds = std::nullopt;
  std::optional<Rect> deviceBounds = std::nullopt;
  std::optional<float> drawScale = std::nullopt;
  bool hasCoverage = pendingFill.maskFilter != nullptr || !pendingClip.isEmpty() ||
                     pendingClip.isInverseFillType();
  bool hasImageFill = pendingType == PendingOpType::Image || pendingType == PendingOpType::Atlas;
  auto [needLocalBounds, needDeviceBounds] =
      needComputeBounds(pendingFill, hasCoverage, hasImageFill);
  auto aaType = getAAType(pendingFill);
  Rect clipBounds = {};
  if (needLocalBounds) {
    clipBounds = getClipBounds(pendingClip);
    localBounds = Rect::MakeEmpty();
    drawScale = 0.0f;
  }

  if (needLocalBounds || needDeviceBounds) {
    if (pendingType == PendingOpType::RRect) {
      deviceBounds = Rect::MakeEmpty();
      for (auto& record : pendingRRects) {
        auto rect = record->viewMatrix.mapRect(record->rRect.rect);
        deviceBounds->join(rect);
        drawScale = std::max(*drawScale, record->viewMatrix.getMaxScale());
      }
      localBounds = deviceBounds;
      if (!localBounds->intersect(clipBounds)) {
        localBounds->setEmpty();
      }
    } else {
      if (needLocalBounds) {
        auto rectCount = pendingRects.size();
        for (size_t i = 0; i < rectCount; i++) {
          auto& record = pendingRects[i];
          auto viewMatrix = record->viewMatrix;
          auto rect = &record->rect;
          if (hasRectToRectDraw) {
            auto& uvRect = *pendingUVRects[i];
            viewMatrix.preConcat(MakeRectToRectMatrix(uvRect, record->rect));
            rect = &uvRect;
          }
          localBounds->join(ClipLocalBounds(*rect, viewMatrix, clipBounds));
          drawScale = std::max(*drawScale, viewMatrix.getMaxScale());
        }
      }
      if (needDeviceBounds) {
        deviceBounds = Rect::MakeEmpty();
        for (auto& record : pendingRects) {
          auto rect = record->viewMatrix.mapRect(record->rect);
          deviceBounds->join(rect);
        }
      }
    }
    if (localBounds.has_value() && localBounds->isEmpty()) {
      return;
    }
  }

  switch (pendingType) {
    case PendingOpType::Rect:
      if (pendingRects.size() == 1) {
        auto& paint = pendingRects.front();
        if (drawAsClear(paint->rect, {paint->viewMatrix, pendingClip}, pendingFill)) {
          return;
        }
      }
    // fallthrough
    case PendingOpType::Image: {
      auto subsetMode = UVSubsetMode::None;
      if (pendingConstraint == SrcRectConstraint::Strict && pendingImage) {
        subsetMode = pendingSampling.filterMode == FilterMode::Linear
                         ? UVSubsetMode::SubsetOnly
                         : UVSubsetMode::RoundOutAndSubset;
      }
      bool needUVCoord =
          needLocalBounds && (hasRectToRectDraw || HasDifferentViewMatrix(pendingRects));
      auto uvRects =
          hasRectToRectDraw ? std::move(pendingUVRects) : std::vector<PlacementPtr<Rect>>();
      auto provider =
          RectsVertexProvider::MakeFrom(drawingBuffer(), std::move(pendingRects),
                                        std::move(uvRects), aaType, needUVCoord, subsetMode);
      drawOp = RectDrawOp::Make(context, std::move(provider), renderFlags);
    } break;
    case PendingOpType::RRect: {
      auto provider =
          RRectsVertexProvider::MakeFrom(drawingBuffer(), std::move(pendingRRects), aaType,
                                         RRectUseScale(context), std::move(pendingStrokes));
      drawOp = RRectDrawOp::Make(context, std::move(provider), renderFlags);
    } break;
    case PendingOpType::Atlas: {
      auto provider = RectsVertexProvider::MakeFrom(drawingBuffer(), std::move(pendingRects), {},
                                                    AAType::None, true, UVSubsetMode::None);
      drawOp = AtlasTextOp::Make(context, std::move(provider), renderFlags,
                                 std::move(pendingAtlasTexture), pendingSampling);
    } break;
    default:
      break;
  }
  if (drawOp != nullptr && pendingType == PendingOpType::Image) {
    FPArgs args = {context, renderFlags, localBounds.value_or(Rect::MakeEmpty()),
                   drawScale.value_or(1.0f)};
    auto processor =
        FragmentProcessor::Make(std::move(pendingImage), args, pendingSampling, pendingConstraint);
    if (processor == nullptr) {
      return;
    }
    drawOp->addColorFP(std::move(processor));
  }
  addDrawOp(std::move(drawOp), pendingClip, pendingFill, localBounds, deviceBounds,
            drawScale.value_or(1.0f));
}

static void FlipYIfNeeded(Rect* rect, const RenderTargetProxy* renderTarget) {
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    renderTarget->getOriginTransform().mapRect(rect);
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
  Rect clipRect = {};
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
  if (bounds != deviceBounds) {
    return false;
  }
  // discard all previous ops since the clear rect covers the entire render target.
  drawOps.clear();
  auto format = renderTarget->format();
  auto caps = context->caps();
  auto& writeSwizzle = caps->getWriteSwizzle(format);
  clearColor = writeSwizzle.applyTo(fill.color.premultiply());
  return true;
}

void OpsCompositor::makeClosed() {
  if (renderTarget == nullptr) {
    return;
  }
  flushPendingOps();
  submitDrawOps();
  renderTarget = nullptr;
  // Remove the compositor from the list, so it won't be flushed again.
  context->drawingManager()->compositors.erase(cachedPosition);
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

std::pair<bool, bool> OpsCompositor::needComputeBounds(const Fill& fill, bool hasCoverage,
                                                       bool hasImageFill) {
  bool needLocalBounds = hasImageFill || fill.shader != nullptr || fill.maskFilter != nullptr;
  bool needDeviceBounds = false;
  if (BlendModeNeedDstTexture(fill.blendMode, hasCoverage)) {
    auto caps = context->caps();
    if (!caps->shaderCaps()->frameBufferFetchSupport &&
        (!caps->textureBarrierSupport || renderTarget->asTextureProxy() == nullptr ||
         renderTarget->sampleCount() > 1)) {
      needDeviceBounds = true;
    }
  }
  if (pendingType == PendingOpType::RRect && (needDeviceBounds || needLocalBounds)) {
    // When either localBounds or deviceBounds needs to be computed for RRect, both should be set to
    // true, since localBounds and deviceBounds are computed together in that case.
    needLocalBounds = true;
    needDeviceBounds = true;
  }
  return {needLocalBounds, needDeviceBounds};
}

Rect OpsCompositor::getClipBounds(const Path& clip) {
  if (clip.isInverseFillType()) {
    return renderTarget->bounds();
  }
  auto bounds = clip.getBounds();
  if (!bounds.intersect(renderTarget->bounds())) {
    bounds.setEmpty();
  }
  return bounds;
}

std::pair<std::optional<Rect>, bool> OpsCompositor::getClipRect(const Path& clip) {
  Rect rect = {};
  if (clip.isInverseFillType() || !clip.isRect(&rect)) {
    return {std::nullopt, false};
  }
  FlipYIfNeeded(&rect, renderTarget.get());
  if (IsPixelAligned(rect)) {
    rect.round();
    if (rect != renderTarget->bounds()) {
      return {rect, true};
    }
    // Cannot return '{}' as an empty Rect, since it would be interpreted as std::nullopt.
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
    auto shapeProxy = proxyProvider()->createGPUShapeProxy(shape, aaType, clipBounds, renderFlags);
    auto uvMatrix = Matrix::MakeTrans(bounds.left, bounds.top);
    auto drawOp = ShapeDrawOp::Make(std::move(shapeProxy), {}, uvMatrix, aaType);
    CAPUTRE_SHAPE_MESH(drawOp.get(), shape, aaType, clipBounds);
    auto clipRenderTarget = RenderTargetProxy::MakeFallback(
        context, width, height, true, 1, false, ImageOrigin::TopLeft, BackingFit::Approx);
    if (clipRenderTarget == nullptr) {
      return nullptr;
    }
    clipTexture = clipRenderTarget->asTextureProxy();
    auto opList = drawingBuffer()->makeArray<DrawOp>(&drawOp, 1);
    context->drawingManager()->addOpsRenderTask(std::move(clipRenderTarget), std::move(opList),
                                                Color::Transparent());
  } else {
    auto rasterizer =
        PathRasterizer::MakeFrom(width, height, clip, aaType != AAType::None, &rasterizeMatrix);
    clipTexture = proxyProvider()->createTextureProxy(rasterizer, false, renderFlags);
  }
  clipKey = uniqueKey;
  return clipTexture;
}

std::pair<PlacementPtr<FragmentProcessor>, bool> OpsCompositor::getClipMaskFP(const Path& clip,
                                                                              AAType aaType,
                                                                              Rect* scissorRect) {
  if (clip.isEmpty() && clip.isInverseFillType()) {
    return {nullptr, false};
  }
  auto buffer = context->drawingBuffer();
  auto [rect, useScissor] = getClipRect(clip);
  if (rect.has_value()) {
    if (!rect->isEmpty()) {
      *scissorRect = *rect;
      if (!useScissor) {
        scissorRect->roundOut();
        return {AARectEffect::Make(buffer, *rect), true};
      }
    }
    return {nullptr, false};
  }
  auto clipBounds = getClipBounds(clip);
  *scissorRect = clipBounds;
  FlipYIfNeeded(scissorRect, renderTarget.get());
  scissorRect->roundOut();
  auto textureProxy = getClipTexture(clip, aaType);
  auto uvMatrix = Matrix::MakeTrans(-clipBounds.left, -clipBounds.top);
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    uvMatrix.preConcat(renderTarget->getOriginTransform());
  }
  auto processor = DeviceSpaceTextureEffect::Make(buffer, std::move(textureProxy), uvMatrix);
  return {FragmentProcessor::MulInputByChildAlpha(buffer, std::move(processor)), true};
}

DstTextureInfo OpsCompositor::makeDstTextureInfo(const Rect& deviceBounds, AAType aaType) {
  auto caps = context->caps();
  if (caps->shaderCaps()->frameBufferFetchSupport) {
    return {};
  }
  Rect bounds = {};
  auto textureProxy = caps->textureBarrierSupport ? renderTarget->asTextureProxy() : nullptr;
  if (textureProxy == nullptr || renderTarget->sampleCount() > 1) {
    if (deviceBounds.isEmpty()) {
      return {};
    }
    bounds = deviceBounds;
    if (aaType != AAType::None) {
      bounds.outset(1.0f, 1.0f);
    }
    bounds.roundOut();
    if (!bounds.intersect(renderTarget->bounds())) {
      return {};
    }
    FlipYIfNeeded(&bounds, renderTarget.get());
  }
  DstTextureInfo dstTextureInfo = {};
  if (textureProxy != nullptr) {
    if (renderTarget->sampleCount() > 1) {
      // Submit draw ops immediately to ensure the MSAA render target is resolved.
      submitDrawOps();
    }
    dstTextureInfo.textureProxy = std::move(textureProxy);
    return dstTextureInfo;
  }
  submitDrawOps();
  dstTextureInfo.offset = {bounds.x(), bounds.y()};
  textureProxy = proxyProvider()->createTextureProxy(
      {}, static_cast<int>(bounds.width()), static_cast<int>(bounds.height()),
      renderTarget->format(), false, renderTarget->origin(), BackingFit::Approx);
  if (textureProxy == nullptr) {
    return {};
  }
  context->drawingManager()->addRenderTargetCopyTask(
      renderTarget, textureProxy, static_cast<int>(bounds.x()), static_cast<int>(bounds.y()));
  dstTextureInfo.textureProxy = std::move(textureProxy);
  return dstTextureInfo;
}

void OpsCompositor::addDrawOp(PlacementPtr<DrawOp> op, const Path& clip, const Fill& fill,
                              const std::optional<Rect>& localBounds,
                              const std::optional<Rect>& deviceBounds, float drawScale) {

  if (op == nullptr || fill.nothingToDraw() || (clip.isEmpty() && !clip.isInverseFillType())) {
    return;
  }
  DEBUG_ASSERT(renderTarget != nullptr);
  if (localBounds.has_value() && localBounds->isEmpty()) {
    return;
  }

  FPArgs args = {context, renderFlags, localBounds.value_or(Rect::MakeEmpty()), drawScale};
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
  Rect scissorRect = {};
  auto aaType = getAAType(fill);
  auto [clipMask, hasMask] = getClipMaskFP(clip, aaType, &scissorRect);
  if (hasMask) {
    if (!clipMask) {
      return;
    }
    op->addCoverageFP(std::move(clipMask));
  }
  op->setScissorRect(scissorRect);
  op->setBlendMode(fill.blendMode);
  if (BlendModeNeedDstTexture(fill.blendMode, op->hasCoverage())) {
    auto dstTextureInfo = makeDstTextureInfo(deviceBounds.value_or(Rect::MakeEmpty()), aaType);
    auto shaderCaps = context->caps()->shaderCaps();
    if (!shaderCaps->frameBufferFetchSupport && dstTextureInfo.textureProxy == nullptr) {
      return;
    }
    auto xferProcessor =
        PorterDuffXferProcessor::Make(drawingBuffer(), fill.blendMode, std::move(dstTextureInfo));
    op->setXferProcessor(std::move(xferProcessor));
  }
  drawOps.emplace_back(std::move(op));
}

void OpsCompositor::fillTextAtlas(std::shared_ptr<TextureProxy> textureProxy, const Rect& rect,
                                  const SamplingOptions& sampling, const MCState& state,
                                  const Fill& fill) {
  DEBUG_ASSERT(textureProxy != nullptr);
  DEBUG_ASSERT(!rect.isEmpty());
  if (!canAppend(PendingOpType::Atlas, state.clip, fill) || pendingAtlasTexture != textureProxy ||
      pendingSampling != sampling) {
    flushPendingOps(PendingOpType::Atlas, state.clip, fill);
    pendingAtlasTexture = std::move(textureProxy);
    pendingSampling = sampling;
  }
  auto record = drawingBuffer()->make<RectRecord>(rect, state.matrix, fill.color.premultiply());
  pendingRects.emplace_back(std::move(record));
}

void OpsCompositor::submitDrawOps() {
  auto opArray = drawingBuffer()->makeArray(std::move(drawOps));
  context->drawingManager()->addOpsRenderTask(renderTarget, std::move(opArray), clearColor);
  clearColor.reset();
}

}  // namespace tgfx
