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
#include "core/atlas/AtlasTypes.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/tasks/TextAtlasUploadTask.h"

namespace tgfx {
static void computeAtlasKey(const GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                            BytesKey& key) {
  key.write(glyphFace->getScale());
  key.write(glyphFace->getUniqueID());
  int packedID = glyphID;
  auto shift = sizeof(glyphID) * 8;
  Font font;
  if (glyphFace->asFont(&font)) {
    auto bold = static_cast<int>(font.isFauxBold());
    packedID |= (bold << shift);
  } else {
    packedID |= (0b1 << (shift + 1));
  }
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

static float findMaxGlyphDimension(const GlyphFace* glyphFace, const std::vector<GlyphID>& glyphIDs,
                                   const Stroke* stroke) {
  float maxDimension = 0.f;
  for (auto& glyphID : glyphIDs) {
    auto bounds = glyphFace->getBounds(glyphID);
    if (bounds.isEmpty()) {
      continue;
    }
    if (stroke != nullptr) {
      stroke->applyToBounds(&bounds);
    }
    maxDimension = std::max(maxDimension, std::max(bounds.width(), bounds.height()));
  }
  return maxDimension;
}

static std::shared_ptr<ImageCodec> getGlyphCodec(std::shared_ptr<GlyphFace> glyphFace,
                                                 GlyphID glyphID, const Stroke* stroke,
                                                 Matrix* matrix) {
  auto glyphCodec = glyphFace->getImage(glyphID, stroke, matrix);
  if (glyphCodec) {
    return glyphCodec;
  }
  auto shape = Shape::MakeFrom(std::move(glyphFace), glyphID);
  if (shape == nullptr) {
    return nullptr;
  }
  auto bounds = shape->getBounds();
  if (bounds.isEmpty()) {
    return nullptr;
  }
  if (stroke != nullptr) {
    stroke->applyToBounds(&bounds);
    shape = Shape::ApplyStroke(std::move(shape), stroke);
  }
  shape = Shape::ApplyMatrix(std::move(shape), Matrix::MakeTrans(-bounds.x(), -bounds.y()));
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  glyphCodec = PathRasterizer::Make(width, height, std::move(shape), true, true);
  matrix->setTranslate(bounds.x(), bounds.y());
  return glyphCodec;
}

static MaskFormat getMaskFormat(GlyphFace* glyphFace) {
  if (!glyphFace->hasColor()) {
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
  if (context == nullptr || opsCompositor == nullptr || glyphRunList == nullptr) {
    return nullptr;
  }
  if (context->atlasManager() == nullptr) {
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
    rejectedGlyphRun.glyphFace = run.glyphFace;
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

void TextRender::directMaskDrawing(const GlyphRun& glyphRun, const MCState& state, const Fill& fill,
                                   const Stroke* stroke, GlyphRun& rejectedGlyphRun) const {
  AtlasCell glyph;
  auto nextFlushToken = context->drawingManager()->nextFlushToken();
  PlotUseUpdater plotUseUpdater;

  auto maxScale = state.matrix.getMaxScale();
  auto hasScale = !FloatNearlyEqual(maxScale, 1.0f);
  auto scaledGlyphFace = glyphRun.glyphFace;
  if (hasScale) {
    scaledGlyphFace = glyphRun.glyphFace->makeScaled(maxScale);
  }
  if (scaledGlyphFace == nullptr) {
    return;
  }
  std::unique_ptr<Stroke> scaledStroke;
  if (stroke) {
    scaledStroke =
        std::make_unique<Stroke>(stroke->width * maxScale, stroke->cap, stroke->join, maxScale);
  }
  size_t index = 0;
  for (auto& glyphID : glyphRun.glyphs) {
    auto glyphPosition = glyphRun.positions[index++];
    auto bounds = scaledGlyphFace->getBounds(glyphID);
    if (bounds.isEmpty()) {
      continue;
    }
    if (scaledStroke) {
      scaledStroke->applyToBounds(&bounds);
    }
    auto maxDimension = static_cast<int>(ceilf(std::max(bounds.width(), bounds.height())));
    if (maxDimension >= Atlas::kMaxCellSize) {
      rejectedGlyphRun.glyphs.push_back(glyphID);
      rejectedGlyphRun.positions.push_back(glyphPosition);
      continue;
    }
    BytesKey glyphKey;
    computeAtlasKey(scaledGlyphFace.get(), glyphID, stroke, glyphKey);

    auto maskFormat = getMaskFormat(scaledGlyphFace.get());
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);

    auto glyphState = state;
    AtlasCellLocator glyphLocator;
    auto& atlasLocator = glyphLocator.atlasLocator;
    if (atlasManager->getCellLocator(maskFormat, glyphKey, glyphLocator)) {
      glyphState.matrix = glyphLocator.matrix;
    } else {
      auto glyphCodec =
          getGlyphCodec(scaledGlyphFace, glyphID, scaledStroke.get(), &glyphState.matrix);
      if (glyphCodec == nullptr) {
        continue;
      }

      glyph._key = std::move(glyphKey);
      glyph._maskFormat = maskFormat;
      glyph._id = glyphID;
      glyph._width = static_cast<uint16_t>(glyphCodec->width());
      glyph._height = static_cast<uint16_t>(glyphCodec->height());
      glyph._matrix = glyphState.matrix;

      atlasManager->addCellToAtlas(glyph, nextFlushToken, atlasLocator);

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

void TextRender::pathDrawing(const GlyphRun& glyphRun, const MCState& state, const Fill& fill,
                             const Stroke* stroke, GlyphRun& rejectedGlyphRun) const {
  if (!glyphRun.glyphFace->hasOutlines()) {
    rejectedGlyphRun = glyphRun;
  }

  auto maxScale = state.matrix.getMaxScale();
  Path totalPath = {};
  auto glyphFace = glyphRun.glyphFace;
  if (!FloatNearlyEqual(maxScale, 1.0f)) {
    glyphFace = glyphFace->makeScaled(maxScale);
    DEBUG_ASSERT(glyphFace != nullptr);
  }
  size_t index = 0;
  auto& positions = glyphRun.positions;
  for (auto& glyphID : glyphRun.glyphs) {
    Path glyphPath = {};
    auto& position = positions[index];
    if (glyphFace->getPath(glyphID, &glyphPath)) {
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
    rejectedGlyphRun = glyphRun;
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
    return;
  }

  auto newState = state;
  newState.matrix = Matrix::MakeTrans(clipBounds.x(), clipBounds.y());
  auto rect = Rect::MakeWH(image->width(), image->height());
  opsCompositor->fillImage(std::move(image), rect, {}, newState,
                           fill.makeWithMatrix(rasterizeMatrix));
}

void TextRender::transformedMaskDrawing(const GlyphRun& glyphRun, const MCState& state,
                                        const Fill& fill, const Stroke* stroke) const {
  AtlasCell atlasCell;
  auto nextFlushToken = context->drawingManager()->nextFlushToken();
  PlotUseUpdater plotUseUpdater;

  auto maxScale = state.matrix.getMaxScale();
  auto hasScale = !FloatNearlyEqual(maxScale, 1.0f);
  auto scaledGlyphFace = glyphRun.glyphFace;
  if (hasScale) {
    scaledGlyphFace = glyphRun.glyphFace->makeScaled(maxScale);
  }

  std::unique_ptr<Stroke> scaledStroke = nullptr;
  if (stroke) {
    scaledStroke =
        std::make_unique<Stroke>(stroke->width * maxScale, stroke->cap, stroke->join, maxScale);
  }
  static constexpr float kMaxAtlasDimension = Atlas::kMaxCellSize - 2.f;
  auto cellScale = 1.f;
  auto maxDimension =
      findMaxGlyphDimension(scaledGlyphFace.get(), glyphRun.glyphs, scaledStroke.get());
  while (maxDimension > kMaxAtlasDimension) {
    auto reductionFactor = kMaxAtlasDimension / maxDimension;
    scaledGlyphFace = scaledGlyphFace->makeScaled(reductionFactor);

    if (scaledStroke) {
      scaledStroke->width *= reductionFactor;
      scaledStroke->miterLimit *= reductionFactor;
    }
    maxDimension =
        findMaxGlyphDimension(scaledGlyphFace.get(), glyphRun.glyphs, scaledStroke.get());
    cellScale *= reductionFactor;
  }

  size_t index = 0;
  for (auto& glyphID : glyphRun.glyphs) {
    auto glyphPosition = glyphRun.positions[index++];
    auto bounds = scaledGlyphFace->getBounds(glyphID);
    if (bounds.isEmpty()) {
      continue;
    }
    if (scaledStroke) {
      scaledStroke->applyToBounds(&bounds);
    }

    BytesKey glyphKey;
    computeAtlasKey(scaledGlyphFace.get(), glyphID, stroke, glyphKey);
    auto maskFormat = getMaskFormat(scaledGlyphFace.get());
    auto& textureProxies = atlasManager->getTextureProxies(maskFormat);

    auto glyphState = state;
    AtlasCellLocator glyphLocator;
    auto& atlasLocator = glyphLocator.atlasLocator;
    if (atlasManager->getCellLocator(maskFormat, glyphKey, glyphLocator)) {
      glyphState.matrix = glyphLocator.matrix;
    } else {
      auto glyphCodec =
          getGlyphCodec(scaledGlyphFace, glyphID, scaledStroke.get(), &glyphState.matrix);
      if (glyphCodec == nullptr) {
        continue;
      }

      atlasCell._key = std::move(glyphKey);
      atlasCell._maskFormat = maskFormat;
      atlasCell._id = glyphID;
      atlasCell._width = static_cast<uint16_t>(glyphCodec->width());
      atlasCell._height = static_cast<uint16_t>(glyphCodec->height());
      atlasCell._matrix = glyphState.matrix;

      atlasManager->addCellToAtlas(atlasCell, nextFlushToken, atlasLocator);

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
