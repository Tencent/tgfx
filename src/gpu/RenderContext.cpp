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
#include "core/PathRef.h"
#include "core/PathTriangulator.h"
#include "core/Rasterizer.h"
#include "core/atlas/AtlasManager.h"
#include "core/images/TextureImage.h"
#include "core/utils/Caster.h"
#include "core/utils/MathExtra.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"
#include "gpu/tasks/TextAtlasUploadTask.h"

namespace tgfx {

static void computeAtlasKey(GlyphFace* glyphFace, GlyphID glyphID, const Stroke* stroke,
                            BytesKey& key) {
  float fontSize = 0.f;
  uint32_t typeFaceID = 0;
  float strokeWidth = 0.f;
  bool fauxBold = false;
  bool fauxItalic = false;
  if (stroke != nullptr) {
    strokeWidth = stroke->width;
  }
  Font font;
  if (glyphFace->asFont(&font)) {
    fontSize = font.getSize();
    typeFaceID = font.getTypeface()->uniqueID();
    fauxBold = font.isFauxBold();
    fauxItalic = font.isFauxItalic();
  }
  key.write(fontSize);
  key.write(typeFaceID);
  key.write(strokeWidth);
  key.write(fauxBold);
  key.write(fauxItalic);  //matrix
  key.write(glyphID);
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
  return clip.isEmpty() ? Rect::MakeEmpty() : clip.getBounds();
}

void RenderContext::drawFill(const MCState& state, const Fill& fill) {
  auto& clip = state.clip;
  if (clip.isEmpty() && clip.isInverseFillType()) {
    if (auto compositor = getOpsCompositor(fill.isOpaque())) {
      compositor->fillRect(renderTarget->bounds(), {}, fill.makeWithMatrix(state.matrix));
    }
  } else {
    auto shape = Shape::MakeFrom(clip);
    drawShape(std::move(shape), {}, fill.makeWithMatrix(state.matrix));
  }
}

void RenderContext::drawRect(const Rect& rect, const MCState& state, const Fill& fill) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillRect(rect, state, fill);
  }
}

void RenderContext::drawRRect(const RRect& rRect, const MCState& state, const Fill& fill) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillRRect(rRect, state, fill);
  }
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

void RenderContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                              const MCState& state, const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  auto rect = Rect::MakeWH(image->width(), image->height());
  return drawImageRect(std::move(image), rect, sampling, state, fill);
}

void RenderContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                  const SamplingOptions& sampling, const MCState& state,
                                  const Fill& fill) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(image->isAlphaOnly() || fill.shader == nullptr);
  auto compositor = getOpsCompositor();
  if (compositor == nullptr) {
    return;
  }
  auto samplingOptions = sampling;
  if (samplingOptions.mipmapMode != MipmapMode::None && !state.matrix.hasNonIdentityScale()) {
    // There is no scaling for the source image, so we can disable mipmaps to save memory.
    samplingOptions.mipmapMode = MipmapMode::None;
  }
  auto subsetImage = Caster::AsSubsetImage(image.get());
  if (subsetImage == nullptr) {
    compositor->fillImage(std::move(image), rect, samplingOptions, state, fill);
  } else {
    // Unwrap the subset image to maximize the merging of draw calls.
    auto imageRect = rect;
    auto imageState = state;
    auto& subset = subsetImage->bounds;
    imageRect.offset(subset.left, subset.top);
    imageState.matrix.preTranslate(-subset.left, -subset.top);
    auto offsetMatrix = Matrix::MakeTrans(subset.left, subset.top);
    compositor->fillImage(subsetImage->source, imageRect, samplingOptions, imageState,
                          fill.makeWithMatrix(offsetMatrix));
  }
}

void RenderContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const MCState& state, const Fill& fill, const Stroke* stroke) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  if (glyphRunList->hasColor()) {
    drawColorGlyphs(std::move(glyphRunList), state, fill);
    return;
  }
  auto maxScale = state.matrix.getMaxScale();
  if (maxScale <= 0.0f || renderTarget == nullptr) {
    return;
  }

  Context* context = renderTarget->getContext();
  if (context == nullptr) {
    return;
  }
  auto atlasManager = renderTarget->getContext()->atlasManager();
  if (atlasManager == nullptr) {
    return;
  }

  Glyph glyph;
  auto nextFlushToken = context->drawingManager()->nextFlushToken();
  PlotUseUpdater plotUseUpdater;
  auto hasScale = !FloatNearlyEqual(maxScale, 1.0f);
  for (auto& run : glyphRunList->glyphRuns()) {
    auto scaledGlyphFace = run.glyphFace;
    if (hasScale) {
      scaledGlyphFace = run.glyphFace->makeScaled(maxScale);
    }
    size_t index = 0;
    for (auto& glyphID : run.glyphs) {
      auto glyphPosition = run.positions[index++];
      std::vector<GlyphID> glyphs{glyphID};
      std::vector<Point> positions{glyphPosition};
      GlyphRun glyphRun(run.glyphFace, std::move(glyphs), std::move(positions));
      auto runList = std::make_shared<GlyphRunList>(std::move(glyphRun));
      auto bounds = runList->getBounds(maxScale);
      auto noStrokeBounds = bounds;
      if (stroke) {
        stroke->applyToBounds(&bounds);
      }
      bounds.scale(maxScale, maxScale);
      if (stroke) {
        noStrokeBounds.scale(maxScale, maxScale);
        auto positionOffsetX = (bounds.x() - noStrokeBounds.x()) / maxScale;
        auto positionOffsetY = (bounds.y() - noStrokeBounds.y()) / maxScale;
        glyphPosition.offset(positionOffsetX, positionOffsetY);
      }

      auto width = static_cast<int>(ceilf(bounds.width()));
      auto height = static_cast<int>(ceilf(bounds.height()));

      auto minDimension = std::min(width, height);
      auto maxDimension = std::max(width, height);
      if (minDimension <= 0 || maxDimension > 256) {
        continue;
      }

      BytesKey glyphKey;
      computeAtlasKey(scaledGlyphFace.get(), glyphID, stroke, glyphKey);
      auto maskFormat = scaledGlyphFace->hasColor() ? MaskFormat::RGBA : MaskFormat::A8;
      auto& textureProxies = atlasManager->getTextureProxies(maskFormat);
      auto& atlasImages = atlasManager->getImages(maskFormat);

      DEBUG_ASSERT(atlasImages.size() == textureProxies.size());

      AtlasLocator locator;
      if (!atlasManager->hasGlyph(maskFormat, glyphKey)) {
        glyph._key = glyphKey;
        glyph._maskFormat = maskFormat;
        glyph._glyphId = glyphID;
        glyph._width = width;
        glyph._height = height;

        atlasManager->addGlyphToAtlasWithoutFillImage(glyph, nextFlushToken, locator);
        auto rasterizeMatrix = Matrix::MakeScale(maxScale);
        rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());

        auto rasterizer = Rasterizer::MakeFrom(width, height, std::move(runList), fill.antiAlias,
                                               rasterizeMatrix, stroke);
        auto asyncDecoding = !(renderFlags & RenderFlags::DisableAsyncTask);
        auto source = ImageSource::MakeFrom(std::move(rasterizer), false, asyncDecoding);
        auto uniqueKey = UniqueKey::Make();
        auto offset = Point::Make(locator.getLocation().left, locator.getLocation().top);
        auto task = context->drawingBuffer()->makeNode<TextAtlasUploadTask>(
            uniqueKey, std::move(source), textureProxies[locator.pageIndex()], offset);
        context->drawingManager()->addResourceTask(std::move(task));

      } else {
        atlasManager->getGlyphLocator(maskFormat, glyphKey, locator);
      }

      atlasManager->setPlotUseToken(plotUseUpdater, locator.plotLocator(), maskFormat,
                                    nextFlushToken);

      auto image = atlasImages[locator.pageIndex()];
      if (image == nullptr) {
        continue;
      }

      auto glyphBounds = scaledGlyphFace->getBounds(glyphID);

      auto rect = locator.getLocation();
      auto newState = state;
      newState.matrix = Matrix::MakeTrans(glyphBounds.x(), glyphBounds.y());
      newState.matrix.postScale(1.f / maxScale, 1.f / maxScale);
      newState.matrix.postTranslate(glyphPosition.x, glyphPosition.y);
      newState.matrix.postConcat(state.matrix);
      newState.matrix.preTranslate(-rect.x(), -rect.y());

      drawImageRect(std::move(image), rect, {}, newState, fill);
    }
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
  drawImage(std::move(image), {}, drawState, fill.makeWithMatrix(viewMatrix));
}

void RenderContext::drawColorGlyphs(std::shared_ptr<GlyphRunList> glyphRunList,
                                    const MCState& state, const Fill& fill) {
  auto viewMatrix = state.matrix;
  auto scale = viewMatrix.getMaxScale();
  if (scale <= 0) {
    return;
  }
  viewMatrix.preScale(1.0f / scale, 1.0f / scale);
  for (auto& glyphRun : glyphRunList->glyphRuns()) {
    auto glyphFace = glyphRun.glyphFace;
    glyphFace = glyphFace->makeScaled(scale);
    DEBUG_ASSERT(glyphFace != nullptr);
    auto& glyphIDs = glyphRun.glyphs;
    auto glyphCount = glyphIDs.size();
    auto& positions = glyphRun.positions;
    auto glyphState = state;
    for (size_t i = 0; i < glyphCount; ++i) {
      const auto& glyphID = glyphIDs[i];
      const auto& position = positions[i];
      auto glyphImage = glyphFace->getImage(glyphID, &glyphState.matrix);
      if (glyphImage == nullptr) {
        continue;
      }
      glyphState.matrix.postTranslate(position.x * scale, position.y * scale);
      glyphState.matrix.postConcat(viewMatrix);
      auto rect = Rect::MakeWH(glyphImage->width(), glyphImage->height());
      drawImageRect(std::move(glyphImage), rect, {}, glyphState, fill);
    }
  }
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
    if (surface) {
      surface->contentChanged();
    }
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
    Fill fill = {Color::White(), BlendMode::Src, false};
    opsCompositor->fillImage(std::move(oldContent), renderTarget->bounds(), {}, MCState{}, fill);
  }
}

}  // namespace tgfx
