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

RenderContext::RenderContext(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                             uint32_t renderFlags)
    : renderFlags(renderFlags) {
  opContext = new OpContext(std::move(renderTargetProxy), renderFlags);
}

RenderContext::RenderContext(Surface* surface) : surface(surface) {
  renderFlags = surface->_renderFlags;
  opContext = new OpContext(surface->renderTargetProxy, renderFlags);
}

RenderContext::~RenderContext() {
  delete opContext;
}

Context* RenderContext::getContext() const {
  return opContext->renderTarget()->getContext();
}

Rect RenderContext::getClipBounds(const Path& clip) {
  if (clip.isInverseFillType()) {
    return Rect::MakeWH(opContext->renderTarget()->width(), opContext->renderTarget()->height());
  }
  return clip.isEmpty() ? Rect::MakeEmpty() : clip.getBounds();
}

void RenderContext::drawStyle(const MCState& state, const FillStyle& style) {
  auto fillStyle = style;
  if (fillStyle.shader) {
    fillStyle.shader = fillStyle.shader->makeWithMatrix(state.matrix);
  }
  if (fillStyle.maskFilter) {
    fillStyle.maskFilter = fillStyle.maskFilter->makeWithMatrix(state.matrix);
  }
  drawRect(Rect::MakeWH(opContext->renderTarget()->width(), opContext->renderTarget()->height()),
           MCState{state.clip}, fillStyle);
}

void RenderContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(!rect.isEmpty());
  if (drawAsClear(rect, state, style)) {
    return;
  }
  auto drawOp = RectDrawOp::Make(style.color.premultiply(), rect, state.matrix);
  addDrawOp(std::move(drawOp), rect, state, style);
}

static bool HasColorOnly(const FillStyle& style) {
  return style.colorFilter == nullptr && style.shader == nullptr && style.maskFilter == nullptr;
}

bool RenderContext::drawAsClear(const Rect& rect, const MCState& state, const FillStyle& style) {
  if (!HasColorOnly(style) || !style.isOpaque() || !state.matrix.rectStaysRect()) {
    return false;
  }
  auto color = style.color.premultiply();
  auto bounds = rect;
  state.matrix.mapRect(&bounds);
  auto [clipRect, useScissor] = getClipRect(state.clip, &bounds);
  if (clipRect.has_value()) {
    auto format = opContext->renderTarget()->format();
    const auto& writeSwizzle = getContext()->caps()->getWriteSwizzle(format);
    color = writeSwizzle.applyTo(color);
    if (useScissor) {
      addOp(ClearOp::Make(color, *clipRect), [] { return false; });
      return true;
    }
    if (clipRect->isEmpty()) {
      addOp(ClearOp::Make(color, bounds), [] { return true; });
      return true;
    }
  }
  return false;
}

void RenderContext::drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(!rRect.rect.isEmpty());
  auto drawOp = RRectDrawOp::Make(style.color.premultiply(), rRect, state.matrix);
  addDrawOp(std::move(drawOp), rRect.rect, state, style);
}

void RenderContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const FillStyle& style) {
  DEBUG_ASSERT(shape != nullptr);
  auto maxScale = state.matrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return;
  }
  auto bounds = shape->getBounds(maxScale);
  auto clipBounds = getClipBounds(state.clip);
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
  auto uvMatrix = Matrix::I();
  auto subsetImage = Caster::AsSubsetImage(image.get());
  if (subsetImage != nullptr) {
    uvMatrix = Matrix::MakeTrans(subsetImage->bounds.left, subsetImage->bounds.top);
    image = subsetImage->source;
  }
  FPArgs args = {getContext(), renderFlags, rect, state.matrix};
  auto processor = FragmentProcessor::Make(std::move(image), args, sampling);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = RectDrawOp::Make(style.color.premultiply(), rect, state.matrix, uvMatrix);
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
  auto textureProxy = proxyProvider->createTextureProxy({}, rasterizer, false, renderFlags);
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
  auto bounds = Rect::MakeEmpty();
  if (filter || style.maskFilter) {
    if (picture->hasUnboundedFill()) {
      Matrix invertMatrix = {};
      if (state.matrix.invert(&invertMatrix)) {
        bounds = getClipBounds(state.clip);
        invertMatrix.mapRect(&bounds);
      }
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

static void FlipYIfNeeded(Rect* rect, const RenderTargetProxy* renderTarget) {
  if (renderTarget->origin() == ImageOrigin::BottomLeft) {
    auto height = rect->height();
    rect->top = static_cast<float>(renderTarget->height()) - rect->bottom;
    rect->bottom = rect->top + height;
  }
}

std::pair<std::optional<Rect>, bool> RenderContext::getClipRect(const Path& clip,
                                                                const Rect* deviceBounds) {
  auto rect = Rect::MakeEmpty();
  if (clip.isRect(&rect)) {
    if (deviceBounds != nullptr && !rect.intersect(*deviceBounds)) {
      return {{}, false};
    }
    auto renderTarget = opContext->renderTarget();
    FlipYIfNeeded(&rect, renderTarget);
    if (IsPixelAligned(rect)) {
      rect.round();
      if (rect != Rect::MakeWH(renderTarget->width(), renderTarget->height())) {
        return {rect, true};
      } else {
        return {Rect::MakeEmpty(), false};
      }
    } else {
      return {rect, false};
    }
  }
  return {{}, false};
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
    OpContext context(renderTarget, renderFlags);
    context.addOp(std::move(drawOp));
    clipTexture = renderTarget->getTextureProxy();
  } else {
    auto rasterizer =
        Rasterizer::MakeFrom(width, height, clip, aaType == AAType::Coverage, rasterizeMatrix);
    auto proxyProvider = getContext()->proxyProvider();
    clipTexture = proxyProvider->createTextureProxy({}, rasterizer, false, renderFlags);
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
  FlipYIfNeeded(scissorRect, opContext->renderTarget());
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
  FPArgs args = {getContext(), renderFlags, localBounds, state.matrix};
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
  if (surface && !surface->aboutToDraw(willDiscardContent)) {
    return;
  }
  opContext->addOp(std::move(op));
}

AAType RenderContext::getAAType(const FillStyle& style) const {
  if (opContext->renderTarget()->sampleCount() > 1) {
    return AAType::MSAA;
  }
  if (style.antiAlias) {
    return AAType::Coverage;
  }
  return AAType::None;
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
  auto renderTarget = opContext->renderTarget();
  auto rtRect = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  if (clipRect != rtRect) {
    return false;
  }
  auto deviceRect = viewMatrix.mapRect(localBounds);
  if (!deviceRect.contains(rtRect)) {
    return false;
  }
  return style.isOpaque();
}

void RenderContext::replaceRenderTarget(std::shared_ptr<RenderTargetProxy> newRenderTargetProxy) {
  delete opContext;
  opContext = new OpContext(std::move(newRenderTargetProxy), renderFlags);
}

}  // namespace tgfx
