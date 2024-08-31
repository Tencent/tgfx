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
#include "core/Rasterizer.h"
#include "core/SimpleTextBlob.h"
#include "gpu/DrawingManager.h"
#include "gpu/OpContext.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/ClearOp.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/ops/RRectOp.h"
#include "gpu/ops/TriangulatingPathOp.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/TextureEffect.h"
#include "images/TextureImage.h"
#include "utils/StrokeKey.h"

namespace tgfx {
// https://chromium-review.googlesource.com/c/chromium/src/+/1099564/
static constexpr int AA_TESSELLATOR_MAX_VERB_COUNT = 100;
// A factor used to estimate the memory size of a tessellated path, based on the average value of
// Buffer.size() / Path.countPoints() from 4300+ tessellated path data.
static constexpr int AA_TESSELLATOR_BUFFER_SIZE_FACTOR = 170;
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
  opContext = new OpContext(std::move(renderTargetProxy));
}

RenderContext::RenderContext(Surface* surface) : surface(surface) {
  renderFlags = surface->surfaceOptions.renderFlags();
  opContext = new OpContext(surface->renderTargetProxy);
}

RenderContext::~RenderContext() {
  delete opContext;
}

Context* RenderContext::getContext() const {
  return opContext->renderTarget()->getContext();
}

Rect RenderContext::clipLocalBounds(const Rect& localBounds, const MCState& state) {
  Matrix invert = {};
  if (!state.matrix.invert(&invert)) {
    return {};
  }
  auto drawRect = localBounds;
  auto& clip = state.clip;
  auto wideOpen = clip.isEmpty() && clip.isInverseFillType();
  if (!wideOpen) {
    auto clipBounds = clip.getBounds();
    invert.mapRect(&clipBounds);
    if (!drawRect.intersect(clipBounds)) {
      return {};
    }
  }
  return drawRect;
}

void RenderContext::clear() {
  FillStyle style = {};
  style.color = Color::Transparent();
  style.blendMode = BlendMode::Src;
  auto renderTarget = opContext->renderTarget();
  auto rect = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  MCState state = {};
  drawAsClear(rect, state, style);
}

void RenderContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  if (drawAsClear(rect, state, style)) {
    return;
  }
  auto localBounds = clipLocalBounds(rect, state);
  if (localBounds.isEmpty()) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, localBounds, state.matrix);
  addDrawOp(std::move(drawOp), localBounds, state, style);
}

static bool HasColorOnly(const FillStyle& style) {
  return style.colorFilter == nullptr && style.shader == nullptr && style.maskFilter == nullptr;
}

bool RenderContext::drawAsClear(const Rect& rect, const MCState& state, const FillStyle& style) {
  if (!HasColorOnly(style) || !state.matrix.rectStaysRect()) {
    return false;
  }
  auto color = style.color;
  if (style.blendMode == BlendMode::Clear) {
    color = Color::Transparent();
  } else if (style.blendMode != BlendMode::Src) {
    if (!color.isOpaque()) {
      return false;
    }
  }
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
    } else if (clipRect->isEmpty()) {
      addOp(ClearOp::Make(color, bounds), [] { return true; });
      return true;
    }
  }
  return false;
}

void RenderContext::drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) {
  auto localBounds = clipLocalBounds(rRect.rect, state);
  if (localBounds.isEmpty()) {
    return;
  }
  auto drawOp = RRectOp::Make(style.color, rRect, state.matrix);
  addDrawOp(std::move(drawOp), localBounds, state, style);
}

static bool ShouldTriangulatePath(const Path& path, const Matrix& viewMatrix) {
  if (path.countVerbs() <= AA_TESSELLATOR_MAX_VERB_COUNT) {
    return true;
  }
  auto scales = viewMatrix.getAxisScales();
  auto bounds = path.getBounds();
  bounds.scale(scales.x, scales.y);
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  return path.countPoints() * AA_TESSELLATOR_BUFFER_SIZE_FACTOR <= width * height;
}

void RenderContext::drawPath(const Path& path, const MCState& state, const FillStyle& style,
                             const Stroke* stroke) {
  auto pathBounds = path.getBounds();
  if (stroke != nullptr) {
    pathBounds.outset(stroke->width, stroke->width);
  }
  auto localBounds = clipLocalBounds(pathBounds, state);
  if (localBounds.isEmpty()) {
    return;
  }
  std::unique_ptr<DrawOp> drawOp = nullptr;
  if (ShouldTriangulatePath(path, state.matrix)) {
    drawOp = TriangulatingPathOp::Make(style.color, path, state.matrix, stroke, renderFlags);
  } else {
    auto maskFP = makeTextureMask(path, state.matrix, stroke);
    if (maskFP != nullptr) {
      drawOp = FillRectOp::Make(style.color, localBounds, state.matrix);
      drawOp->addCoverageFP(std::move(maskFP));
    }
  }
  addDrawOp(std::move(drawOp), localBounds, state, style);
}

static std::unique_ptr<FragmentProcessor> CreateMaskFP(std::shared_ptr<TextureProxy> textureProxy,
                                                       const Matrix* localMatrix = nullptr) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto isAlphaOnly = textureProxy->isAlphaOnly();
  auto processor = TextureEffect::Make(std::move(textureProxy), {}, localMatrix);
  if (processor == nullptr) {
    return nullptr;
  }
  if (!isAlphaOnly) {
    processor = FragmentProcessor::MulInputByChildAlpha(std::move(processor));
  }
  return processor;
}

std::unique_ptr<FragmentProcessor> RenderContext::makeTextureMask(const Path& path,
                                                                  const Matrix& viewMatrix,
                                                                  const Stroke* stroke) {
  auto scales = viewMatrix.getAxisScales();
  auto bounds = path.getBounds();
  bounds.scale(scales.x, scales.y);
  static const auto TexturePathType = UniqueID::Next();
  BytesKey bytesKey(3 + (stroke ? StrokeKeyCount : 0));
  bytesKey.write(TexturePathType);
  bytesKey.write(scales.x);
  bytesKey.write(scales.y);
  if (stroke) {
    WriteStrokeKey(&bytesKey, stroke);
  }
  auto uniqueKey = UniqueKey::Combine(PathRef::GetUniqueKey(path), bytesKey);
  auto width = ceilf(bounds.width());
  auto height = ceilf(bounds.height());
  auto rasterizeMatrix = Matrix::MakeScale(scales.x, scales.y);
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto rasterizer = Rasterizer::MakeFrom(path, ISize::Make(width, height), rasterizeMatrix, stroke);
  auto proxyProvider = getContext()->proxyProvider();
  auto textureProxy = proxyProvider->createTextureProxy(uniqueKey, rasterizer, false, renderFlags);
  return CreateMaskFP(std::move(textureProxy), &rasterizeMatrix);
}

void RenderContext::drawImageRect(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                                  const Rect& rect, const MCState& state, const FillStyle& style) {
  if (image == nullptr) {
    return;
  }
  auto localBounds = clipLocalBounds(rect, state);
  if (localBounds.isEmpty()) {
    return;
  }
  auto isAlphaOnly = image->isAlphaOnly();
  FPArgs args = {getContext(), renderFlags, localBounds, state.matrix};
  auto processor = FragmentProcessor::Make(std::move(image), args, sampling);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, localBounds, state.matrix);
  drawOp->addColorFP(std::move(processor));
  if (!isAlphaOnly && style.shader) {
    auto fillStyle = style;
    fillStyle.shader = nullptr;
    addDrawOp(std::move(drawOp), localBounds, state, fillStyle);
  } else {
    addDrawOp(std::move(drawOp), localBounds, state, style);
  }
}

void RenderContext::drawGlyphRun(GlyphRun glyphRun, const MCState& state, const FillStyle& style,
                                 const Stroke* stroke) {
  if (glyphRun.empty()) {
    return;
  }
  if (glyphRun.hasColor()) {
    drawColorGlyphs(glyphRun, state, style);
    return;
  }
  auto maxScale = state.matrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return;
  }
  auto scaleMatrix = Matrix::MakeScale(maxScale);
  // Scale the glyphs before measuring to prevent precision loss with small font sizes.
  auto bounds = glyphRun.getBounds(scaleMatrix, stroke);
  auto localBounds = bounds;
  localBounds.scale(1.0f / maxScale, 1.0f / maxScale);
  localBounds = clipLocalBounds(localBounds, state);
  if (localBounds.isEmpty()) {
    return;
  }
  auto rasterizeMatrix = scaleMatrix;
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto textBlob = std::make_shared<SimpleTextBlob>(std::move(glyphRun));
  auto rasterizer = Rasterizer::MakeFrom(std::move(textBlob), ISize::Make(width, height),
                                         rasterizeMatrix, stroke);
  auto proxyProvider = getContext()->proxyProvider();
  auto textureProxy = proxyProvider->createTextureProxy({}, rasterizer, false, renderFlags);
  auto processor = CreateMaskFP(std::move(textureProxy), &rasterizeMatrix);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, localBounds, state.matrix);
  drawOp->addCoverageFP(std::move(processor));
  addDrawOp(std::move(drawOp), localBounds, state, style);
}

void RenderContext::drawLayer(std::shared_ptr<Picture> picture, const MCState& state,
                              const FillStyle& style, std::shared_ptr<ImageFilter> filter) {
  auto bounds = picture->getBounds(state.matrix);
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto renderTarget = opContext->renderTarget()->makeRenderTargetProxy(width, height);
  if (renderTarget == nullptr) {
    return;
  }
  RenderContext renderContext(renderTarget, renderFlags);
  auto viewMatrix = state.matrix;
  viewMatrix.postTranslate(-bounds.x(), -bounds.y());
  MCState replayState(viewMatrix);
  picture->playback(&renderContext, replayState);
  if (renderTarget->sampleCount() > 1) {
    auto drawingManager = getContext()->drawingManager();
    drawingManager->addTextureResolveTask(renderTarget);
  }
  auto image = TextureImage::Wrap(renderTarget->getTextureProxy());
  if (image == nullptr) {
    return;
  }
  MCState drawState = state;
  drawState.matrix = Matrix::MakeTrans(bounds.x(), bounds.y());
  if (filter) {
    auto offset = Point::Zero();
    image = image->makeWithFilter(std::move(filter), &offset);
    if (image == nullptr) {
      return;
    }
    drawState.matrix.preTranslate(offset.x, offset.y);
  }
  drawImageRect(std::move(image), {}, bounds, drawState, style);
}

void RenderContext::drawColorGlyphs(const GlyphRun& glyphRun, const MCState& state,
                                    const FillStyle& style) {
  auto viewMatrix = state.matrix;
  auto scale = viewMatrix.getMaxScale();
  viewMatrix.preScale(1.0f / scale, 1.0f / scale);
  auto font = glyphRun.font();
  font = font.makeWithSize(font.getSize() * scale);
  auto glyphCount = glyphRun.runSize();
  auto& glyphIDs = glyphRun.glyphIDs();
  auto& positions = glyphRun.positions();
  auto glyphState = state;
  for (size_t i = 0; i < glyphCount; ++i) {
    const auto& glyphID = glyphIDs[i];
    const auto& position = positions[i];
    auto glyphImage = font.getImage(glyphID, &glyphState.matrix);
    if (glyphImage == nullptr) {
      continue;
    }
    glyphState.matrix.postTranslate(position.x * scale, position.y * scale);
    glyphState.matrix.postConcat(viewMatrix);
    auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
    drawImageRect(std::move(glyphImage), {}, rect, glyphState, style);
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

std::shared_ptr<TextureProxy> RenderContext::getClipTexture(const Path& clip) {
  auto domainID = PathRef::GetUniqueKey(clip).domainID();
  if (domainID == clipID) {
    return clipTexture;
  }
  auto bounds = clip.getBounds();
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto rasterizeMatrix = Matrix::MakeTrans(-bounds.left, -bounds.top);
  if (ShouldTriangulatePath(clip, rasterizeMatrix)) {
    auto drawOp =
        TriangulatingPathOp::Make(Color::White(), clip, rasterizeMatrix, nullptr, renderFlags);
    drawOp->setAA(AAType::Coverage);
    auto renderTarget = RenderTargetProxy::Make(getContext(), width, height, PixelFormat::ALPHA_8);
    if (renderTarget == nullptr) {
      renderTarget = RenderTargetProxy::Make(getContext(), width, height, PixelFormat::RGBA_8888);
      if (renderTarget == nullptr) {
        return nullptr;
      }
    }
    OpContext context(renderTarget);
    // Since the clip may not coverage the entire render target, we need to clear the render target
    // to transparent. Otherwise, the associated texture will have undefined pixels outside the clip.
    context.addOp(ClearOp::Make(Color::Transparent(), Rect::MakeWH(width, height)));
    context.addOp(std::move(drawOp));
    clipTexture = renderTarget->getTextureProxy();
  } else {
    auto rasterizer = Rasterizer::MakeFrom(clip, ISize::Make(width, height), rasterizeMatrix);
    auto proxyProvider = getContext()->proxyProvider();
    clipTexture = proxyProvider->createTextureProxy({}, rasterizer, false, renderFlags);
  }
  clipID = domainID;
  return clipTexture;
}

std::unique_ptr<FragmentProcessor> RenderContext::getClipMask(const Path& clip,
                                                              const Rect& deviceBounds,
                                                              const Matrix& viewMatrix,
                                                              Rect* scissorRect) {
  if (!clip.isEmpty() && clip.contains(deviceBounds)) {
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
  auto clipBounds = clip.getBounds();
  *scissorRect = clipBounds;
  FlipYIfNeeded(scissorRect, opContext->renderTarget());
  scissorRect->roundOut();
  auto texture = getClipTexture(clip);
  if (texture == nullptr) {
    return nullptr;
  }
  auto localMatrix = viewMatrix;
  localMatrix.postTranslate(-clipBounds.left, -clipBounds.top);
  auto maskEffect = TextureEffect::Make(texture, {}, &localMatrix);
  if (!texture->isAlphaOnly()) {
    maskEffect = FragmentProcessor::MulInputByChildAlpha(std::move(maskEffect));
  }
  return maskEffect;
}

void RenderContext::addDrawOp(std::unique_ptr<DrawOp> op, const Rect& localBounds,
                              const MCState& state, const FillStyle& style) {
  if (op == nullptr) {
    return;
  }
  FPArgs args = {getContext(), renderFlags, localBounds, state.matrix};
  auto isRectOp = op->classID() == FillRectOp::ClassID();
  auto aaType = AAType::None;
  if (opContext->renderTarget()->sampleCount() > 1) {
    aaType = AAType::MSAA;
  } else if (style.antiAlias) {
    if (!isRectOp || !args.viewMatrix.rectStaysRect() || !IsPixelAligned(op->bounds())) {
      aaType = AAType::Coverage;
    }
  }
  op->setAA(aaType);
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
  auto clipMask = getClipMask(state.clip, op->bounds(), args.viewMatrix, &scissorRect);
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

enum class SrcColorOpacity {
  Unknown,
  // The src color is known to be opaque (alpha == 255)
  Opaque,
  // The src color is known to be fully transparent (color == 0)
  TransparentBlack,
  // The src alpha is known to be fully transparent (alpha == 0)
  TransparentAlpha,
};

static bool BlendModeIsOpaque(BlendMode mode, SrcColorOpacity opacityType) {
  BlendInfo blendInfo = {};
  if (!BlendModeAsCoeff(mode, &blendInfo)) {
    return false;
  }
  switch (blendInfo.srcBlend) {
    case BlendModeCoeff::DA:
    case BlendModeCoeff::DC:
    case BlendModeCoeff::IDA:
    case BlendModeCoeff::IDC:
      return false;
    default:
      break;
  }
  switch (blendInfo.dstBlend) {
    case BlendModeCoeff::Zero:
      return true;
    case BlendModeCoeff::ISA:
      return opacityType == SrcColorOpacity::Opaque;
    case BlendModeCoeff::SA:
      return opacityType == SrcColorOpacity::TransparentBlack ||
             opacityType == SrcColorOpacity::TransparentAlpha;
    case BlendModeCoeff::SC:
      return opacityType == SrcColorOpacity::TransparentBlack;
    default:
      return false;
  }
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
  if (style.maskFilter) {
    return false;
  }
  if (style.colorFilter && style.colorFilter->isAlphaUnchanged()) {
    return false;
  }
  auto opacityType = SrcColorOpacity::Unknown;
  auto alpha = style.color.alpha;
  if (alpha == 1.0f && (!style.shader || style.shader->isOpaque())) {
    opacityType = SrcColorOpacity::Opaque;
  } else if (alpha == 0) {
    if (style.shader) {
      opacityType = SrcColorOpacity::TransparentAlpha;
    } else {
      opacityType = SrcColorOpacity::TransparentBlack;
    }
  }
  return BlendModeIsOpaque(style.blendMode, opacityType);
}

void RenderContext::replaceRenderTarget(std::shared_ptr<RenderTargetProxy> newRenderTargetProxy) {
  delete opContext;
  opContext = new OpContext(std::move(newRenderTargetProxy));
}

}  // namespace tgfx
