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

#include "SurfaceDrawContext.h"
#include "core/PathRef.h"
#include "core/Rasterizer.h"
#include "core/SimpleTextBlob.h"
#include "gpu/OpContext.h"
#include "gpu/ProxyProvider.h"
#include "gpu/ops/ClearOp.h"
#include "gpu/ops/FillRectOp.h"
#include "gpu/ops/RRectOp.h"
#include "gpu/ops/TriangulatingPathOp.h"
#include "gpu/processors/AARectEffect.h"
#include "gpu/processors/TextureEffect.h"
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

static Path GetInitClip(RenderTargetProxy* renderTarget) {
  Path path = {};
  path.addRect(Rect::MakeWH(renderTarget->width(), renderTarget->height()));
  return path;
}

SurfaceDrawContext::SurfaceDrawContext(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                       uint32_t renderFlags)
    : DrawContext(GetInitClip(renderTargetProxy.get())), renderFlags(renderFlags) {
  opContext = new OpContext(std::move(renderTargetProxy));
}

SurfaceDrawContext::SurfaceDrawContext(Surface* surface)
    : DrawContext(GetInitClip(surface->renderTargetProxy.get())), surface(surface) {
  renderFlags = surface->surfaceOptions.renderFlags();
  opContext = new OpContext(surface->renderTargetProxy);
}

SurfaceDrawContext::~SurfaceDrawContext() {
  delete opContext;
}

Context* SurfaceDrawContext::getContext() const {
  return opContext->renderTarget()->getContext();
}

FPArgs SurfaceDrawContext::makeFPArgs(const Rect& localBounds, const Matrix& viewMatrix) {
  Matrix invert = {};
  if (!viewMatrix.invert(&invert)) {
    return {};
  }
  auto drawRect = localBounds;
  auto& clip = getClip();
  auto wideOpen = clip.isEmpty() && clip.isInverseFillType();
  if (!wideOpen) {
    auto clipBounds = clip.getBounds();
    invert.mapRect(&clipBounds);
    if (!drawRect.intersect(clipBounds)) {
      return {};
    }
  }
  return {getContext(), renderFlags, drawRect, viewMatrix};
}

void SurfaceDrawContext::clear() {
  FillStyle style = {};
  style.color = Color::Transparent();
  style.blendMode = BlendMode::Src;
  auto renderTarget = opContext->renderTarget();
  auto rect = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  drawRect(rect, style);
}

void SurfaceDrawContext::drawRect(const Rect& rect, const FillStyle& style) {
  auto& viewMatrix = getMatrix();
  if (drawAsClear(rect, viewMatrix, style)) {
    return;
  }
  auto args = makeFPArgs(rect, viewMatrix);
  if (args.empty()) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, args.drawRect, args.viewMatrix);
  addDrawOp(std::move(drawOp), args, style);
}

static bool HasColorOnly(const FillStyle& style) {
  return style.colorFilter == nullptr && style.shader == nullptr && style.maskFilter == nullptr;
}

bool SurfaceDrawContext::drawAsClear(const Rect& rect, const Matrix& viewMatrix,
                                     const FillStyle& style) {
  if (!HasColorOnly(style) || !viewMatrix.rectStaysRect()) {
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
  viewMatrix.mapRect(&bounds);
  auto [clipRect, useScissor] = getClipRect(&bounds);
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

void SurfaceDrawContext::drawRRect(const RRect& rRect, const FillStyle& style) {
  auto args = makeFPArgs(rRect.rect, getMatrix());
  if (args.empty()) {
    return;
  }
  auto drawOp = RRectOp::Make(style.color, rRect, args.viewMatrix);
  addDrawOp(std::move(drawOp), args, style);
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

void SurfaceDrawContext::drawPath(const Path& path, const FillStyle& style, const Stroke* stroke) {
  auto pathBounds = path.getBounds();
  if (stroke != nullptr) {
    pathBounds.outset(stroke->width, stroke->width);
  }
  auto args = makeFPArgs(pathBounds, getMatrix());
  if (args.empty()) {
    return;
  }
  std::unique_ptr<DrawOp> drawOp = nullptr;
  if (ShouldTriangulatePath(path, args.viewMatrix)) {
    drawOp =
        TriangulatingPathOp::Make(style.color, path, args.viewMatrix, stroke, args.renderFlags);
  } else {
    auto maskFP = makeTextureMask(path, args.viewMatrix, stroke);
    if (maskFP != nullptr) {
      drawOp = FillRectOp::Make(style.color, args.drawRect, args.viewMatrix);
      drawOp->addCoverageFP(std::move(maskFP));
    }
  }
  addDrawOp(std::move(drawOp), args, style);
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

std::unique_ptr<FragmentProcessor> SurfaceDrawContext::makeTextureMask(const Path& path,
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

void SurfaceDrawContext::drawImageRect(std::shared_ptr<Image> image,
                                       const SamplingOptions& sampling, const Rect& rect,
                                       const FillStyle& style) {
  if (image == nullptr) {
    return;
  }
  drawImageRect(std::move(image), sampling, rect, getMatrix(), style);
}

void SurfaceDrawContext::drawImageRect(std::shared_ptr<Image> image,
                                       const SamplingOptions& sampling, const Rect& rect,
                                       const Matrix& viewMatrix, const FillStyle& style) {
  auto args = makeFPArgs(rect, viewMatrix);
  if (args.empty()) {
    return;
  }
  auto isAlphaOnly = image->isAlphaOnly();
  auto processor = FragmentProcessor::Make(std::move(image), args, sampling);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, args.drawRect, args.viewMatrix);
  drawOp->addColorFP(std::move(processor));
  addDrawOp(std::move(drawOp), args, style, !isAlphaOnly);
}

void SurfaceDrawContext::drawGlyphRun(GlyphRun glyphRun, const FillStyle& style,
                                      const Stroke* stroke) {
  if (glyphRun.empty()) {
    return;
  }
  if (glyphRun.hasColor()) {
    drawColorGlyphs(glyphRun, style);
    return;
  }
  auto& viewMatrix = getMatrix();
  auto maxScale = viewMatrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return;
  }
  auto scaleMatrix = Matrix::MakeScale(maxScale);
  // Scale the glyphs before measuring to prevent precision loss with small font sizes.
  auto bounds = glyphRun.getBounds(scaleMatrix, stroke);
  auto localBounds = bounds;
  localBounds.scale(1.0f / maxScale, 1.0f / maxScale);
  auto args = makeFPArgs(localBounds, viewMatrix);
  if (args.empty()) {
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
  auto textureProxy = proxyProvider->createTextureProxy({}, rasterizer, false, args.renderFlags);
  auto processor = CreateMaskFP(std::move(textureProxy), &rasterizeMatrix);
  if (processor == nullptr) {
    return;
  }
  auto drawOp = FillRectOp::Make(style.color, args.drawRect, args.viewMatrix);
  drawOp->addCoverageFP(std::move(processor));
  addDrawOp(std::move(drawOp), args, style);
}

void SurfaceDrawContext::drawColorGlyphs(const GlyphRun& glyphRun, const FillStyle& style) {
  auto viewMatrix = getMatrix();
  auto scale = viewMatrix.getMaxScale();
  viewMatrix.preScale(1.0f / scale, 1.0f / scale);
  auto font = glyphRun.font();
  font = font.makeWithSize(font.getSize() * scale);
  auto glyphCount = glyphRun.runSize();
  auto& glyphIDs = glyphRun.glyphIDs();
  auto& positions = glyphRun.positions();
  for (size_t i = 0; i < glyphCount; ++i) {
    const auto& glyphID = glyphIDs[i];
    const auto& position = positions[i];
    auto glyphMatrix = Matrix::I();
    auto glyphImage = font.getImage(glyphID, &glyphMatrix);
    if (glyphImage == nullptr) {
      continue;
    }
    glyphMatrix.postTranslate(position.x * scale, position.y * scale);
    glyphMatrix.postConcat(viewMatrix);
    auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
    drawImageRect(std::move(glyphImage), {}, rect, glyphMatrix, style);
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

std::pair<std::optional<Rect>, bool> SurfaceDrawContext::getClipRect(const Rect* deviceBounds) {
  auto& clip = getClip();
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

std::shared_ptr<TextureProxy> SurfaceDrawContext::getClipTexture() {
  auto& clip = getClip();
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
    auto renderTarget = RenderTargetProxy::Make(getContext(), width, height, PixelFormat::ALPHA_8);
    if (renderTarget == nullptr) {
      renderTarget = RenderTargetProxy::Make(getContext(), width, height, PixelFormat::RGBA_8888);
      if (renderTarget == nullptr) {
        return nullptr;
      }
    }
    OpContext context(renderTarget);
    context.addOp(std::move(drawOp));
    clipTexture = renderTarget->getTextureProxy();
  } else {
    auto uniqueKey = PathRef::GetUniqueKey(clip);
    auto rasterizer = Rasterizer::MakeFrom(clip, ISize::Make(width, height), rasterizeMatrix);
    auto proxyProvider = getContext()->proxyProvider();
    clipTexture = proxyProvider->createTextureProxy({}, rasterizer, false, renderFlags);
  }
  clipID = domainID;
  return clipTexture;
}

std::unique_ptr<FragmentProcessor> SurfaceDrawContext::getClipMask(const Rect& deviceBounds,
                                                                   const Matrix& viewMatrix,
                                                                   Rect* scissorRect) {
  auto& clip = getClip();
  if (!clip.isEmpty() && clip.contains(deviceBounds)) {
    return nullptr;
  }
  auto [rect, useScissor] = getClipRect();
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
  auto texture = getClipTexture();
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

void SurfaceDrawContext::addDrawOp(std::unique_ptr<DrawOp> op, const FPArgs& args,
                                   const FillStyle& style, bool ignoreShader) {
  if (op == nullptr || args.empty()) {
    return;
  }
  auto isFillRectOp = op->classID() == FillRectOp::ClassID();
  auto aaType = AAType::None;
  if (opContext->renderTarget()->sampleCount() > 1) {
    aaType = AAType::MSAA;
  } else if (style.antiAlias) {
    if (!isFillRectOp || !args.viewMatrix.rectStaysRect() || !IsPixelAligned(op->bounds())) {
      aaType = AAType::Coverage;
    }
  }
  op->setAA(aaType);
  op->setBlendMode(style.blendMode);
  if (!ignoreShader) {
    auto shaderFP = FragmentProcessor::Make(style.shader, args);
    if (shaderFP) {
      op->addColorFP(std::move(shaderFP));
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
  auto clipMask = getClipMask(op->bounds(), args.viewMatrix, &scissorRect);
  if (clipMask) {
    op->addCoverageFP(std::move(clipMask));
  }
  op->setScissorRect(scissorRect);
  addOp(std::move(op), [&] { return wouldOverwriteEntireRT(args, style, isFillRectOp); });
}

void SurfaceDrawContext::addOp(std::unique_ptr<Op> op,
                               const std::function<bool()>& willDiscardContent) {
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

bool SurfaceDrawContext::wouldOverwriteEntireRT(const FPArgs& args, const FillStyle& style,
                                                bool isRectOp) const {
  if (!isRectOp) {
    return false;
  }
  auto& clip = getClip();
  auto& viewMatrix = args.viewMatrix;
  auto clipRect = Rect::MakeEmpty();
  if (!clip.isRect(&clipRect) || !viewMatrix.rectStaysRect()) {
    return false;
  }
  auto renderTarget = opContext->renderTarget();
  auto rtRect = Rect::MakeWH(renderTarget->width(), renderTarget->height());
  if (clipRect != rtRect) {
    return false;
  }
  auto deviceRect = viewMatrix.mapRect(args.drawRect);
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

void SurfaceDrawContext::replaceRenderTarget(
    std::shared_ptr<RenderTargetProxy> newRenderTargetProxy) {
  delete opContext;
  opContext = new OpContext(std::move(newRenderTargetProxy));
}

}  // namespace tgfx
