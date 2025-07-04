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
#include "core/PathRasterizer.h"
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/Rasterizer.h"
#include "core/ScalerContext.h"
#include "core/UserTypeface.h"
#include "core/images/SubsetImage.h"
#include "core/shapes/TextShape.h"
#include "core/utils/ApplyStrokeToBounds.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"

namespace tgfx {
static uint32_t GetTypefaceID(const Typeface* typeface, bool isCustom) {
  return isCustom ? static_cast<const UserTypeface*>(typeface)->builderID() : typeface->uniqueID();
}

static void ComputeAtlasKey(const Font& font, uint32_t typefaceID, GlyphID glyphID,
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
      ApplyStrokeToBounds(*stroke, &bounds, true);
    }
    maxDimension = std::max(maxDimension, std::max(bounds.width(), bounds.height()));
  }
  return maxDimension;
}

static std::shared_ptr<ImageCodec> GetGlyphCodec(const Font& font, GlyphID glyphID,
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
  if (clipBounds.isEmpty() || !clipBounds.intersect(bounds)) {
    return;
  }

  std::vector<GlyphRun> rejectedGlyphRuns;
  const auto& glyphRuns = glyphRunList->glyphRuns();
  for (size_t i = 0; i < glyphRuns.size(); ++i) {
    auto& run = glyphRuns[i];
    if (run.font.getTypeface() == nullptr) {
      continue;
    }
    GlyphRun rejectedGlyphRun;
    drawGlyphsAsDirectMask(run, state, fill, stroke, &rejectedGlyphRun);
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
    drawGlyphsAsPath(std::move(rejectedGlyphRunList), state, fill, stroke, clipBounds);
    return;
  }

  for (const auto& run : rejectedGlyphRuns) {
    drawGlyphsAsTransformedMask(run, state, fill, stroke);
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

void RenderContext::drawGlyphsAsDirectMask(const GlyphRun& sourceGlyphRun, const MCState& state,
                                           const Fill& fill, const Stroke* stroke,
                                           GlyphRun* rejectedGlyphRun) {
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

  AtlasCell atlasCell;
  size_t index = 0;
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  auto drawingManager = getContext()->drawingManager();
  auto nextFlushToken = atlasManager->nextFlushToken();
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
    if (maxDimension >= Atlas::MaxCellSize) {
      rejectedGlyphRun->glyphs.push_back(glyphID);
      rejectedGlyphRun->positions.push_back(glyphPosition);
      continue;
    }

    auto typeface = font.getTypeface();
    BytesKey glyphKey;
    ComputeAtlasKey(font, GetTypefaceID(typeface.get(), typeface->isCustom()), glyphID,
                    scaledStroke.get(), glyphKey);

    auto maskFormat = GetMaskFormat(font);
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);

    auto glyphState = state;
    AtlasCellLocator cellLocator;
    auto& atlasLocator = cellLocator.atlasLocator;
    if (atlasManager->getCellLocator(maskFormat, glyphKey, cellLocator)) {
      glyphState.matrix = cellLocator.matrix;
    } else {
      auto glyphCodec = GetGlyphCodec(font, glyphID, scaledStroke.get(), &glyphState.matrix);
      if (glyphCodec == nullptr) {
        rejectedGlyphRun->glyphs.push_back(glyphID);
        rejectedGlyphRun->positions.push_back(glyphPosition);
        continue;
      }
      atlasCell._key = std::move(glyphKey);
      atlasCell._maskFormat = maskFormat;
      atlasCell._width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell._height = static_cast<uint16_t>(glyphCodec->height());
      atlasCell._matrix = glyphState.matrix;

      if (atlasManager->addCellToAtlas(atlasCell, nextFlushToken, atlasLocator)) {
        auto pageIndex = atlasLocator.pageIndex();
        auto offset = Point::Make(atlasLocator.getLocation().left, atlasLocator.getLocation().top);
        drawingManager->addAtlasCellCodecTask(textureProxies[pageIndex], offset,
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
    auto rect = atlasLocator.getLocation();
    glyphState.matrix.postScale(1.f / maxScale, 1.f / maxScale);
    glyphState.matrix.postTranslate(glyphPosition.x, glyphPosition.y);
    glyphState.matrix.postConcat(state.matrix);
    glyphState.matrix.preTranslate(-rect.x(), -rect.y());

    auto glyphFill = fill.makeWithMatrix(state.matrix);
    glyphFill.antiAlias = false;
    if (auto compositor = getOpsCompositor()) {
      compositor->fillTextAtlas(std::move(textureProxy), rect, {}, glyphState, glyphFill);
    }
  }
}
void RenderContext::drawGlyphsAsPath(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const MCState& state, const Fill& fill, const Stroke* stroke,
                                     const Rect& clipBounds) {
  Matrix inverseMatrix = {};
  if (!state.matrix.invert(&inverseMatrix)) {
    return;
  }
  Path clipPath = {};
  auto localClipBounds = inverseMatrix.mapRect(clipBounds);
  clipPath.addRect(localClipBounds);
  std::shared_ptr<Shape> shape =
      std::make_shared<TextShape>(std::move(glyphRunList), state.matrix.getMaxScale());
  shape = Shape::ApplyStroke(std::move(shape), stroke);
  shape = Shape::Merge(std::move(shape), Shape::MakeFrom(std::move(clipPath)), PathOp::Intersect);
  if (auto compositor = getOpsCompositor()) {
    compositor->fillShape(std::move(shape), state, fill);
  }
}

void RenderContext::drawGlyphsAsTransformedMask(const GlyphRun& sourceGlyphRun,
                                                const MCState& state, const Fill& fill,
                                                const Stroke* stroke) {
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
  static constexpr float MaxAtlasDimension = Atlas::MaxCellSize - 2.f;
  auto cellScale = 1.f;
  auto maxDimension = FindMaxGlyphDimension(font, sourceGlyphRun.glyphs, scaledStroke.get());
  while (maxDimension > MaxAtlasDimension) {
    auto reductionFactor = MaxAtlasDimension / maxDimension;
    font = font.makeWithSize(font.getSize() * reductionFactor);

    if (scaledStroke) {
      scaledStroke->width *= reductionFactor;
      scaledStroke->miterLimit *= reductionFactor;
    }
    maxDimension = FindMaxGlyphDimension(font, sourceGlyphRun.glyphs, scaledStroke.get());
    cellScale *= reductionFactor;
  }

  AtlasCell atlasCell;
  size_t index = 0;
  PlotUseUpdater plotUseUpdater;
  auto atlasManager = getContext()->atlasManager();
  auto nextFlushToken = atlasManager->nextFlushToken();
  auto drawingManager = getContext()->drawingManager();
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
    ComputeAtlasKey(font, GetTypefaceID(typeface, typeface->isCustom()), glyphID,
                    scaledStroke.get(), glyphKey);
    auto maskFormat = GetMaskFormat(font);
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);

    auto glyphState = state;
    AtlasCellLocator glyphLocator;
    auto& atlasLocator = glyphLocator.atlasLocator;
    if (atlasManager->getCellLocator(maskFormat, glyphKey, glyphLocator)) {
      glyphState.matrix = glyphLocator.matrix;
    } else {
      auto glyphCodec = GetGlyphCodec(font, glyphID, scaledStroke.get(), &glyphState.matrix);
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

      auto pageIndex = atlasLocator.pageIndex();
      auto offset = Point::Make(atlasLocator.getLocation().left, atlasLocator.getLocation().top);
      drawingManager->addAtlasCellCodecTask(textureProxies[pageIndex], offset,
                                            std::move(glyphCodec));
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

    auto glyphFill = fill.makeWithMatrix(state.matrix);
    glyphFill.antiAlias = false;
    if (auto compositor = getOpsCompositor()) {
      compositor->fillTextAtlas(std::move(textureProxy), rect, {}, glyphState, glyphFill);
    }
  }
}
}  // namespace tgfx
