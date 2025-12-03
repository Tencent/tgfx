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
#include "core/AtlasCell.h"
#include "core/AtlasManager.h"
#include "core/GlyphRasterizer.h"
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/ScalerContext.h"
#include "core/UserTypeface.h"
#include "core/images/SubsetImage.h"
#include "core/shapes/TextShape.h"
#include "core/utils/MathExtra.h"
#include "core/utils/PixelFormatUtil.h"
#include "core/utils/StrokeUtils.h"
#include "gpu/DrawingManager.h"

namespace tgfx {
static uint32_t GetTypefaceID(const Typeface* typeface, bool isCustom) {
  return isCustom ? static_cast<const UserTypeface*>(typeface)->builderID() : typeface->uniqueID();
}

static void ComputeAtlasKey(const Font& font, const std::shared_ptr<ScalerContext>& scalerContext,
                            uint32_t typefaceID, GlyphID glyphID, const Stroke* stroke,
                            BytesKey& key) {
  key.write(typefaceID);
  key.write(scalerContext->getBackingSize());
  if (font.hasColor()) {
    key.write(glyphID);
    return;
  }
  int packedID = glyphID;
  auto bold = static_cast<int>(font.isFauxBold());
  packedID |= (bold << (sizeof(glyphID) * 8));
  key.write(packedID);
  if (stroke != nullptr) {
    key.write(stroke->width);
    key.write(stroke->miterLimit);
    int zipValue = 0;
    auto cap = static_cast<int>(stroke->cap);
    auto join = static_cast<int>(stroke->join);
    zipValue |= (0b11 & cap);
    zipValue |= (0b1100 & (join << 2));
    key.write(zipValue);
  }
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
    const Stroke* stroke, Point* glyphOffset) {
  if (glyphID == 0) {
    return nullptr;
  }

  auto hasFauxBold = !font.hasColor() && font.isFauxBold();
  auto bounds = scalerContext->getImageTransform(glyphID, hasFauxBold, stroke, nullptr);
  if (!bounds.isEmpty()) {
    glyphOffset->x = bounds.left;
    glyphOffset->y = bounds.top;
    auto width = static_cast<int>(ceilf(bounds.width()));
    auto height = static_cast<int>(ceilf(bounds.height()));
    return std::make_shared<GlyphRasterizer>(width, height, scalerContext, glyphID, hasFauxBold,
                                             stroke);
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
    return nullptr;
  }
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
    shape = Shape::ApplyStroke(std::move(shape), stroke);
  }
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  glyphOffset->x = bounds.left;
  glyphOffset->y = bounds.top;
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
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

static bool IsGlyphVisible(const Font& font, GlyphID glyphID, const Rect& clipBounds,
                           const Stroke* stroke, float scale, const Point& glyphPosition,
                           int* maxDimension) {
  auto bounds = font.getBounds(glyphID);
  if (bounds.isEmpty()) {
    return false;
  }
  if (stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, &bounds);
  }
  if (maxDimension != nullptr) {
    *maxDimension = static_cast<int>(ceilf(std::max(bounds.width(), bounds.height())));
  }
  bounds.scale(scale, scale);
  bounds.offset(glyphPosition.x, glyphPosition.y);
  return Rect::Intersects(bounds, clipBounds);
}

static void GetGlyphMatrix(const std::shared_ptr<ScalerContext>& scalerContext,
                           const Point& glyphOffset, bool fauxItalic, Matrix* glyphMatrix) {
  glyphMatrix->setTranslate(glyphOffset.x, glyphOffset.y);
  auto scale = scalerContext->getSize() / scalerContext->getBackingSize();
  glyphMatrix->postScale(scale, scale);
  if (fauxItalic) {
    glyphMatrix->postSkew(ITALIC_SKEW, 0);
  }
}

static SamplingOptions GetSamplingOptions(const std::shared_ptr<ScalerContext>& scalerContext,
                                          bool fauxItalic, const Matrix& stateMatrix) {
  if (fauxItalic || !FloatNearlyEqual(scalerContext->getBackingSize(), scalerContext->getSize())) {
    return SamplingOptions{FilterMode::Linear, MipmapMode::None};
  }
  const auto isUniformScale = stateMatrix.isScaleTranslate() &&
                              FloatNearlyEqual(stateMatrix.getScaleX(), stateMatrix.getScaleY());
  const auto filterMode = isUniformScale ? FilterMode::Nearest : FilterMode::Linear;
  return SamplingOptions{filterMode, MipmapMode::None};
}

RenderContext::RenderContext(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                             bool clearAll, Surface* surface,
                             const std::shared_ptr<ColorSpace>& colorSpace)
    : renderTarget(std::move(proxy)), renderFlags(renderFlags), surface(surface),
      _colorSpace(colorSpace) {
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
    ApplyStrokeToBounds(*stroke, &bounds);
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
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
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

  auto maxScale = state.matrix.getMaxScale();
  auto inverseScale = 1.f / maxScale;
  auto font = sourceGlyphRun.font;
  if (!FloatNearlyEqual(maxScale, 1.0f)) {
    font = font.makeWithSize(font.getSize() * maxScale);
  }

  auto maskFormat = GetMaskFormat(font);
  auto typeface = font.getTypeface();
  std::unique_ptr<Stroke> scaledStroke = nullptr;
  if (!font.hasColor() && stroke != nullptr) {
    scaledStroke = std::make_unique<Stroke>(*stroke);
    scaledStroke->width *= maxScale;
  }
  AtlasCell atlasCell;
  atlasCell.maskFormat = maskFormat;
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  auto drawingManager = getContext()->drawingManager();
  auto nextFlushToken = atlasManager->nextFlushToken();
  size_t index = 0;

  for (auto& glyphID : sourceGlyphRun.glyphs) {
    auto glyphPosition = sourceGlyphRun.positions[index++];
    int maxDimension = 0;
    if (!IsGlyphVisible(font, glyphID, localClipBounds, scaledStroke.get(), inverseScale,
                        glyphPosition, &maxDimension)) {
      continue;
    }

    if (maxDimension >= Atlas::MaxCellSize) {
      rejectedGlyphRun->glyphs.push_back(glyphID);
      rejectedGlyphRun->positions.push_back(glyphPosition);
      continue;
    }

    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);
    AtlasCellLocator glyphLocator;
    auto& atlasLocator = glyphLocator.atlasLocator;
    Point glyphOffset = {};
    BytesKey glyphKey;
    ComputeAtlasKey(font, font.scalerContext, GetTypefaceID(typeface.get(), typeface->isCustom()),
                    glyphID, scaledStroke.get(), glyphKey);
    if (atlasManager->getCellLocator(maskFormat, glyphKey, glyphLocator)) {
      glyphOffset = glyphLocator.offset;
    } else {
      auto glyphCodec =
          GetGlyphCodec(font, font.scalerContext, glyphID, scaledStroke.get(), &glyphOffset);
      if (glyphCodec == nullptr) {
        rejectedGlyphRun->glyphs.push_back(glyphID);
        rejectedGlyphRun->positions.push_back(glyphPosition);
        continue;
      }

      atlasCell.key = std::move(glyphKey);
      atlasCell.offset = glyphOffset;
      atlasCell.width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell.height = static_cast<uint16_t>(glyphCodec->height());

      if (atlasManager->addCellToAtlas(atlasCell, nextFlushToken, atlasLocator)) {
        auto pageIndex = atlasLocator.pageIndex();
        auto atlasOffset =
            Point::Make(atlasLocator.getLocation().left, atlasLocator.getLocation().top);
        drawingManager->addAtlasCellTask(textureProxies[pageIndex], atlasOffset,
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
    GetGlyphMatrix(font.scalerContext, glyphOffset, font.isFauxItalic(), &glyphState.matrix);
    const auto rect = atlasLocator.getLocation();
    const auto sampling = GetSamplingOptions(font.scalerContext, font.isFauxItalic(), state.matrix);
    ComputeGlyphFinalMatrix(rect, state.matrix, inverseScale, glyphPosition, &glyphState.matrix,
                            sampling.minFilterMode == FilterMode::Nearest);
    compositor->fillTextAtlas(std::move(textureProxy), rect, sampling, glyphState,
                              brush.makeWithMatrix(state.matrix));
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

  auto maxScale = state.matrix.getMaxScale();
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
  AtlasCell atlasCell;
  atlasCell.maskFormat = maskFormat;
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  auto nextFlushToken = atlasManager->nextFlushToken();
  auto drawingManager = getContext()->drawingManager();
  size_t index = 0;

  for (auto& glyphID : sourceGlyphRun.glyphs) {
    auto glyphPosition = sourceGlyphRun.positions[index++];
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);
    AtlasCellLocator glyphLocator;
    auto& atlasLocator = glyphLocator.atlasLocator;
    Point glyphOffset = {};
    BytesKey glyphKey;
    ComputeAtlasKey(font, font.scalerContext, GetTypefaceID(typeface.get(), typeface->isCustom()),
                    glyphID, scaledStroke.get(), glyphKey);

    if (atlasManager->getCellLocator(maskFormat, glyphKey, glyphLocator)) {
      glyphOffset = glyphLocator.offset;
    } else {
      auto glyphCodec =
          GetGlyphCodec(font, font.scalerContext, glyphID, scaledStroke.get(), &glyphOffset);
      if (glyphCodec == nullptr) {
        continue;
      }

      atlasCell.key = std::move(glyphKey);
      atlasCell.offset = glyphOffset;
      atlasCell.width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell.height = static_cast<uint16_t>(glyphCodec->height());

      if (!atlasManager->addCellToAtlas(atlasCell, nextFlushToken, atlasLocator)) {
        continue;
      }
      auto pageIndex = atlasLocator.pageIndex();
      auto offset = Point::Make(atlasLocator.getLocation().left, atlasLocator.getLocation().top);
      drawingManager->addAtlasCellTask(textureProxies[pageIndex], offset, std::move(glyphCodec));
    }
    atlasManager->setPlotUseToken(plotUseUpdater, atlasLocator.plotLocator(), maskFormat,
                                  nextFlushToken);
    auto textureProxy = textureProxies[atlasLocator.pageIndex()];
    if (textureProxy == nullptr) {
      continue;
    }

    auto glyphState = state;
    GetGlyphMatrix(font.scalerContext, glyphOffset, font.isFauxItalic(), &glyphState.matrix);
    auto rect = atlasLocator.getLocation();
    ComputeGlyphFinalMatrix(rect, state.matrix, 1.f / (maxScale * cellScale), glyphPosition,
                            &glyphState.matrix, false);
    compositor->fillTextAtlas(std::move(textureProxy), rect,
                              SamplingOptions(FilterMode::Linear, MipmapMode::None), glyphState,
                              brush.makeWithMatrix(state.matrix));
  }
}
}  // namespace tgfx
