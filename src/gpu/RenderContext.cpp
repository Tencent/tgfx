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
#include <tgfx/core/Surface.h>
#include "core/Atlas.h"
#include "core/AtlasCell.h"
#include "core/AtlasManager.h"
#include "core/GlyphSource.h"
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/Rasterizer.h"
#include "core/ScalerContext.h"
#include "core/UserTypeface.h"
#include "core/images/SubsetImage.h"
#include "core/utils/ApplyStrokeToBounds.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/tasks/TextAtlasUploadTask.h"

namespace tgfx {
static uint32_t getTypefaceID(const Typeface* typeface, bool isCustom) {
  return isCustom ? static_cast<const UserTypeface*>(typeface)->builderID() : typeface->uniqueID();
}

static void computeAtlasKey(const Font& font, uint32_t typefaceID, GlyphID glyphID,
                            const Stroke* stroke, BytesKey& key) {
  key.write(font.getSize());
  key.write(typefaceID);
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

static MaskFormat getMaskFormat(const Font& font) {
  if (!font.hasColor()) {
    return MaskFormat::A8;
  }
#ifdef __APPLE__
  return MaskFormat::BGRA;
#else
  return MaskFormat::RGBA;
#endif
}

static float findMaxGlyphDimension(const Font& font, const std::vector<GlyphID>& glyphIDs,
                                   const Stroke* stroke) {
  float maxDimension = 0.f;
  for (auto& glyphID : glyphIDs) {
    auto bounds = font.getBounds(glyphID);
    if (bounds.isEmpty()) {
      continue;
    }
    if (stroke != nullptr) {
      ApplyStrokeToBounds(*stroke, &bounds, true);
    }
    maxDimension = std::max(maxDimension, std::max(bounds.width(), bounds.height()));
  }
  return maxDimension;
}

static std::shared_ptr<ImageCodec> getGlyphCodec(const Font& font, GlyphID glyphID,
                                                 const Stroke* stroke, Matrix* matrix) {
  auto glyphCodec = font.getImage(glyphID, stroke, matrix);
  if (glyphCodec) {
    return glyphCodec;
  }
  auto shape = Shape::MakeFrom(font, glyphID);
  if (shape == nullptr) {
    return nullptr;
  }
  auto bounds = shape->getBounds();
  if (bounds.isEmpty()) {
    return nullptr;
  }
  if (stroke != nullptr) {
    ApplyStrokeToBounds(*stroke, &bounds, true);
    shape = Shape::ApplyStroke(std::move(shape), stroke);
  }
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  glyphCodec = PathRasterizer::Make(width, height, std::move(shape), true, true);
  matrix->setTranslate(bounds.x(), bounds.y());
  return glyphCodec;
}

RenderContext::RenderContext(std::shared_ptr<RenderTargetProxy> proxy, uint32_t renderFlags,
                             bool clearAll, Surface* surface)
    : renderTarget(std::move(proxy)), renderFlags(renderFlags), surface(surface) {
  if (clearAll) {
    auto drawingManager = renderTarget->getContext()->drawingManager();
    opsCompositor = drawingManager->addOpsCompositor(renderTarget, renderFlags);
    opsCompositor->fillRect(renderTarget->bounds(), MCState{},
                            {Color::Transparent(), BlendMode::Src});
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

void RenderContext::drawFill(const Fill& fill) {
  if (auto compositor = getOpsCompositor(fill.isOpaque())) {
    compositor->fillRect(renderTarget->bounds(), {}, fill);
  }
}

void RenderContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillRect(rect, state, fill);
  }
}

void RenderContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill,
                              const Stroke* stroke) {
  if (auto compositor = getOpsCompositor()) {
    compositor->drawRRect(rRect, state, fill, stroke);
  }
}

void RenderContext::drawPath(const Path& path, const MCState& state, const Fill& fill) {
  // Temporarily use drawShape for rendering, and perform merging in the compositor later.
  drawShape(Shape::MakeFrom(path), state, fill);
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

void RenderContext::drawShape(std::shared_ptr<Shape> shape, const MCState& state,
                              const Fill& fill) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillShape(std::move(shape), state, fill);
  }
}

void RenderContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                  const SamplingOptions& sampling, const MCState& state,
                                  const Fill& fill, SrcRectConstraint constraint) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(image->isAlphaOnly() || fill.shader == nullptr);
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

  compositor->fillImage(std::move(image), rect, samplingOptions, state, fill, constraint);
}

void RenderContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const MCState& state, const Fill& fill, const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  if (getContext() == nullptr || getContext()->atlasManager() == nullptr) {
    return;
  }
  auto maxScale = state.matrix.getMaxScale();
  if (FloatNearlyZero(maxScale)) {
    return;
  }
  auto bounds = glyphRunList->getBounds(maxScale);
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
  }
  state.matrix.mapRect(&bounds);  // To device space
  auto clipBounds = getClipBounds(state.clip);
  if (clipBounds.isEmpty()) {
    return;
  }
  if (!clipBounds.intersect(bounds)) {
    return;
  }

  size_t maxGlyphRunCount = 0;
  for (const auto& run : glyphRunList->glyphRuns()) {
    maxGlyphRunCount = std::max(maxGlyphRunCount, run.glyphs.size());
  }

  GlyphRun sourceGlyphRun, rejectedGlyphRun;
  sourceGlyphRun.glyphs.reserve(maxGlyphRunCount);
  rejectedGlyphRun.glyphs.reserve(maxGlyphRunCount);
  sourceGlyphRun.positions.reserve(maxGlyphRunCount);
  rejectedGlyphRun.positions.reserve(maxGlyphRunCount);

  for (const auto& run : glyphRunList->glyphRuns()) {
    if (run.font.getTypeface() == nullptr) {
      continue;
    }
    rejectedGlyphRun.font = run.font;
    directMaskDrawing(run, state, fill, stroke, rejectedGlyphRun);
    if (rejectedGlyphRun.glyphs.empty()) {
      continue;
    }

    std::swap(sourceGlyphRun, rejectedGlyphRun);
    rejectedGlyphRun.positions.clear();
    rejectedGlyphRun.glyphs.clear();

    pathDrawing(sourceGlyphRun, state, fill, stroke, clipBounds, rejectedGlyphRun);
    if (rejectedGlyphRun.glyphs.empty()) {
      continue;
    }

    transformedMaskDrawing(rejectedGlyphRun, state, fill, stroke);
  }
}

void RenderContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, state);
}

void RenderContext::drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                              const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(fill.shader == nullptr);
  Matrix viewMatrix = {};
  Rect bounds = {};
  if (filter) {
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
  auto imageRect = Rect::MakeWH(image->width(), image->height());
  drawImageRect(std::move(image), imageRect, {}, drawState, fill.makeWithMatrix(viewMatrix),
                SrcRectConstraint::Fast);
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
    opsCompositor = drawingManager->addOpsCompositor(renderTarget, renderFlags);
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
    opsCompositor = drawingManager->addOpsCompositor(renderTarget, renderFlags);
    Fill fill = {{}, BlendMode::Src, false};
    opsCompositor->fillImage(std::move(oldContent), renderTarget->bounds(), {}, MCState{}, fill,
                             SrcRectConstraint::Fast);
  }
}

void RenderContext::directMaskDrawing(const GlyphRun& sourceGlyphRun, const MCState& state,
                                      const Fill& fill, const Stroke* stroke,
                                      GlyphRun& rejectedGlyphRun) const {
  AtlasCell glyphCell;
  auto nextFlushToken = getContext()->drawingManager()->nextFlushToken();
  PlotUseUpdater plotUseUpdater;

  auto maxScale = state.matrix.getMaxScale();
  auto hasScale = !FloatNearlyEqual(maxScale, 1.0f);
  auto font = sourceGlyphRun.font;
  if (hasScale) {
    font = font.makeWithSize(font.getSize() * maxScale);
  }

  std::unique_ptr<Stroke> scaledStroke;
  if (stroke) {
    scaledStroke =
        std::make_unique<Stroke>(stroke->width * maxScale, stroke->cap, stroke->join, maxScale);
  }
  auto atlasManager = getContext()->atlasManager();
  size_t index = 0;
  for (auto& glyphID : sourceGlyphRun.glyphs) {
    auto glyphPosition = sourceGlyphRun.positions[index++];
    auto bounds = font.getBounds(glyphID);
    if (bounds.isEmpty()) {
      continue;
    }
    if (scaledStroke) {
      ApplyStrokeToBounds(*scaledStroke, &bounds, true);
    }
    auto maxDimension = static_cast<int>(ceilf(std::max(bounds.width(), bounds.height())));
    if (maxDimension >= Atlas::kMaxCellSize) {
      rejectedGlyphRun.glyphs.push_back(glyphID);
      rejectedGlyphRun.positions.push_back(glyphPosition);
      continue;
    }

    auto typeface = font.getTypeface();
    BytesKey glyphKey;
    computeAtlasKey(font, getTypefaceID(typeface.get(), typeface->isCustom()), glyphID,
                    scaledStroke.get(), glyphKey);

    auto maskFormat = getMaskFormat(font);
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);

    auto glyphState = state;
    AtlasCellLocator cellLocator;
    auto& atlasLocator = cellLocator.atlasLocator;
    if (atlasManager->getCellLocator(maskFormat, glyphKey, cellLocator)) {
      glyphState.matrix = cellLocator.matrix;
    } else {
      auto glyphCodec = getGlyphCodec(font, glyphID, scaledStroke.get(), &glyphState.matrix);
      if (glyphCodec == nullptr) {
        rejectedGlyphRun.glyphs.push_back(glyphID);
        rejectedGlyphRun.positions.push_back(glyphPosition);
        continue;
      }
      glyphCell._key = std::move(glyphKey);
      glyphCell._maskFormat = maskFormat;
      glyphCell._width = static_cast<uint16_t>(glyphCodec->width());
      glyphCell._height = static_cast<uint16_t>(glyphCodec->height());
      glyphCell._matrix = glyphState.matrix;

      if (atlasManager->addCellToAtlas(glyphCell, nextFlushToken, atlasLocator)) {
        auto source = GlyphSource::MakeFrom(glyphCodec);
        auto offset = Point::Make(atlasLocator.getLocation().left, atlasLocator.getLocation().top);
        auto task = getContext()->drawingBuffer()->make<TextAtlasUploadTask>(
            UniqueKey::Make(), std::move(source), textureProxies[atlasLocator.pageIndex()], offset);
        getContext()->drawingManager()->addResourceTask(std::move(task));
      } else {
        rejectedGlyphRun.glyphs.push_back(glyphID);
        rejectedGlyphRun.positions.push_back(glyphPosition);
        continue;
      }
    }
    atlasManager->setPlotUseToken(plotUseUpdater, atlasLocator.plotLocator(), maskFormat,
                                  nextFlushToken);
    auto textureProxy = textureProxies[atlasLocator.pageIndex()];
    if (textureProxy == nullptr) {
      rejectedGlyphRun.glyphs.push_back(glyphID);
      rejectedGlyphRun.positions.push_back(glyphPosition);
      continue;
    }
    auto rect = atlasLocator.getLocation();
    glyphState.matrix.postScale(1.f / maxScale, 1.f / maxScale);
    glyphState.matrix.postTranslate(glyphPosition.x, glyphPosition.y);
    glyphState.matrix.postConcat(state.matrix);
    glyphState.matrix.preTranslate(-rect.x(), -rect.y());

    auto newFill = fill;
    newFill.antiAlias = false;
    drawGlyphAtlas(std::move(textureProxy), rect, {}, glyphState, newFill, state.matrix);
  }
}
void RenderContext::pathDrawing(GlyphRun& sourceGlyphRun, const MCState& state, const Fill& fill,
                                const Stroke* stroke, const Rect& clipBounds,
                                GlyphRun& rejectedGlyphRun) const {
  if (!sourceGlyphRun.font.hasOutlines()) {
    rejectedGlyphRun = std::move(sourceGlyphRun);
  }
  auto maxScale = state.matrix.getMaxScale();
  Path totalPath = {};
  auto font = sourceGlyphRun.font;
  if (!FloatNearlyEqual(maxScale, 1.0f)) {
    font = font.makeWithSize(font.getSize() * maxScale);
  }
  size_t index = 0;
  Rect bounds = {};
  auto& positions = sourceGlyphRun.positions;
  for (auto& glyphID : sourceGlyphRun.glyphs) {
    Path glyphPath = {};
    auto& position = positions[index];
    if (font.getPath(glyphID, &glyphPath)) {
      auto glyphMatrix = Matrix::MakeScale(1.0f / maxScale, 1.0f / maxScale);
      glyphMatrix.postTranslate(position.x, position.y);
      glyphPath.transform(glyphMatrix);
      totalPath.addPath(glyphPath);
      auto glyphBounds = font.getBounds(glyphID);
      glyphBounds.offset(position.x * maxScale, position.y * maxScale);
      bounds.join(glyphBounds);
    } else {
      rejectedGlyphRun.glyphs.push_back(glyphID);
      rejectedGlyphRun.positions.push_back(position);
    }
    index++;
  }
  bounds.scale(1.0f / maxScale, 1.0f / maxScale);
  if (totalPath.isEmpty()) {
    rejectedGlyphRun = std::move(sourceGlyphRun);
    return;
  }
  if (stroke) {
    ApplyStrokeToBounds(*stroke, &bounds);
  }
  state.matrix.mapRect(&bounds);
  if (!bounds.intersects(clipBounds)) {
    return;
  }
  auto rasterizeMatrix = state.matrix;
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto shape = Shape::MakeFrom(totalPath);
  shape = Shape::ApplyStroke(std::move(shape), stroke);
  shape = Shape::ApplyMatrix(std::move(shape), rasterizeMatrix);
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto rasterizer = PathRasterizer::Make(width, height, std::move(shape), true, true);
  auto image = Image::MakeFrom(std::move(rasterizer));
  if (image == nullptr) {
    rejectedGlyphRun = std::move(sourceGlyphRun);
    return;
  }
  auto newState = state;
  newState.matrix = Matrix::MakeTrans(bounds.x(), bounds.y());
  auto rect = Rect::MakeWH(image->width(), image->height());
  opsCompositor->fillImage(std::move(image), rect, {}, newState,
                           fill.makeWithMatrix(rasterizeMatrix), SrcRectConstraint::Fast);
}

void RenderContext::transformedMaskDrawing(const GlyphRun& sourceGlyphRun, const MCState& state,
                                           const Fill& fill, const Stroke* stroke) const {
  AtlasCell atlasCell;
  auto nextFlushToken = getContext()->drawingManager()->nextFlushToken();
  PlotUseUpdater plotUseUpdater;

  auto maxScale = state.matrix.getMaxScale();
  auto hasScale = !FloatNearlyEqual(maxScale, 1.0f);
  auto font = sourceGlyphRun.font;
  if (hasScale) {
    font = font.makeWithSize(font.getSize() * maxScale);
  }

  std::unique_ptr<Stroke> scaledStroke = nullptr;
  if (stroke) {
    scaledStroke =
        std::make_unique<Stroke>(stroke->width * maxScale, stroke->cap, stroke->join, maxScale);
  }
  static constexpr float kMaxAtlasDimension = Atlas::kMaxCellSize - 2.f;
  auto cellScale = 1.f;
  auto maxDimension = findMaxGlyphDimension(font, sourceGlyphRun.glyphs, scaledStroke.get());
  while (maxDimension > kMaxAtlasDimension) {
    auto reductionFactor = kMaxAtlasDimension / maxDimension;
    font = font.makeWithSize(font.getSize() * reductionFactor);

    if (scaledStroke) {
      scaledStroke->width *= reductionFactor;
      scaledStroke->miterLimit *= reductionFactor;
    }
    maxDimension = findMaxGlyphDimension(font, sourceGlyphRun.glyphs, scaledStroke.get());
    cellScale *= reductionFactor;
  }

  auto atlasManager = getContext()->atlasManager();
  size_t index = 0;
  for (auto& glyphID : sourceGlyphRun.glyphs) {
    auto glyphPosition = sourceGlyphRun.positions[index++];
    auto bounds = font.getBounds(glyphID);
    if (bounds.isEmpty()) {
      continue;
    }
    if (scaledStroke) {
      ApplyStrokeToBounds(*scaledStroke, &bounds, true);
    }

    auto typeface = font.getTypeface().get();
    BytesKey glyphKey;
    computeAtlasKey(font, getTypefaceID(typeface, typeface->isCustom()), glyphID,
                    scaledStroke.get(), glyphKey);
    auto maskFormat = getMaskFormat(font);
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);

    auto glyphState = state;
    AtlasCellLocator glyphLocator;
    auto& atlasLocator = glyphLocator.atlasLocator;
    if (atlasManager->getCellLocator(maskFormat, glyphKey, glyphLocator)) {
      glyphState.matrix = glyphLocator.matrix;
    } else {
      auto glyphCodec = getGlyphCodec(font, glyphID, scaledStroke.get(), &glyphState.matrix);
      if (glyphCodec == nullptr) {
        continue;
      }
      atlasCell._key = std::move(glyphKey);
      atlasCell._maskFormat = maskFormat;
      atlasCell._width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell._height = static_cast<uint16_t>(glyphCodec->height());
      atlasCell._matrix = glyphState.matrix;
      if (!atlasManager->addCellToAtlas(atlasCell, nextFlushToken, atlasLocator)) {
        continue;
      }
      auto source = GlyphSource::MakeFrom(glyphCodec);
      auto offset = Point::Make(atlasLocator.getLocation().left, atlasLocator.getLocation().top);
      auto task = getContext()->drawingBuffer()->make<TextAtlasUploadTask>(
          UniqueKey::Make(), std::move(source), textureProxies[atlasLocator.pageIndex()], offset);
      getContext()->drawingManager()->addResourceTask(std::move(task));
    }

    atlasManager->setPlotUseToken(plotUseUpdater, atlasLocator.plotLocator(), maskFormat,
                                  nextFlushToken);
    auto textureProxy = textureProxies[atlasLocator.pageIndex()];
    if (textureProxy == nullptr) {
      continue;
    }
    auto rect = atlasLocator.getLocation();
    glyphState.matrix.postScale(1.f / (maxScale * cellScale), 1.f / (maxScale * cellScale));
    glyphState.matrix.postTranslate(glyphPosition.x, glyphPosition.y);
    glyphState.matrix.postConcat(state.matrix);
    glyphState.matrix.preTranslate(-rect.x(), -rect.y());

    auto newFill = fill;
    newFill.antiAlias = false;
    drawGlyphAtlas(std::move(textureProxy), rect, {}, glyphState, newFill, state.matrix);
  }
}

void RenderContext::drawGlyphAtlas(std::shared_ptr<TextureProxy> textureProxy, const Rect& rect,
                                   const SamplingOptions& sampling, const MCState& state,
                                   const Fill& fill, const Matrix& viewMatrix) const {
  DEBUG_ASSERT(textureProxy != nullptr);
  DEBUG_ASSERT(textureProxy->isAlphaOnly() || fill.shader == nullptr);
  opsCompositor->fillTextAtlas(std::move(textureProxy), rect, sampling, state, fill, viewMatrix);
}
}  // namespace tgfx
