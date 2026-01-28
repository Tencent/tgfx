/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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
#include "core/Atlas.h"
#include "core/AtlasManager.h"
#include "core/AtlasStrikeCache.h"
#include "core/GlyphRasterizer.h"
#include "core/GlyphRun.h"
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/ScalerContext.h"
#include "core/UserTypeface.h"
#include "core/images/SubsetImage.h"
#include "core/shapes/GlyphShape.h"
#include "core/utils/FauxBoldScale.h"
#include "core/utils/MathExtra.h"
#include "core/utils/StrokeUtils.h"
#include "gpu/DrawingManager.h"
#include "tgfx/core/Surface.h"

namespace tgfx {

static uint32_t GetTypefaceID(const Typeface* typeface, bool isCustom) {
  return isCustom ? static_cast<const UserTypeface*>(typeface)->builderID() : typeface->uniqueID();
}

static BytesKey GetStrikeKey(uint32_t typefaceID, float backingSize, bool isBold,
                             const Stroke* stroke) {
  BytesKey key;
  key.write(typefaceID);
  key.write(backingSize);
  if (!stroke) {
    key.write(static_cast<uint32_t>(isBold));
    return key;
  }
  key.write(stroke->width);
  key.write(stroke->miterLimit);
  uint32_t zipValue = 0;
  const auto cap = static_cast<uint32_t>(stroke->cap);
  const auto join = static_cast<uint32_t>(stroke->join);
  const auto bold = static_cast<uint32_t>(isBold);
  zipValue |= (cap & 0b11);          // cap: bit 0-1
  zipValue |= ((join & 0b11) << 2);  // join: bit 2-3
  zipValue |= ((bold & 0b1) << 4);   // bold: bit 4
  key.write(zipValue);
  return key;
}

static Font GetScaledFont(const Font& font, float scale) {
  if (FloatNearlyEqual(scale, 1.0f)) {
    return font;
  }
  return font.makeWithSize(font.getSize() * scale);
}

static std::unique_ptr<Stroke> GetScaledStroke(const Font& font, const Stroke* stroke,
                                               float scale) {
  if (font.hasColor() || stroke == nullptr) {
    return nullptr;
  }
  auto scaledStroke = std::make_unique<Stroke>(*stroke);
  scaledStroke->width *= scale;
  return scaledStroke;
}

static MaskFormat GetMaskFormat(const Font& font) {
  if (!font.hasColor()) {
    return MaskFormat::A8;
  }
#ifdef __APPLE__
  return MaskFormat::BGRA;
#else
  return MaskFormat::RGBA;
#endif
}

static float FindMaxGlyphDimension(const Font& font, const GlyphID* glyphs,
                                   const std::vector<size_t>& glyphIndices, const Stroke* stroke) {
  float maxDimension = 0.f;
  for (size_t i : glyphIndices) {
    auto bounds = font.getBounds(glyphs[i]);
    if (bounds.isEmpty()) {
      continue;
    }
    if (stroke != nullptr) {
      ApplyStrokeToBounds(*stroke, &bounds);
    }
    maxDimension = std::max(maxDimension, std::max(bounds.width(), bounds.height()));
  }
  return maxDimension;
}

static std::shared_ptr<ImageCodec> GetGlyphCodec(
    const Font& font, const std::shared_ptr<ScalerContext>& scalerContext, GlyphID glyphID,
    const Stroke* stroke, Point* glyphOffset, bool* isEmptyGlyph, bool* shouldRetry = nullptr) {
  if (glyphID == 0) {
    return nullptr;
  }

  auto hasFauxBold = !font.hasColor() && font.isFauxBold();
  auto bounds = scalerContext->getImageTransform(glyphID, hasFauxBold, stroke, nullptr);
  //bounds.isEmpty may be caused by unsupported stroke or bold operations.
  if (!bounds.isEmpty()) {
    if (std::max(bounds.width(), bounds.height()) > Atlas::MaxCellSize) {
      if (shouldRetry) {
        *shouldRetry = true;
      }
      return nullptr;
    }
    glyphOffset->x = bounds.left;
    glyphOffset->y = bounds.top;
    auto width = FloatCeilToInt(bounds.width());
    auto height = FloatCeilToInt(bounds.height());
    return std::make_shared<GlyphRasterizer>(width, height, scalerContext, glyphID, hasFauxBold,
                                             stroke, *glyphOffset);
  }

  std::shared_ptr<Shape> shape = nullptr;
  if (!font.isFauxItalic()) {
    shape = Shape::MakeFrom(font, glyphID);
  } else {
    auto noItalicFont = font;
    noItalicFont.setFauxItalic(false);
    shape = Shape::MakeFrom(std::move(noItalicFont), glyphID);
  }
  if (shape == nullptr) {
    return nullptr;
  }
  bounds = shape->getBounds();
  if (bounds.isEmpty()) {
    *isEmptyGlyph = true;
    return nullptr;
  }
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
    shape = Shape::ApplyStroke(std::move(shape), stroke);
  }
  if (std::max(bounds.width(), bounds.height()) > Atlas::MaxCellSize) {
    if (shouldRetry) {
      *shouldRetry = true;
    }
    return nullptr;
  }
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  glyphOffset->x = bounds.left;
  glyphOffset->y = bounds.top;
  auto width = FloatCeilToInt(bounds.width());
  auto height = FloatCeilToInt(bounds.height());
  return PathRasterizer::MakeFrom(width, height, std::move(shape), true,
#ifdef TGFX_USE_TEXT_GAMMA_CORRECTION
                                  true
#else
                                  false
#endif
  );
}

static void ComputeGlyphRenderMatrix(const Rect& atlasLocation, const Matrix& stateMatrix,
                                     const Matrix& positionMatrix, float scale,
                                     const Point& glyphOffset, bool fauxItalic,
                                     bool needsPixelAlignment, Matrix* outMatrix) {
  auto skewX = fauxItalic ? ITALIC_SKEW * scale : 0.0f;
  auto tx =
      scale * (glyphOffset.x - atlasLocation.x()) + skewX * (glyphOffset.y - atlasLocation.y());
  auto ty = scale * (glyphOffset.y - atlasLocation.y());
  // setAll Equivalent to :
  //   outMatrix->setTranslate(- atlasLocation.x(), - atlasLocation.y()); → [1, 0, -ax; 0, 1, -ay]
  //   outMatrix->postTranslate(glyphOffset.x, glyphOffset.y); → [1, 0, gx-ax; 0, 1, gy-ay]
  //   outMatrix->postScale(glyphRenderScale, glyphRenderScale); → [s, 0, s*(gx-ax); 0, s, s*(gy-ay)]
  //   if (fauxItalic) {
  //       outMatrix->postSkew(ITALIC_SKEW, 0); → [s, skewX, s*(gx-ax)+skewX*(gy-ay); 0, s, s*(gy-ay)]
  //   }
  //   outMatrix->postScale(inverseScale, inverseScale); inverseScale is folded into s above
  //
  // Here scale = glyphRenderScale * inverseScale, combining both into a single matrix construction.
  outMatrix->setAll(scale, skewX, tx, 0, scale, ty);
  outMatrix->postConcat(positionMatrix);
  outMatrix->postConcat(stateMatrix);
  if (needsPixelAlignment) {
    (*outMatrix)[2] = std::round((*outMatrix)[2]);
    (*outMatrix)[5] = std::round((*outMatrix)[5]);
  }
}

static void ApplyScaleAndStrokeToBounds(Rect* bounds, float scale, const Stroke* stroke) {
  if (stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, bounds, Matrix::MakeScale(scale));
  }
  bounds->scale(scale, scale);
}

static Rect GetTypefaceBounds(const std::shared_ptr<Typeface>& typeface, float fontSize,
                              float inverseScale, const Stroke* scaledStroke) {
  auto bounds = typeface->getBounds();
  bounds.scale(fontSize, fontSize);
  ApplyScaleAndStrokeToBounds(&bounds, inverseScale, scaledStroke);
  return bounds;
}

static const Rect* GetGlyphBounds(const Font& font, GlyphID glyphID, float inverseScale,
                                  const Stroke* scaledStroke, Rect* outBounds) {
  *outBounds = font.getBounds(glyphID);
  if (outBounds->isEmpty()) {
    return nullptr;
  }
  ApplyScaleAndStrokeToBounds(outBounds, inverseScale, scaledStroke);
  return outBounds;
}

static SamplingOptions GetSamplingOptions(float glyphRenderScale, bool fauxItalic,
                                          const Matrix& stateMatrix) {
  if (fauxItalic || !FloatNearlyEqual(glyphRenderScale, 1.0f)) {
    return SamplingOptions{FilterMode::Linear, MipmapMode::None};
  }
  const auto isUniformScale = stateMatrix.isScaleTranslate() &&
                              FloatNearlyEqual(stateMatrix.getScaleX(), stateMatrix.getScaleY());
  const auto filterMode = isUniformScale ? FilterMode::Nearest : FilterMode::Linear;
  return SamplingOptions{filterMode, MipmapMode::None};
}

RenderContext::RenderContext(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                             bool clearAll, Surface* surface,
                             std::shared_ptr<ColorSpace> colorSpace)
    : renderTarget(std::move(proxy)), renderFlags(renderFlags), surface(surface),
      _colorSpace(std::move(colorSpace)) {
  if (clearAll) {
    auto drawingManager = renderTarget->getContext()->drawingManager();
    opsCompositor = drawingManager->addOpsCompositor(renderTarget, renderFlags,
                                                     PMColor::Transparent(), _colorSpace);
  }
}

Rect RenderContext::getClipBounds(const Path& clip) {
  if (clip.isInverseFillType()) {
    return renderTarget->bounds();
  }
  auto bounds = clip.getBounds();
  if (!bounds.intersect(renderTarget->bounds())) {
    bounds.setEmpty();
  }
  return bounds;
}

void RenderContext::drawFill(const Brush& brush) {
  if (auto compositor = getOpsCompositor(brush.isOpaque())) {
    compositor->fillRect(renderTarget->bounds(), {}, brush, nullptr);
  }
}

void RenderContext::drawRect(const Rect& rect, const MCState& state, const Brush& brush,
                             const Stroke* stroke) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillRect(rect, state, brush, stroke);
  }
}

void RenderContext::drawRRect(const RRect& rRect, const MCState& state, const Brush& brush,
                              const Stroke* stroke) {
  if (auto compositor = getOpsCompositor()) {
    compositor->drawRRect(rRect, state, brush, stroke);
  }
}

void RenderContext::drawPath(const Path& path, const MCState& state, const Brush& brush) {
  // Temporarily use drawShape for rendering, and perform merging in the compositor later.
  drawShape(Shape::MakeFrom(path), state, brush, nullptr);
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

void RenderContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                              const MCState& state, const Brush& brush) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillImage(std::move(image), sampling, state, brush);
  }
}

void RenderContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const Brush& brush, const Stroke* stroke) {
  if (auto compositor = getOpsCompositor()) {
    shape = Shape::ApplyStroke(std::move(shape), stroke);
    compositor->drawShape(std::move(shape), state, brush);
  }
}

void RenderContext::drawImageRect(std::shared_ptr<Image> image, const Rect& srcRect,
                                  const Rect& dstRect, const SamplingOptions& sampling,
                                  const MCState& state, const Brush& brush,
                                  SrcRectConstraint constraint) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(image->isAlphaOnly() || brush.shader == nullptr);
  auto compositor = getOpsCompositor();
  if (compositor == nullptr) {
    return;
  }
  auto samplingOptions = sampling;
  if (constraint == SrcRectConstraint::Strict ||
      (samplingOptions.mipmapMode != MipmapMode::None && !state.matrix.hasNonIdentityScale())) {
    // Mipmaps perform sampling at different scales, which could cause samples to go outside the
    // strict region. So we disable mipmaps for strict constraints.

    // There is no scaling for the source image, so we can disable mipmaps to save memory.
    samplingOptions.mipmapMode = MipmapMode::None;
  }
  compositor->fillImageRect(std::move(image), srcRect, dstRect, samplingOptions, state, brush,
                            constraint);
}

void RenderContext::drawTextBlob(std::shared_ptr<TextBlob> textBlob, const MCState& state,
                                 const Brush& brush, const Stroke* stroke) {
  DEBUG_ASSERT(textBlob != nullptr);
  if (FloatNearlyZero(state.matrix.getMaxScale())) {
    return;
  }
  auto bounds = textBlob->getBounds();
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds, state.matrix);
  }
  state.matrix.mapRect(&bounds);  // To device space
  auto clipBounds = getClipBounds(state.clip);
  if (clipBounds.isEmpty() || !clipBounds.intersect(bounds)) {
    return;
  }
  auto localClipBounds = clipBounds;
  Matrix inverseMatrix = {};
  if (!state.matrix.invert(&inverseMatrix)) {
    return;
  }
  inverseMatrix.mapRect(&localClipBounds);

  for (auto run : *textBlob) {
    if (run.font().getTypeface() == nullptr) {
      continue;
    }
    // Glyphs with per-glyph rotation/scale (RSXform/Matrix) and outlines use path rendering
    // to avoid aliasing.
    if (HasComplexTransform(run) && run.font().hasOutlines()) {
      for (size_t i = 0; i < run.glyphCount(); i++) {
        drawGlyphAsPath(run.font(), run.glyphs()[i], ComputeGlyphMatrix(run, i), state, brush,
                        stroke, localClipBounds);
      }
      continue;
    }
    std::vector<size_t> rejectedIndices;
    drawGlyphsAsDirectMask(run, state, brush, stroke, localClipBounds, &rejectedIndices);
    // Process rejected glyphs immediately to maintain correct draw order.
    if (!rejectedIndices.empty()) {
      if (!run.font().hasColor() && run.font().hasOutlines()) {
        for (size_t i : rejectedIndices) {
          drawGlyphAsPath(run.font(), run.glyphs()[i], ComputeGlyphMatrix(run, i), state, brush,
                          stroke, localClipBounds);
        }
      } else {
        drawGlyphsAsTransformedMask(run, rejectedIndices, state, brush, stroke);
      }
    }
  }
}

void RenderContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, state);
}

void RenderContext::drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                              const MCState& state, const Brush& brush) {
  DEBUG_ASSERT(brush.shader == nullptr);
  Matrix viewMatrix = {};
  Rect bounds = {};
  if (picture->hasUnboundedFill()) {
    bounds = ToLocalBounds(getClipBounds(state.clip), state.matrix);
  } else {
    bounds = picture->getBounds();
  }
  if (bounds.isEmpty()) {
    return;
  }
  // Use roundOut() to snap bounds to integer pixel boundaries, ensuring both the texture size and
  // the viewMatrix offset are pixel-aligned, which prevents anti-aliasing artifacts at edges.
  bounds.roundOut();
  auto width = FloatSaturateToInt(bounds.width());
  auto height = FloatSaturateToInt(bounds.height());
  viewMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), width, height, &viewMatrix, colorSpace());
  if (image == nullptr) {
    return;
  }
  MCState drawState = state;
  if (filter) {
    Point offset = {};
    image = image->makeWithFilter(std::move(filter), &offset);
    if (image == nullptr) {
      return;
    }
    viewMatrix.preTranslate(-offset.x, -offset.y);
  }
  Matrix invertMatrix = {};
  if (!viewMatrix.invert(&invertMatrix)) {
    return;
  }
  drawState.matrix.preConcat(invertMatrix);
  drawImage(image, {}, drawState, brush.makeWithMatrix(viewMatrix));
}

bool RenderContext::flush() {
  if (opsCompositor != nullptr) {
    auto closed = opsCompositor->isClosed();
    opsCompositor->makeClosed();
    opsCompositor = nullptr;
    return !closed;
  }
  return false;
}

OpsCompositor* RenderContext::getOpsCompositor(bool discardContent) {
  if (surface && !surface->aboutToDraw(discardContent)) {
    return nullptr;
  }
  if (opsCompositor == nullptr || opsCompositor->isClosed()) {
    auto drawingManager = renderTarget->getContext()->drawingManager();
    opsCompositor =
        drawingManager->addOpsCompositor(renderTarget, renderFlags, std::nullopt, _colorSpace);
  } else if (discardContent) {
    opsCompositor->discardAll();
  }
  return opsCompositor.get();
}

void RenderContext::replaceRenderTarget(std::shared_ptr<RenderTargetProxy> newRenderTarget,
                                        std::shared_ptr<Image> oldContent) {
  renderTarget = std::move(newRenderTarget);
  if (oldContent != nullptr) {
    DEBUG_ASSERT(oldContent->width() == renderTarget->width() &&
                 oldContent->height() == renderTarget->height());
    auto drawingManager = renderTarget->getContext()->drawingManager();
    opsCompositor =
        drawingManager->addOpsCompositor(renderTarget, renderFlags, std::nullopt, _colorSpace);
    Brush brush = {{}, BlendMode::Src, false};
    opsCompositor->fillImageRect(std::move(oldContent), renderTarget->bounds(),
                                 renderTarget->bounds(), {}, MCState{}, brush,
                                 SrcRectConstraint::Fast);
  }
}

// rejectedIndices is guaranteed non-null by internal callers.
void RenderContext::drawGlyphsAsDirectMask(const GlyphRun& sourceGlyphRun, const MCState& state,
                                           const Brush& brush, const Stroke* stroke,
                                           const Rect& localClipBounds,
                                           std::vector<size_t>* rejectedIndices) {
  auto compositor = getOpsCompositor();
  if (compositor == nullptr) {
    return;
  }

  const auto maxScale = state.matrix.getMaxScale();
  const auto inverseScale = 1.0f / maxScale;

  auto font = GetScaledFont(sourceGlyphRun.font(), maxScale);
  const auto maskFormat = GetMaskFormat(font);
  auto typeface = font.getTypeface();
  auto scaledStroke = GetScaledStroke(font, stroke, maxScale);

  const auto typefaceID = GetTypefaceID(typeface.get(), typeface->isCustom());
  const auto backingSize = font.scalerContext->getBackingSize();
  const auto isBold = !font.hasColor() && font.isFauxBold();
  auto strikeKey = GetStrikeKey(typefaceID, backingSize, isBold, scaledStroke.get());

  AtlasCell atlasCell{maskFormat};
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  const auto nextFlushToken = atlasManager->nextFlushToken();
  auto strike = getContext()->atlasStrikeCache()->findOrCreateStrike(strikeKey);
  DEBUG_ASSERT(strike != nullptr);

  auto drawingManager = getContext()->drawingManager();
  const auto atlasBrush = brush.makeWithMatrix(state.matrix);
  const auto glyphRenderScale = font.scalerContext->getSize() / backingSize;
  const auto combinedScale = glyphRenderScale * inverseScale;
  const auto sampling = GetSamplingOptions(glyphRenderScale, font.isFauxItalic(), state.matrix);

  auto typefaceBounds =
      GetTypefaceBounds(typeface, font.getSize(), inverseScale, scaledStroke.get());
  const auto* sharedBounds = typefaceBounds.isEmpty() ? nullptr : &typefaceBounds;

  auto& textureProxies = atlasManager->getTextureProxies(maskFormat);

  Rect perGlyphBounds = {};
  Matrix positionMatrix = {};
  for (size_t i = 0; i < sourceGlyphRun.glyphCount(); i++) {
    auto glyphID = sourceGlyphRun.glyphs()[i];
    auto glyphBounds = sharedBounds ? sharedBounds
                                    : GetGlyphBounds(font, glyphID, inverseScale,
                                                     scaledStroke.get(), &perGlyphBounds);
    if (!glyphBounds) {
      continue;
    }

    auto mappedBounds = MapGlyphBounds(sourceGlyphRun, i, *glyphBounds);
    if (!Rect::Intersects(mappedBounds, localClipBounds)) {
      continue;
    }

    if (strike->isEmptyGlyph(glyphID)) {
      continue;
    }

    Point glyphOffset = {};
    auto atlasGlyph = strike->getGlyph(glyphID);
    DEBUG_ASSERT(atlasGlyph != nullptr);
    auto& atlasLocator = atlasGlyph->atlasLocator;
    if (atlasManager->hasGlyph(maskFormat, atlasGlyph)) {
      glyphOffset = atlasGlyph->offset;
    } else {
      bool shouldRetry = false;
      bool isEmptyGlyph = false;
      auto glyphCodec = GetGlyphCodec(font, font.scalerContext, glyphID, scaledStroke.get(),
                                      &glyphOffset, &isEmptyGlyph, &shouldRetry);
      if (glyphCodec == nullptr) {
        if (isEmptyGlyph) {
          strike->markEmptyGlyph(glyphID);
        } else {
          // shouldRetry means glyph is too large, record for path rendering.
          rejectedIndices->push_back(i);
        }
        continue;
      }

      atlasGlyph->offset = glyphOffset;
      atlasCell.width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell.height = static_cast<uint16_t>(glyphCodec->height());

      if (atlasManager->addCellToAtlas(atlasCell, nextFlushToken, &atlasLocator)) {
        auto& atlasRect = atlasLocator.getLocation();
        drawingManager->addAtlasCellTask(textureProxies[atlasLocator.pageIndex()],
                                         Point::Make(atlasRect.x(), atlasRect.y()),
                                         std::move(glyphCodec));
      } else {
        // Atlas full, record for path rendering.
        rejectedIndices->push_back(i);
        continue;
      }
    }

    atlasManager->setPlotUseToken(plotUseUpdater, atlasLocator.plotLocator(), maskFormat,
                                  nextFlushToken);
    auto textureProxy = textureProxies[atlasLocator.pageIndex()];
    if (textureProxy == nullptr) {
      // Texture proxy unavailable, record for path rendering.
      rejectedIndices->push_back(i);
      continue;
    }

    auto glyphState = state;
    auto& rect = atlasLocator.getLocation();
    sourceGlyphRun.positionMode() == GlyphPositionMode::Horizontal
        ? (void)ComputeGlyphMatrix(sourceGlyphRun, i, &positionMatrix)
        : (void)(positionMatrix = ComputeGlyphMatrix(sourceGlyphRun, i));
    ComputeGlyphRenderMatrix(rect, state.matrix, positionMatrix, combinedScale, glyphOffset,
                             font.isFauxItalic(), sampling.minFilterMode == FilterMode::Nearest,
                             &glyphState.matrix);
    compositor->fillTextAtlas(std::move(textureProxy), rect, sampling, glyphState, atlasBrush);
  }
}

void RenderContext::drawGlyphAsPath(const Font& font, GlyphID glyphID, const Matrix& glyphMatrix,
                                    const MCState& state, const Brush& brush, const Stroke* stroke,
                                    Rect& localClipBounds) {
  std::shared_ptr<Shape> shape = std::make_shared<GlyphShape>(font, glyphID);
  shape = Shape::ApplyMatrix(std::move(shape), glyphMatrix);
  shape = Shape::ApplyStroke(std::move(shape), stroke);

  Path clipPath = {};
  if (brush.antiAlias) {
    localClipBounds.outset(1.0f, 1.0f);
  }
  clipPath.addRect(localClipBounds);
  shape = Shape::Merge(std::move(shape), Shape::MakeFrom(std::move(clipPath)), PathOp::Intersect);
  if (auto compositor = getOpsCompositor()) {
    compositor->drawShape(std::move(shape), state, brush);
  }
}

void RenderContext::drawGlyphsAsTransformedMask(const GlyphRun& sourceGlyphRun,
                                                const std::vector<size_t>& glyphIndices,
                                                const MCState& state, const Brush& brush,
                                                const Stroke* stroke) {
  auto compositor = getOpsCompositor();
  if (compositor == nullptr) {
    return;
  }

  const auto maxScale = state.matrix.getMaxScale();
  auto font = GetScaledFont(sourceGlyphRun.font(), maxScale);
  auto scaledStroke = GetScaledStroke(font, stroke, maxScale);
  static constexpr float MaxAtlasDimension = Atlas::MaxCellSize - 2.f;
  auto cellScale = 1.f;
  auto maxDimension =
      FindMaxGlyphDimension(font, sourceGlyphRun.glyphs(), glyphIndices, scaledStroke.get());
  while (maxDimension > MaxAtlasDimension) {
    auto reductionFactor = MaxAtlasDimension / maxDimension;
    font = font.makeWithSize(font.getSize() * reductionFactor);
    if (scaledStroke) {
      scaledStroke->width *= reductionFactor;
    }
    maxDimension =
        FindMaxGlyphDimension(font, sourceGlyphRun.glyphs(), glyphIndices, scaledStroke.get());
    cellScale *= reductionFactor;
  }

  auto maskFormat = GetMaskFormat(font);
  auto typeface = font.getTypeface();
  const auto typefaceID = GetTypefaceID(typeface.get(), typeface->isCustom());
  const auto backingSize = font.scalerContext->getBackingSize();
  const auto isBold = !font.hasColor() && font.isFauxBold();
  auto strikeKey = GetStrikeKey(typefaceID, backingSize, isBold, scaledStroke.get());

  AtlasCell atlasCell{maskFormat};
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  auto nextFlushToken = atlasManager->nextFlushToken();
  auto drawingManager = getContext()->drawingManager();
  auto strike = getContext()->atlasStrikeCache()->findOrCreateStrike(strikeKey);
  auto& textureProxies = atlasManager->getTextureProxies(maskFormat);
  const auto atlasBrush = brush.makeWithMatrix(state.matrix);
  const auto glyphRenderScale = font.scalerContext->getSize() / backingSize;
  const auto combinedScale = glyphRenderScale / (maxScale * cellScale);

  Matrix positionMatrix = {};
  for (size_t i : glyphIndices) {
    auto glyphID = sourceGlyphRun.glyphs()[i];
    if (strike->isEmptyGlyph(glyphID)) {
      continue;
    }
    Point glyphOffset = {};
    auto atlasGlyph = strike->getGlyph(glyphID);
    DEBUG_ASSERT(atlasGlyph != nullptr);
    auto& atlasLocator = atlasGlyph->atlasLocator;

    if (atlasManager->hasGlyph(maskFormat, atlasGlyph)) {
      glyphOffset = atlasGlyph->offset;
    } else {
      bool isEmptyGlyph = false;
      auto glyphCodec = GetGlyphCodec(font, font.scalerContext, glyphID, scaledStroke.get(),
                                      &glyphOffset, &isEmptyGlyph);
      if (glyphCodec == nullptr) {
        if (isEmptyGlyph) {
          strike->markEmptyGlyph(glyphID);
        }
        continue;
      }

      atlasGlyph->offset = glyphOffset;
      atlasCell.width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell.height = static_cast<uint16_t>(glyphCodec->height());

      if (!atlasManager->addCellToAtlas(atlasCell, nextFlushToken, &atlasLocator)) {
        continue;
      }
      auto pageIndex = atlasLocator.pageIndex();
      const auto& atlasRect = atlasLocator.getLocation();
      drawingManager->addAtlasCellTask(textureProxies[pageIndex],
                                       Point::Make(atlasRect.x(), atlasRect.y()),
                                       std::move(glyphCodec));
    }
    atlasManager->setPlotUseToken(plotUseUpdater, atlasLocator.plotLocator(), maskFormat,
                                  nextFlushToken);
    auto textureProxy = textureProxies[atlasLocator.pageIndex()];
    if (textureProxy == nullptr) {
      continue;
    }

    auto glyphState = state;
    auto rect = atlasLocator.getLocation();
    sourceGlyphRun.positionMode() == GlyphPositionMode::Horizontal
        ? (void)ComputeGlyphMatrix(sourceGlyphRun, i, &positionMatrix)
        : (void)(positionMatrix = ComputeGlyphMatrix(sourceGlyphRun, i));
    ComputeGlyphRenderMatrix(rect, state.matrix, positionMatrix, combinedScale, glyphOffset,
                             font.isFauxItalic(), false, &glyphState.matrix);
    compositor->fillTextAtlas(std::move(textureProxy), rect,
                              SamplingOptions(FilterMode::Linear, MipmapMode::None), glyphState,
                              atlasBrush);
  }
}
}  // namespace tgfx
