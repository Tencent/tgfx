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
//#include "core/TextAtlas.h"
#include "core/utils/Caster.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
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
    return Rect::MakeEmpty();
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

struct Parameters {
  size_t imageIndex = 0;
  std::vector<tgfx::Matrix> matrices;
  std::vector<tgfx::Rect> rects;
};

void RenderContext::drawAtlas(const MCState& mcState, std::shared_ptr<Image> atlas,
                              const Matrix matrix[], const Rect tex[], const Color colors[],
                              size_t count, const SamplingOptions& sampling, const Fill& fill) {
  auto state = mcState;
  auto atlasRect = Rect::MakeWH(atlas->width(), atlas->height());
  for (size_t i = 0; i < count; ++i) {
    auto rect = tex[i];
    if (!rect.intersect(atlasRect)) {
      continue;
    }
    state.matrix = mcState.matrix * matrix[i];
    state.matrix.preTranslate(-rect.x(), -rect.y());
    auto glyphFill = fill;
    if (colors) {
      glyphFill.color = colors[i];
    }
    if (rect == atlasRect) {
      drawImage(atlas, sampling, state, glyphFill);
    } else {
      drawImageRect(atlas, rect, sampling, state, glyphFill);
    }
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
  if (maxScale <= 0.0f) {
    return;
  }

  if (auto compositor = getOpsCompositor()) {
    compositor->fillGlyphRuns(glyphRunList, state, fill, stroke);
  }

  // auto textAtlas = TextAtlas::Make(renderTarget->getContext(), glyphRunList, maxScale, stroke);
  // AtlasLocator locator;
  // Parameters parameters = {};
  //
  // for (auto& glyphRun : glyphRunList->glyphRuns()) {
  //   for (size_t i = 0; i < glyphRun.glyphs.size(); ++i) {
  //     auto glyph = glyphRun.glyphs[i];
  //     if (!textAtlas->getLocator(glyph, &locator)) {
  //       continue;
  //     }
  //     auto matrix = tgfx::Matrix::I();
  //     matrix.postTranslate(locator.glyphBounds.x(), locator.glyphBounds.y());
  //     matrix.postScale(1 / maxScale, 1 / maxScale);
  //     matrix.postTranslate(glyphRun.positions[i].x, glyphRun.positions[i].y);
  //     parameters.matrices.emplace_back(matrix);
  //     parameters.rects.emplace_back(locator.location);
  //     if (parameters.imageIndex != locator.imageIndex) {
  //       drawAtlas(state, textAtlas->getAtlasImage(parameters.imageIndex),
  //                 parameters.matrices.data(), parameters.rects.data(), nullptr,
  //                 parameters.matrices.size(), {}, fill);
  //       parameters = {};
  //       parameters.imageIndex = locator.imageIndex;
  //     }
  //   }
  // }
  // drawAtlas(state, textAtlas->getAtlasImage(parameters.imageIndex), parameters.matrices.data(),
  //           parameters.rects.data(), nullptr, parameters.matrices.size(), {}, fill);

  //  auto bounds = glyphRunList->getBounds(maxScale);
  //  if (stroke) {
  //    stroke->applyToBounds(&bounds);
  //  }
  //  bounds.scale(maxScale, maxScale);
  //  auto rasterizeMatrix = Matrix::MakeScale(maxScale);
  //  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  //  auto invert = Matrix::I();
  //  if (!rasterizeMatrix.invert(&invert)) {
  //    return;
  //  }
  //  auto newState = state;
  //  newState.matrix.preConcat(invert);
  //  auto width = static_cast<int>(ceilf(bounds.width()));
  //  auto height = static_cast<int>(ceilf(bounds.height()));
  //  auto rasterizer = Rasterizer::MakeFrom(width, height, std::move(glyphRunList), fill.antiAlias,
  //                                         rasterizeMatrix, stroke);
  //  auto image = Image::MakeFrom(std::move(rasterizer));
  //  if (image == nullptr) {
  //    return;
  //  }
  //  drawImage(std::move(image), {}, newState, fill.makeWithMatrix(rasterizeMatrix));
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
    auto offset = Point::Zero();
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
