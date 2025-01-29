/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "RenderContext.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/Rasterizer.h"
#include "core/images/TextureImage.h"
#include "core/utils/Caster.h"
#include "gpu/DrawingManager.h"
#include "gpu/OpContext.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/ClearOp.h"
#include "gpu/ops/RRectDrawOp.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/ops/ShapeDrawOp.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {
/**
 * Defines the maximum distance a draw can extend beyond a clip's boundary and still be considered
 * 'on the other side'. This tolerance accounts for potential floating point rounding errors. The
 * value of 1e-3 is chosen because, in the coverage case, as long as coverage stays within
 * 0.5 * 1/256 of its intended value, it shouldn't affect the final pixel values.
 */
static constexpr float BOUNDS_TOLERANCE = 1e-3f;

static constexpr uint32_t InvalidContentVersion = 0;

RenderContext::RenderContext(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags)
    : opContext(std::move(renderTarget), renderFlags) {
}

Rect RenderContext::getClipBounds(const Path& clip) {
  if (clip.isInverseFillType()) {
    return opContext.bounds();
  }
  return clip.isEmpty() ? Rect::MakeEmpty() : clip.getBounds();
}

static bool HasColorOnly(const FillStyle& style) {
  return style.colorFilter == nullptr && style.shader == nullptr && style.maskFilter == nullptr;
}

static FillStyle ApplyMatrix(const FillStyle& style, const Matrix& matrix) {
  auto fillStyle = style;
  if (fillStyle.shader) {
    fillStyle.shader = fillStyle.shader->makeWithMatrix(matrix);
  }
  if (fillStyle.maskFilter) {
    fillStyle.maskFilter = fillStyle.maskFilter->makeWithMatrix(matrix);
  }
  return fillStyle;
}

void RenderContext::drawStyle(const MCState& state, const FillStyle& style) {
  auto rect = opContext.bounds();
  if (HasColorOnly(style) && style.isOpaque() && state.clip.isEmpty() &&
      state.clip.isInverseFillType()) {
    auto color = style.color.premultiply();
    auto format = opContext.renderTarget->format();
    const auto& writeSwizzle = getContext()->caps()->getWriteSwizzle(format);
    color = writeSwizzle.applyTo(color);
    addOp(ClearOp::Make(color, rect), [] { return true; });
    return;
  }

  drawRect(rect, MCState{state.clip}, ApplyMatrix(style, state.matrix));
}

void RenderContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(!rect.isEmpty());
  auto drawOp = RectDrawOp::Make(style.color.premultiply(), rect, state.matrix);
  addDrawOp(std::move(drawOp), rect, state, style);
}

void RenderContext::drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(!rRect.rect.isEmpty());
  auto drawOp = RRectDrawOp::Make(style.color.premultiply(), rRect, state.matrix);
  addDrawOp(std::move(drawOp), rRect.rect, state, style);
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

void RenderContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const FillStyle& style) {
  DEBUG_ASSERT(shape != nullptr);
  auto maxScale = state.matrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return;
  }
  auto clipBounds = getClipBounds(state.clip);
  auto bounds = shape->isInverseFillType() ? ToLocalBounds(clipBounds, state.matrix)
                                           : shape->getBounds(maxScale);
  auto drawOp =
      ShapeDrawOp::Make(style.color.premultiply(), std::move(shape), state.matrix, clipBounds);
  addDrawOp(std::move(drawOp), bounds, state, style);
}

void RenderContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                              const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(image != nullptr);
  auto rect = Rect::MakeWH(image->width(), image->height());
  return drawImageRect(std::move(image), rect, sampling, state, style);
}

void RenderContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                  const SamplingOptions& sampling, const MCState& state,
                                  const FillStyle& style) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(image->isAlphaOnly() || style.shader == nullptr);
  auto subsetImage = Caster::AsSubsetImage(image.get());
  if (subsetImage != nullptr) {
    // Unwrap the subset image to maximize the merging of draw calls.
    auto& subset = subsetImage->bounds;
    auto newRect = rect;
    newRect.offset(subset.left, subset.top);
    auto newState = state;
    newState.matrix.preTranslate(-subset.left, -subset.top);
    auto offsetMatrix = Matrix::MakeTrans(subset.left, subset.top);
    drawImageRect(subsetImage->source, newRect, sampling, newState,
                  ApplyMatrix(style, offsetMatrix));
    return;
  }
  FPArgs args = {getContext(), opContext.renderFlags, rect, state.matrix};
  auto processor = FragmentProcessor::Make(std::move(image), args, sampling);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = RectDrawOp::Make(style.color.premultiply(), rect, state.matrix);
  drawOp->addColorFP(std::move(processor));
  addDrawOp(std::move(drawOp), rect, state, style);
}

void RenderContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const Stroke* stroke, const MCState& state,
                                     const FillStyle& style) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  if (glyphRunList->hasColor()) {
    drawColorGlyphs(std::move(glyphRunList), state, style);
    return;
  }
  auto maxScale = state.matrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return;
  }
  auto bounds = glyphRunList->getBounds(maxScale);
  if (stroke) {
    stroke->applyToBounds(&bounds);
  }
  auto localBounds = bounds;
  bounds.scale(maxScale, maxScale);
  auto rasterizeMatrix = Matrix::MakeScale(maxScale);
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto aaType = getAAType(style);
  auto rasterizer = Rasterizer::MakeFrom(width, height, std::move(glyphRunList),
                                         aaType == AAType::Coverage, rasterizeMatrix, stroke);
  auto proxyProvider = getContext()->proxyProvider();
  auto textureProxy =
      proxyProvider->createTextureProxy({}, rasterizer, false, opContext.renderFlags);
  auto processor = TextureEffect::Make(std::move(textureProxy), {}, &rasterizeMatrix, true);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = RectDrawOp::Make(style.color.premultiply(), localBounds, state.matrix);
  drawOp->addCoverageFP(std::move(processor));
  addDrawOp(std::move(drawOp), localBounds, state, style);
}

void RenderContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, state);
}

void RenderContext::drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                              const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(style.shader == nullptr);
  Matrix viewMatrix = {};
  Rect bounds = {};
  if (filter || style.maskFilter) {
    if (picture->hasUnboundedFill()) {
      bounds = ToLocalBounds(getClipBounds(state.clip), state.matrix);
    } else {
      bounds = picture->getBounds();
    }
  } else {
    bounds = getClipBounds(state.clip);
    if (!picture->hasUnboundedFill()) {
      auto deviceBounds = picture->getBounds(&state.matrix);
      if (!bounds.intersect(deviceBounds)) {
        return;
      }
    }
    viewMatrix = state.matrix;
  }
  if (bounds.isEmpty()) {
    return;
  }
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  viewMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), width, height, &viewMatrix);
  Matrix invertMatrix = {};
  if (!viewMatrix.invert(&invertMatrix)) {
    return;
  }
  MCState drawState = state;
  drawState.matrix.preConcat(invertMatrix);
  if (filter) {
    auto offset = Point::Zero();
    image = image->makeWithFilter(std::move(filter), &offset);
    if (image == nullptr) {
      return;
    }
    drawState.matrix.preTranslate(offset.x, offset.y);
  }
  drawImage(std::move(image), {}, drawState, style);
}

void RenderContext::drawColorGlyphs(std::shared_ptr<GlyphRunList> glyphRunList,
                                    const MCState& state, const FillStyle& style) {
  auto viewMatrix = state.matrix;
  auto scale = viewMatrix.getMaxScale();
  if (scale <= 0) {
    return;
  }
  viewMatrix.preScale(1.0f / scale, 1.0f / scale);
  for (auto& glyphRun : glyphRunList->glyphRuns()) {
    auto glyphFace = glyphRun.glyphFace;
    glyphFace = glyphFace->makeScaled(scale);
    DEBUG_ASSERT(glyphFace != nullptr);
    auto& glyphIDs = glyphRun.glyphs;
    auto glyphCount = glyphIDs.size();
    auto& positions = glyphRun.positions;
    auto glyphState = state;
    for (size_t i = 0; i < glyphCount; ++i) {
      const auto& glyphID = glyphIDs[i];
      const auto& position = positions[i];
      auto glyphImage = glyphFace->getImage(glyphID, &glyphState.matrix);
      if (glyphImage == nullptr) {
        continue;
      }
      glyphState.matrix.postTranslate(position.x * scale, position.y * scale);
      glyphState.matrix.postConcat(viewMatrix);
      auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
      drawImageRect(std::move(glyphImage), rect, {}, glyphState, style);
    }
  }
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

std::pair<std::optional<Rect>, bool> RenderContext::getClipRect(const Path& clip) {
  auto rect = Rect::MakeEmpty();
  if (clip.isInverseFillType() || !clip.isRect(&rect)) {
    return {{}, false};
  }
  FlipYIfNeeded(&rect, opContext.renderTarget);
  if (IsPixelAligned(rect)) {
    rect.round();
    if (rect != opContext.bounds()) {
      return {rect, true};
    }
    return {Rect::MakeEmpty(), false};
  }
  return {rect, false};
}

std::shared_ptr<TextureProxy> RenderContext::getClipTexture(const Path& clip, AAType aaType) {
  auto uniqueKey = PathRef::GetUniqueKey(clip);
  if (aaType == AAType::Coverage) {
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
    auto drawOp =
        ShapeDrawOp::Make(Color::White(), Shape::MakeFrom(clip), rasterizeMatrix, clipBounds);
    drawOp->setAA(aaType);
    auto renderTarget = RenderTargetProxy::MakeFallback(getContext(), width, height, true, 1, false,
                                                        ImageOrigin::TopLeft, true);
    if (renderTarget == nullptr) {
      return nullptr;
    }
    OpContext context(renderTarget, opContext.renderFlags);
    context.addOp(std::move(drawOp));
    clipTexture = renderTarget->getTextureProxy();
  } else {
    auto rasterizer =
        Rasterizer::MakeFrom(width, height, clip, aaType == AAType::Coverage, rasterizeMatrix);
    auto proxyProvider = getContext()->proxyProvider();
    clipTexture = proxyProvider->createTextureProxy({}, rasterizer, false, opContext.renderFlags);
  }
  clipKey = uniqueKey;
  return clipTexture;
}

std::unique_ptr<FragmentProcessor> RenderContext::getClipMask(const Path& clip,
                                                              const Rect& deviceBounds,
                                                              const Matrix& viewMatrix,
                                                              AAType aaType, Rect* scissorRect) {
  if ((!clip.isEmpty() && clip.contains(deviceBounds)) ||
      (clip.isEmpty() && clip.isInverseFillType())) {
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
  FlipYIfNeeded(scissorRect, opContext.renderTarget);
  scissorRect->roundOut();
  auto textureProxy = getClipTexture(clip, aaType);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto uvMatrix = viewMatrix;
  uvMatrix.postTranslate(-clipBounds.left, -clipBounds.top);
  return TextureEffect::Make(std::move(textureProxy), {}, &uvMatrix, true);
}

void RenderContext::addDrawOp(std::unique_ptr<DrawOp> op, const Rect& localBounds,
                              const MCState& state, const FillStyle& style) {
  if (op == nullptr) {
    return;
  }
  FPArgs args = {getContext(), opContext.renderFlags, localBounds, state.matrix};
  auto isRectOp = op->classID() == RectDrawOp::ClassID();
  auto aaType = getAAType(style);
  if (aaType == AAType::Coverage && isRectOp && args.viewMatrix.rectStaysRect() &&
      IsPixelAligned(op->bounds())) {
    op->setAA(AAType::None);
  } else {
    op->setAA(aaType);
  }
  op->setBlendMode(style.blendMode);
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
    }
  }
  Rect scissorRect = Rect::MakeEmpty();
  auto clipMask = getClipMask(state.clip, op->bounds(), args.viewMatrix, aaType, &scissorRect);
  if (clipMask) {
    op->addCoverageFP(std::move(clipMask));
  }
  op->setScissorRect(scissorRect);
  addOp(std::move(op), [&] { return wouldOverwriteEntireRT(localBounds, state, style, isRectOp); });
}

void RenderContext::addOp(std::unique_ptr<Op> op, const std::function<bool()>& willDiscardContent) {
  if (!aboutToDraw(willDiscardContent)) {
    return;
  }
  opContext.addOp(std::move(op));
  do {
    _contentVersion++;
  } while (InvalidContentVersion == _contentVersion);
}

AAType RenderContext::getAAType(const FillStyle& style) const {
  if (opContext.renderTarget->sampleCount() > 1) {
    return AAType::MSAA;
  }
  if (style.antiAlias) {
    return AAType::Coverage;
  }
  return AAType::None;
}

std::shared_ptr<Image> RenderContext::makeImageSnapshot() {
  if (cachedImage != nullptr) {
    return cachedImage;
  }
  auto renderTargetProxy = renderTarget();
  auto drawingManager = getContext()->drawingManager();
  drawingManager->addTextureResolveTask(renderTargetProxy);
  auto textureProxy = renderTargetProxy->getTextureProxy();
  if (textureProxy != nullptr && !textureProxy->externallyOwned()) {
    cachedImage = TextureImage::Wrap(std::move(textureProxy));
  } else {
    auto textureCopy = renderTargetProxy->makeTextureProxy();
    drawingManager->addRenderTargetCopyTask(renderTargetProxy, textureCopy, opContext.bounds(),
                                            Point::Zero());
    cachedImage = TextureImage::Wrap(std::move(textureCopy));
  }
  return cachedImage;
}

bool RenderContext::aboutToDraw(const std::function<bool()>& willDiscardContent) {
  if (cachedImage == nullptr) {
    return true;
  }
  auto isUnique = cachedImage.use_count() == 1;
  cachedImage = nullptr;
  if (isUnique) {
    return true;
  }
  auto renderTargetProxy = opContext.renderTarget;
  auto textureProxy = renderTargetProxy->getTextureProxy();
  if (textureProxy == nullptr || textureProxy->externallyOwned()) {
    return true;
  }
  auto newRenderTargetProxy = renderTargetProxy->makeRenderTargetProxy();
  if (newRenderTargetProxy == nullptr) {
    LOGE("RenderContext::aboutToDraw(): Failed to make a copy of the renderTarget!");
    return false;
  }
  if (!willDiscardContent()) {
    auto newTextureProxy = newRenderTargetProxy->getTextureProxy();
    auto drawingManager = getContext()->drawingManager();
    drawingManager->addRenderTargetCopyTask(renderTargetProxy, newTextureProxy, opContext.bounds(),
                                            Point::Zero());
  }
  opContext.replaceRenderTarget(std::move(newRenderTargetProxy));
  return true;
}

bool RenderContext::wouldOverwriteEntireRT(const Rect& localBounds, const MCState& state,
                                           const FillStyle& style, bool isRectOp) const {
  if (!isRectOp) {
    return false;
  }
  auto& clip = state.clip;
  auto& viewMatrix = state.matrix;
  auto clipRect = Rect::MakeEmpty();
  if (!clip.isRect(&clipRect) || !viewMatrix.rectStaysRect()) {
    return false;
  }
  auto rtRect = opContext.bounds();
  if (clipRect != rtRect) {
    return false;
  }
  auto deviceRect = viewMatrix.mapRect(localBounds);
  if (!deviceRect.contains(rtRect)) {
    return false;
  }
  return style.isOpaque();
}
}  // namespace tgfx
