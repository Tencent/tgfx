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
#include "gpu/DrawingManager.h"
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

SurfaceDrawContext::SurfaceDrawContext(std::shared_ptr<RenderTargetProxy> renderTargetProxy,
                                       uint32_t renderFlags)
    : renderTargetProxy(std::move(renderTargetProxy)), renderFlags(renderFlags) {
}

Context* SurfaceDrawContext::getContext() const {
  return renderTargetProxy->getContext();
}

DrawArgs SurfaceDrawContext::makeDrawArgs(const Rect& localBounds, const Matrix& viewMatrix) {
  Matrix invert = {};
  if (!viewMatrix.invert(&invert)) {
    return {};
  }
  auto drawRect = localBounds;
  auto clipBounds = getClip().getBounds();
  invert.mapRect(&clipBounds);
  if (!drawRect.intersect(clipBounds)) {
    return {};
  }
  return {getContext(), renderFlags, drawRect, viewMatrix};
}

void SurfaceDrawContext::drawRect(const Rect& rect, const FillStyle& style) {
  drawRect(rect, getMatrix(), style);
}

void SurfaceDrawContext::drawRect(const Rect& rect, const Matrix& viewMatrix,
                                  const FillStyle& style) {
  if (drawAsClear(rect, viewMatrix, style)) {
    return;
  }
  auto args = makeDrawArgs(rect, viewMatrix);
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
    auto format = renderTargetProxy->format();
    const auto& writeSwizzle = getContext()->caps()->getWriteSwizzle(format);
    color = writeSwizzle.applyTo(color);
    if (useScissor) {
      addOp(ClearOp::Make(color, *clipRect));
      return true;
    } else if (clipRect->isEmpty()) {
      addOp(ClearOp::Make(color, bounds));
      return true;
    }
  }
  return false;
}

void SurfaceDrawContext::drawRRect(const RRect& rRect, const FillStyle& style) {
  auto args = makeDrawArgs(rRect.rect, getMatrix());
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
  auto args = makeDrawArgs(pathBounds, getMatrix());
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
  // Scale the glyphs before measuring to prevent precision loss with small font sizes.
  auto bounds = glyphRun.getBounds(maxScale, stroke);
  auto localBounds = bounds;
  localBounds.scale(1.0f / maxScale, 1.0f / maxScale);
  auto args = makeDrawArgs(localBounds, viewMatrix);
  if (args.empty()) {
    return;
  }
  auto rasterizeMatrix = Matrix::MakeScale(maxScale, maxScale);
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
  auto drawOp = FillRectOp::Make(style.color, args.drawRect, viewMatrix);
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
    auto glyphStyle = style;
    glyphStyle.shader = Shader::MakeImageShader(std::move(glyphImage));
    drawRect(rect, glyphMatrix, glyphStyle);
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

static void FlipYIfNeeded(Rect* rect, const RenderTargetProxy* rt) {
  if (rt->origin() == ImageOrigin::BottomLeft) {
    auto height = rect->height();
    rect->top = static_cast<float>(rt->height()) - rect->bottom;
    rect->bottom = rect->top + height;
  }
}

std::pair<std::optional<Rect>, bool> SurfaceDrawContext::getClipRect(const Rect* deviceBounds) {
  auto& clip = getClip();
  auto rect = Rect::MakeEmpty();
  if (clip.asRect(&rect)) {
    if (deviceBounds != nullptr && !rect.intersect(*deviceBounds)) {
      return {{}, false};
    }
    FlipYIfNeeded(&rect, renderTargetProxy.get());
    if (IsPixelAligned(rect)) {
      rect.round();
      if (rect != Rect::MakeWH(renderTargetProxy->width(), renderTargetProxy->height())) {
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
    auto drawingManager = getContext()->drawingManager();
    auto renderTask = drawingManager->addOpsTask(renderTarget);
    renderTask->addOp(std::move(drawOp));
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
  FlipYIfNeeded(scissorRect, renderTargetProxy.get());
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

void SurfaceDrawContext::addDrawOp(std::unique_ptr<DrawOp> op, const DrawArgs& args,
                                   const FillStyle& style) {
  if (op == nullptr || args.empty()) {
    return;
  }
  auto aaType = AAType::None;
  if (renderTargetProxy->sampleCount() > 1) {
    aaType = AAType::MSAA;
  } else if (style.antiAlias) {
    auto isFillRect = op->classID() == FillRectOp::ClassID();
    if (!isFillRect || !args.viewMatrix.rectStaysRect() || !IsPixelAligned(op->bounds())) {
      aaType = AAType::Coverage;
    }
  }
  op->setAA(aaType);
  op->setBlendMode(style.blendMode);
  auto shaderFP = FragmentProcessor::Make(style.shader, args);
  if (shaderFP != nullptr) {
    op->addColorFP(std::move(shaderFP));
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
  addOp(std::move(op));
}

void SurfaceDrawContext::fillWithFP(std::unique_ptr<FragmentProcessor> fp,
                                    const Matrix& localMatrix, bool autoResolve) {
  fillRectWithFP(Rect::MakeWH(renderTargetProxy->width(), renderTargetProxy->height()),
                 std::move(fp), localMatrix);
  if (autoResolve) {
    auto drawingManager = renderTargetProxy->getContext()->drawingManager();
    drawingManager->addTextureResolveTask(renderTargetProxy);
  }
}

void SurfaceDrawContext::fillRectWithFP(const Rect& dstRect, std::unique_ptr<FragmentProcessor> fp,
                                        const Matrix& localMatrix) {
  if (fp == nullptr) {
    return;
  }
  auto op = FillRectOp::Make(std::nullopt, dstRect, Matrix::I(), &localMatrix);
  op->addColorFP(std::move(fp));
  op->setBlendMode(BlendMode::Src);
  addOp(std::move(op));
}

void SurfaceDrawContext::addOp(std::unique_ptr<Op> op) {
  if (opsTask == nullptr || opsTask->isClosed()) {
    auto drawingManager = renderTargetProxy->getContext()->drawingManager();
    opsTask = drawingManager->addOpsTask(renderTargetProxy);
  }
  opsTask->addOp(std::move(op));
}

void SurfaceDrawContext::replaceRenderTarget(
    std::shared_ptr<RenderTargetProxy> newRenderTargetProxy) {
  renderTargetProxy = std::move(newRenderTargetProxy);
  opsTask = nullptr;
}

}  // namespace tgfx
