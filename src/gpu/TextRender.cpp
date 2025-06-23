/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "TextRender.h"
#include "core/GlyphSource.h"
#include "core/PathRasterizer.h"
#include "core/UserTypeface.h"
#include "core/atlas/AtlasTypes.h"
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

std::unique_ptr<TextRender> TextRender::MakeFrom(Context* context, OpsCompositor* opsCompositor,
                                                 std::shared_ptr<GlyphRunList> glyphRunList,
                                                 const Rect& clipBounds) {
  if (context == nullptr || context->atlasManager() == nullptr || opsCompositor == nullptr ||
      glyphRunList == nullptr) {
    return nullptr;
  }
  return std::unique_ptr<TextRender>(
      new TextRender(context, opsCompositor, std::move(glyphRunList), clipBounds));
}

TextRender::TextRender(Context* context, OpsCompositor* opsCompositor,
                       std::shared_ptr<GlyphRunList> glyphRunList, const Rect& clipBounds)
    : context(context), opsCompositor(opsCompositor), glyphRunList(std::move(glyphRunList)),
      atlasManager(context->atlasManager()), clipBounds(clipBounds) {
}

void TextRender::draw(const MCState& state, const Fill& fill, const Stroke* stroke) const {
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

    pathDrawing(sourceGlyphRun, state, fill, stroke, rejectedGlyphRun);
    if (rejectedGlyphRun.glyphs.empty()) {
      continue;
    }

    transformedMaskDrawing(rejectedGlyphRun, state, fill, stroke);
  }
}

void TextRender::directMaskDrawing(const GlyphRun& sourceGlyphRun, const MCState& state,
                                   const Fill& fill, const Stroke* stroke,
                                   GlyphRun& rejectedGlyphRun) const {
  AtlasCell glyphCell;
  auto nextFlushToken = context->drawingManager()->nextFlushToken();
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
        auto task = context->drawingBuffer()->make<TextAtlasUploadTask>(
            UniqueKey::Make(), std::move(source), textureProxies[atlasLocator.pageIndex()], offset);
        context->drawingManager()->addResourceTask(std::move(task));
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

void TextRender::pathDrawing(GlyphRun& sourceGlyphRun, const MCState& state, const Fill& fill,
                             const Stroke* stroke, GlyphRun& rejectedGlyphRun) const {
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
  auto& positions = sourceGlyphRun.positions;
  for (auto& glyphID : sourceGlyphRun.glyphs) {
    Path glyphPath = {};
    auto& position = positions[index];
    if (font.getPath(glyphID, &glyphPath)) {
      auto glyphMatrix = Matrix::MakeScale(1.0f / maxScale, 1.0f / maxScale);
      glyphMatrix.postTranslate(position.x, position.y);
      glyphPath.transform(glyphMatrix);
      totalPath.addPath(glyphPath);
    } else {
      rejectedGlyphRun.glyphs.push_back(glyphID);
      rejectedGlyphRun.positions.push_back(position);
    }
    index++;
  }
  if (totalPath.isEmpty()) {
    rejectedGlyphRun = std::move(sourceGlyphRun);
    return;
  }

  auto rasterizeMatrix = state.matrix;
  rasterizeMatrix.postTranslate(-clipBounds.x(), -clipBounds.y());
  auto shape = Shape::MakeFrom(totalPath);
  shape = Shape::ApplyStroke(std::move(shape), stroke);
  shape = Shape::ApplyMatrix(std::move(shape), rasterizeMatrix);
  auto bounds = shape->getBounds();
  bounds.offset(clipBounds.x(), clipBounds.y());
  bounds.intersect(clipBounds);
  bounds.roundOut();
  auto width = static_cast<int>(bounds.width());
  auto height = static_cast<int>(bounds.height());

  auto rasterizer = PathRasterizer::Make(width, height, std::move(shape), true, true);
  auto image = Image::MakeFrom(std::move(rasterizer));
  if (image == nullptr) {
    rejectedGlyphRun = std::move(sourceGlyphRun);
    return;
  }
  auto newState = state;
  newState.matrix = Matrix::MakeTrans(clipBounds.x(), clipBounds.y());
  auto rect = Rect::MakeWH(image->width(), image->height());
  opsCompositor->fillImage(std::move(image), rect, {}, newState,
                           fill.makeWithMatrix(rasterizeMatrix));
}

void TextRender::transformedMaskDrawing(const GlyphRun& sourceGlyphRun, const MCState& state,
                                        const Fill& fill, const Stroke* stroke) const {
  AtlasCell atlasCell;
  auto nextFlushToken = context->drawingManager()->nextFlushToken();
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
      auto task = context->drawingBuffer()->make<TextAtlasUploadTask>(
          UniqueKey::Make(), std::move(source), textureProxies[atlasLocator.pageIndex()], offset);
      context->drawingManager()->addResourceTask(std::move(task));
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

void TextRender::drawGlyphAtlas(std::shared_ptr<TextureProxy> textureProxy, const Rect& rect,
                                const SamplingOptions& sampling, const MCState& state,
                                const Fill& fill, const Matrix& viewMatrix) const {
  DEBUG_ASSERT(textureProxy != nullptr);
  DEBUG_ASSERT(textureProxy->isAlphaOnly() || fill.shader == nullptr);
  opsCompositor->fillTextAtlas(std::move(textureProxy), rect, sampling, state, fill, viewMatrix);
}
}  // namespace tgfx
