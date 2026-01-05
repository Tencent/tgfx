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
#include <tgfx/core/Surface.h>
#include "core/Atlas.h"
#include "core/AtlasManager.h"
#include "core/AtlasStrikeCache.h"
#include "core/GlyphRasterizer.h"
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/ScalerContext.h"
#include "core/UserTypeface.h"
#include "core/images/SubsetImage.h"
#include "core/shapes/TextShape.h"
#include "core/utils/FauxBoldScale.h"
#include "core/utils/MathExtra.h"
#include "core/utils/StrokeUtils.h"
#include "gpu/DrawingManager.h"

namespace tgfx {
static uint32_t GetTypefaceID(const Typeface* typeface, bool isCustom) {
  return isCustom ? static_cast<const UserTypeface*>(typeface)->builderID() : typeface->uniqueID();
}

static void CreateStrikeKey(uint32_t typefaceID, float backingSize, bool isBold,
                            const Stroke* stroke, BytesKey* key) {
  key->write(typefaceID);
  key->write(backingSize);
  if (!stroke) {
    return;
  }
  key->write(stroke->width);
  key->write(stroke->miterLimit);
  uint32_t zipValue = 0;
  const auto cap = static_cast<uint32_t>(stroke->cap);
  const auto join = static_cast<uint32_t>(stroke->join);
  const auto bold = static_cast<uint32_t>(isBold);
  zipValue |= (cap & 0b11);          // cap: bit 0-1
  zipValue |= ((join & 0b11) << 2);  // join: bit 2-3
  zipValue |= ((bold & 0b1) << 4);   // bold: bit 4
  key->write(zipValue);
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

static float FindMaxGlyphDimension(const Font& font, const std::vector<GlyphID>& glyphIDs,
                                   const Stroke* stroke) {
  float maxDimension = 0.f;
  for (auto& glyphID : glyphIDs) {
    auto bounds = font.getBounds(glyphID);
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
    const Stroke* stroke, Point* glyphOffset, bool* shouldRetry = nullptr) {
  auto setRetryValue = [shouldRetry](bool value) {
  if (shouldRetry) {
      *shouldRetry = value;
    }
  };

  setRetryValue(false);
  if (glyphID == 0) {
    return nullptr;
  }

  auto hasFauxBold = !font.hasColor() && font.isFauxBold();
  auto bounds = scalerContext->getImageTransform(glyphID, hasFauxBold, stroke, nullptr);
  //bounds.isEmpty may be caused by unsupported stroke or bold operations.
  if (!bounds.isEmpty()) {
    if (std::max(bounds.width(), bounds.height()) > Atlas::MaxCellSize) {
      setRetryValue(true);
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
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
    shape = Shape::ApplyStroke(std::move(shape), stroke);
  }
  bounds = shape->getBounds();
  if (bounds.isEmpty()) {
    return nullptr;
  }
  if (std::max(bounds.width(), bounds.height()) > Atlas::MaxCellSize) {
    setRetryValue(true);
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

static void ComputeGlyphFinalMatrix(const Rect& atlasLocation, const Matrix& stateMatrix,
                                    float scale, const Point& position, Matrix* glyphMatrix,
                                    bool needsPixelAlignment) {
  glyphMatrix->postScale(scale, scale);
  glyphMatrix->postTranslate(position.x, position.y);
  glyphMatrix->postConcat(stateMatrix);
  glyphMatrix->preTranslate(-atlasLocation.x(), -atlasLocation.y());
  if (needsPixelAlignment) {
    // Pixel alignment for nearest-neighbor sampling to prevent texture artifacts like pixel truncation
    (*glyphMatrix)[2] = std::round((*glyphMatrix)[2]);
    (*glyphMatrix)[5] = std::round((*glyphMatrix)[5]);
  }
}

static Rect GetGlyphBounds(const Font& font, bool isCustom, GlyphID glyphID) {
  if (isCustom) {
    return font.getBounds(glyphID);
  }
  auto bounds = font.getTypeface()->getBounds();
  if (!bounds.isEmpty()) {
    bounds.scale(font.getSize(), font.getSize());
    return bounds;
  }
  return font.getBounds(glyphID);
}

static bool IsGlyphVisible(const Font& font, bool isCustom, GlyphID glyphID, const Rect& clipBounds,
                           const Stroke* stroke, float scale, const Point& glyphPosition,
                           int* maxDimension) {
  auto bounds = GetGlyphBounds(font, isCustom, glyphID);
  if (bounds.isEmpty()) {
    return false;
  }
  if (stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, &bounds, Matrix::MakeScale(scale));
  }
  if ((isCustom || font.hasColor()) && maxDimension) {
    *maxDimension = FloatCeilToInt(std::max(bounds.width(), bounds.height()));
  }
  bounds.scale(scale, scale);
  bounds.offset(glyphPosition.x, glyphPosition.y);

  return Rect::Intersects(bounds, clipBounds);
}

static void GetGlyphMatrix(float glyphRenderScale, const Point& glyphOffset, bool fauxItalic,
                           Matrix* glyphMatrix) {
  glyphMatrix->setTranslate(glyphOffset.x, glyphOffset.y);
  glyphMatrix->postScale(glyphRenderScale, glyphRenderScale);
  if (fauxItalic) {
    glyphMatrix->postSkew(ITALIC_SKEW, 0);
  }
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

void RenderContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const MCState& state, const Brush& brush,
                                     const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  if (FloatNearlyZero(state.matrix.getMaxScale())) {
    return;
  }
  auto bounds = glyphRunList->getBounds();
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

  std::vector<GlyphRun> rejectedGlyphRuns = {};
  const auto& glyphRuns = glyphRunList->glyphRuns();
  for (const auto& run : glyphRuns) {
    if (run.font.getTypeface() == nullptr) {
      continue;
    }
    GlyphRun rejectedGlyphRun = {};
    drawGlyphsAsDirectMask(run, state, brush, stroke, localClipBounds, &rejectedGlyphRun);
    if (rejectedGlyphRun.glyphs.empty()) {
      continue;
    }
    rejectedGlyphRun.font = run.font;
    rejectedGlyphRuns.emplace_back(std::move(rejectedGlyphRun));
  }

  if (rejectedGlyphRuns.empty()) {
    return;
  }

  if (!glyphRunList->hasColor() && glyphRunList->hasOutlines()) {
    auto rejectedGlyphRunList = std::make_shared<GlyphRunList>(std::move(rejectedGlyphRuns));
    drawGlyphsAsPath(std::move(rejectedGlyphRunList), state, brush, stroke, localClipBounds);
    return;
  }

  for (const auto& run : rejectedGlyphRuns) {
    drawGlyphsAsTransformedMask(run, state, brush, stroke);
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

void RenderContext::drawGlyphsAsDirectMask(const GlyphRun& sourceGlyphRun, const MCState& state,
                                           const Brush& brush, const Stroke* stroke,
                                           const Rect& localClipBounds,
                                           GlyphRun* rejectedGlyphRun) {
  auto compositor = getOpsCompositor();
  if (compositor == nullptr) {
    return;
  }

  const auto maxScale = state.matrix.getMaxScale();
  const auto inverseScale = 1.0f / maxScale;
  auto font = sourceGlyphRun.font;
  if (!FloatNearlyEqual(maxScale, 1.0f)) {
    font = font.makeWithSize(font.getSize() * maxScale);
  }

  const auto maskFormat = GetMaskFormat(font);
  auto typeface = font.getTypeface();
  std::unique_ptr<Stroke> scaledStroke = nullptr;
  if (!font.hasColor() && stroke != nullptr) {
    scaledStroke = std::make_unique<Stroke>(*stroke);
    scaledStroke->width *= maxScale;
  }

  BytesKey strikeKey;
  const auto typefaceID = GetTypefaceID(typeface.get(), typeface->isCustom());
  const auto backingSize = font.scalerContext->getBackingSize();
  CreateStrikeKey(typefaceID, backingSize, font.isFauxBold(), scaledStroke.get(), &strikeKey);

  AtlasCell atlasCell{maskFormat};
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  auto drawingManager = getContext()->drawingManager();
  const auto nextFlushToken = atlasManager->nextFlushToken();
  auto strike = getContext()->atlasStrikeCache()->findOrCreateStrike(strikeKey);
  DEBUG_ASSERT(strike != nullptr);
  const auto atlasBrush = brush.makeWithMatrix(state.matrix);
  const auto glyphRenderScale = font.scalerContext->getSize() / font.scalerContext->getBackingSize();
  const auto sampling = GetSamplingOptions(glyphRenderScale, font.isFauxItalic(), state.matrix);
  size_t index = 0;

  for (auto& glyphID : sourceGlyphRun.glyphs) {
    auto glyphPosition = sourceGlyphRun.positions[index++];
    int maxDimension = 0;
    if (!IsGlyphVisible(font, typeface->isCustom(), glyphID, localClipBounds, scaledStroke.get(),
                        inverseScale, glyphPosition, &maxDimension)) {
      continue;
    }

    if (maxDimension >= Atlas::MaxCellSize) {
      rejectedGlyphRun->glyphs.push_back(glyphID);
      rejectedGlyphRun->positions.push_back(glyphPosition);
      continue;
    }

    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);
    Point glyphOffset = {};
    auto atlasGlyph = strike->getGlyph(glyphID);
    DEBUG_ASSERT(atlasGlyph != nullptr);
    auto& atlasLocator = atlasGlyph->atlasLocator;
    if (atlasManager->hasGlyph(maskFormat, atlasGlyph)) {
      glyphOffset = atlasGlyph->offset;
    } else {
      bool shouldRetry = false;
      auto glyphCodec = GetGlyphCodec(font, font.scalerContext, glyphID, scaledStroke.get(),
                                      &glyphOffset, &shouldRetry);
      if (glyphCodec == nullptr) {
        if (shouldRetry) {
          rejectedGlyphRun->glyphs.push_back(glyphID);
          rejectedGlyphRun->positions.push_back(glyphPosition);
        }
        continue;
      }

      atlasGlyph->offset = glyphOffset;
      atlasCell.width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell.height = static_cast<uint16_t>(glyphCodec->height());

      if (atlasManager->addCellToAtlas(atlasCell, nextFlushToken, &atlasLocator)) {
        auto pageIndex = atlasLocator.pageIndex();
        const auto& atlasRect = atlasLocator.getLocation();
        drawingManager->addAtlasCellTask(textureProxies[pageIndex],
                                         Point::Make(atlasRect.x(), atlasRect.y()),
                                         std::move(glyphCodec));
      } else {
        rejectedGlyphRun->glyphs.push_back(glyphID);
        rejectedGlyphRun->positions.push_back(glyphPosition);
        continue;
      }
    }

    atlasManager->setPlotUseToken(plotUseUpdater, atlasLocator.plotLocator(), maskFormat,
                                  nextFlushToken);
    auto textureProxy = textureProxies[atlasLocator.pageIndex()];
    if (textureProxy == nullptr) {
      rejectedGlyphRun->glyphs.push_back(glyphID);
      rejectedGlyphRun->positions.push_back(glyphPosition);
      continue;
    }

    auto glyphState = state;
    GetGlyphMatrix(glyphRenderScale, glyphOffset, font.isFauxItalic(), &glyphState.matrix);
    auto& rect = atlasLocator.getLocation();
    ComputeGlyphFinalMatrix(rect, state.matrix, inverseScale, glyphPosition, &glyphState.matrix,
                            sampling.minFilterMode == FilterMode::Nearest);
    compositor->fillTextAtlas(std::move(textureProxy), rect, sampling, glyphState,
                              atlasBrush);
  }
}
void RenderContext::drawGlyphsAsPath(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const MCState& state, const Brush& brush, const Stroke* stroke,
                                     Rect& localClipBounds) {
  Path clipPath = {};
  if (brush.antiAlias) {
    localClipBounds.outset(1.0f, 1.0f);
  }
  clipPath.addRect(localClipBounds);
  std::shared_ptr<Shape> shape = std::make_shared<TextShape>(std::move(glyphRunList));
  shape = Shape::ApplyStroke(std::move(shape), stroke);

  shape = Shape::Merge(std::move(shape), Shape::MakeFrom(std::move(clipPath)), PathOp::Intersect);
  if (auto compositor = getOpsCompositor()) {
    compositor->drawShape(std::move(shape), state, brush);
  }
}

void RenderContext::drawGlyphsAsTransformedMask(const GlyphRun& sourceGlyphRun,
                                                const MCState& state, const Brush& brush,
                                                const Stroke* stroke) {
  auto compositor = getOpsCompositor();
  if (compositor == nullptr) {
    return;
  }

  const auto maxScale = state.matrix.getMaxScale();
  auto font = sourceGlyphRun.font;
  if (!FloatNearlyEqual(maxScale, 1.0f)) {
    font = font.makeWithSize(font.getSize() * maxScale);
  }
  std::unique_ptr<Stroke> scaledStroke = nullptr;
  if (!font.hasColor() && stroke) {
    scaledStroke = std::make_unique<Stroke>(*stroke);
    scaledStroke->width *= maxScale;
  }
  static constexpr float MaxAtlasDimension = Atlas::MaxCellSize - 2.f;
  auto cellScale = 1.f;
  auto maxDimension = FindMaxGlyphDimension(font, sourceGlyphRun.glyphs, scaledStroke.get());
  while (maxDimension > MaxAtlasDimension) {
    auto reductionFactor = MaxAtlasDimension / maxDimension;
    font = font.makeWithSize(font.getSize() * reductionFactor);
    if (scaledStroke) {
      scaledStroke->width *= reductionFactor;
    }
    maxDimension = FindMaxGlyphDimension(font, sourceGlyphRun.glyphs, scaledStroke.get());
    cellScale *= reductionFactor;
  }

  auto maskFormat = GetMaskFormat(font);
  auto typeface = font.getTypeface();
  BytesKey strikeKey;
  const auto typefaceID = GetTypefaceID(typeface.get(), typeface->isCustom());
  const auto backingSize = font.scalerContext->getBackingSize();
  CreateStrikeKey(typefaceID, backingSize, font.isFauxBold(), scaledStroke.get(), &strikeKey);

  AtlasCell atlasCell{maskFormat};
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  auto nextFlushToken = atlasManager->nextFlushToken();
  auto drawingManager = getContext()->drawingManager();
  auto strike = getContext()->atlasStrikeCache()->findOrCreateStrike(strikeKey);
  const auto atlasBrush = brush.makeWithMatrix(state.matrix);
  const auto glyphRenderScale = font.scalerContext->getSize() / font.scalerContext->getBackingSize();
  size_t index = 0;

  for (auto& glyphID : sourceGlyphRun.glyphs) {
    auto glyphPosition = sourceGlyphRun.positions[index++];
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);
    Point glyphOffset = {};
    auto atlasGlyph = strike->getGlyph(glyphID);
    DEBUG_ASSERT(atlasGlyph != nullptr);
    auto& atlasLocator = atlasGlyph->atlasLocator;

    if (atlasManager->hasGlyph(maskFormat, atlasGlyph)) {
      glyphOffset = atlasGlyph->offset;
    } else {
      auto glyphCodec =
          GetGlyphCodec(font, font.scalerContext, glyphID, scaledStroke.get(), &glyphOffset);
      if (glyphCodec == nullptr) {
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
    GetGlyphMatrix(glyphRenderScale, glyphOffset, font.isFauxItalic(), &glyphState.matrix);
    auto rect = atlasLocator.getLocation();
    ComputeGlyphFinalMatrix(rect, state.matrix, 1.f / (maxScale * cellScale), glyphPosition,
                            &glyphState.matrix, false);
    compositor->fillTextAtlas(std::move(textureProxy), rect,
                              SamplingOptions(FilterMode::Linear, MipmapMode::None), glyphState,
                              atlasBrush);
  }
}
}  // namespace tgfx
