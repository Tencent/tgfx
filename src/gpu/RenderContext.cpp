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
#include "core/utils/Caster.h"
#include "gpu/DrawingManager.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
RenderContext::RenderContext(std::shared_ptr<RenderTargetProxy> renderTarget, uint32_t renderFlags,
                             Surface* surface)
    : renderTarget(std::move(renderTarget)), renderFlags(renderFlags), surface(surface) {
}

Rect RenderContext::getClipBounds(const Path& clip) {
  if (clip.isInverseFillType()) {
    return renderTarget->bounds();
  }
  return clip.isEmpty() ? Rect::MakeEmpty() : clip.getBounds();
}

void RenderContext::drawStyle(const MCState& state, const FillStyle& style) {
  auto& clip = state.clip;
  auto discardContent =
      clip.isInverseFillType() && clip.isEmpty() && style.hasOnlyColor() && style.isOpaque();
  if (auto compositor = getOpsCompositor(discardContent)) {
    compositor->fillRect(renderTarget->bounds(), MCState{clip}, style.makeWithMatrix(state.matrix));
  }
}

void RenderContext::drawRect(const Rect& rect, const MCState& state, const FillStyle& style) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillRect(rect, state, style);
  }
}

void RenderContext::drawRRect(const RRect& rRect, const MCState& state, const FillStyle& style) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillRRect(rRect, state, style);
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
                              const FillStyle& style) {
  if (auto compositor = getOpsCompositor()) {
    compositor->fillShape(std::move(shape), state, style);
  }
}

void RenderContext::drawImage(std::shared_ptr<Image> image, const SamplingOptions& sampling,
                              const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(image != nullptr);
  auto rect = Rect::MakeWH(image->width(), image->height());
  return drawImageRect(std::move(image), rect, sampling, state, style);
}

void RenderContext::drawImageRect(std::shared_ptr<Image> image, const Rect& rect,
                                  const SamplingOptions& sampling, const MCState& state,
                                  const FillStyle& style) {
  DEBUG_ASSERT(image != nullptr);
  DEBUG_ASSERT(image->isAlphaOnly() || style.shader == nullptr);
  auto imageRect = rect;
  auto imageState = state;
  auto imageStyle = style;
  auto subsetImage = Caster::AsSubsetImage(image.get());
  if (subsetImage != nullptr) {
    // Unwrap the subset image to maximize the merging of draw calls.
    auto& subset = subsetImage->bounds;
    imageRect.offset(subset.left, subset.top);
    imageState.matrix.preTranslate(-subset.left, -subset.top);
    auto offsetMatrix = Matrix::MakeTrans(subset.left, subset.top);
    imageStyle = style.makeWithMatrix(offsetMatrix);
    image = subsetImage->source;
  }
  auto options = sampling;
  if (options.mipmapMode != MipmapMode::None && !imageState.matrix.hasNonIdentityScale()) {
    // There is no scaling for the source image, so we can disable mipmaps to save memory.
    options.mipmapMode = MipmapMode::None;
  }
  if (auto compositor = getOpsCompositor()) {
    compositor->fillImage(std::move(image), imageRect, options, imageState, imageStyle);
  }
}

AAType RenderContext::getAAType(const FillStyle& style) const {
  if (renderTarget->sampleCount() > 1) {
    return AAType::MSAA;
  }
  if (style.antiAlias) {
    return AAType::Coverage;
  }
  return AAType::None;
}

void RenderContext::drawGlyphRunList(std::shared_ptr<GlyphRunList> glyphRunList,
                                     const Stroke* stroke, const MCState& state,
                                     const FillStyle& style) {
  DEBUG_ASSERT(glyphRunList != nullptr);
  if (glyphRunList->hasColor()) {
    drawColorGlyphs(std::move(glyphRunList), state, style);
    return;
  }
  auto maxScale = state.matrix.getMaxScale();
  if (maxScale <= 0.0f) {
    return;
  }
  auto bounds = glyphRunList->getBounds(maxScale);
  if (stroke) {
    stroke->applyToBounds(&bounds);
  }
  bounds.scale(maxScale, maxScale);
  auto rasterizeMatrix = Matrix::MakeScale(maxScale);
  rasterizeMatrix.postTranslate(-bounds.x(), -bounds.y());
  auto invert = Matrix::I();
  if (!rasterizeMatrix.invert(&invert)) {
    return;
  }
  auto newState = state;
  newState.matrix.preConcat(invert);
  auto width = static_cast<int>(ceilf(bounds.width()));
  auto height = static_cast<int>(ceilf(bounds.height()));
  auto aaType = getAAType(style);
  auto rasterizer = Rasterizer::MakeFrom(width, height, std::move(glyphRunList),
                                         aaType == AAType::Coverage, rasterizeMatrix, stroke);
  auto image = Image::MakeFrom(std::move(rasterizer));
  if (image == nullptr) {
    return;
  }
  drawImage(std::move(image), {}, newState, style.makeWithMatrix(rasterizeMatrix));
}

void RenderContext::drawPicture(std::shared_ptr<Picture> picture, const MCState& state) {
  DEBUG_ASSERT(picture != nullptr);
  picture->playback(this, state);
}

void RenderContext::drawLayer(std::shared_ptr<Picture> picture, std::shared_ptr<ImageFilter> filter,
                              const MCState& state, const FillStyle& style) {
  DEBUG_ASSERT(style.shader == nullptr);
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
  drawImage(std::move(image), {}, drawState, style.makeWithMatrix(viewMatrix));
}

void RenderContext::drawColorGlyphs(std::shared_ptr<GlyphRunList> glyphRunList,
                                    const MCState& state, const FillStyle& style) {
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
      drawImageRect(std::move(glyphImage), rect, {}, glyphState, style);
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
    FillStyle fillStyle = {Color::White(), BlendMode::Src};
    fillStyle.antiAlias = false;
    opsCompositor->fillImage(std::move(oldContent), renderTarget->bounds(), {}, MCState{},
                             fillStyle);
  }
}

}  // namespace tgfx
