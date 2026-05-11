/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "BackgroundHandler.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/BackgroundSnapshotMap.h"
#include "layers/BackgroundSource.h"
#include "layers/DrawArgs.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

namespace {

// A handler that does nothing for background-sourced styles. Used for intermediate artifacts and
// 3D / contour paths where background styles must not produce output.
class NoOpImpl : public BackgroundHandler {
 public:
  void drawBackgroundStyle(const DrawArgs& /*args*/, Canvas* /*canvas*/, Layer* /*layer*/,
                           float /*alpha*/, LayerStyle* /*style*/,
                           const LayerStyleSource* /*source*/) override {
  }
};

// Copy the slice of bgImage this layer consumes into a private surface, so the recorded picture
// references the copy rather than bgImage. bgImage IS bgSource->surface's cachedImage; holding a
// reference to it during capture would trigger Surface::aboutToDraw's copy-on-write path on every
// subsequent write to bgCanvas — a full RT reallocation plus blit that dominates GPU submit time.
// Returns nullptr on failure (no context, over-budget, or non-invertible matrix).
std::shared_ptr<Image> MakeDetachedBgCopy(Context* context, const std::shared_ptr<Image>& bgImage,
                                          const Matrix& bgPixelToLocal, const Rect& layerBounds,
                                          Point* offset, std::shared_ptr<ColorSpace> colorSpace) {
  Matrix localToBgPixel = Matrix::I();
  if (!bgPixelToLocal.invert(&localToBgPixel)) {
    return nullptr;
  }
  auto bgPixelBounds = layerBounds;
  localToBgPixel.mapRect(&bgPixelBounds);
  if (!bgPixelBounds.intersect(Rect::MakeWH(bgImage->width(), bgImage->height()))) {
    return nullptr;
  }
  bgPixelBounds.roundOut();
  auto width = FloatCeilToInt(bgPixelBounds.width());
  auto height = FloatCeilToInt(bgPixelBounds.height());
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto surface = Surface::Make(context, width, height, false, 1, false, 0, std::move(colorSpace));
  if (surface == nullptr) {
    return nullptr;
  }
  auto* canvas = surface->getCanvas();
  canvas->translate(-bgPixelBounds.left, -bgPixelBounds.top);
  canvas->drawImage(bgImage);
  if (offset != nullptr) {
    *offset = {bgPixelBounds.left, bgPixelBounds.top};
  }
  return surface->makeImageSnapshot();
}

// Intersects the sub background's rect with parentArgs.renderRects to produce a narrowed cull
// list for the offscreen's child draw pass. If parentArgs.renderRects is null/empty (no cull),
// falls back to the full bgRect.
void NarrowRenderRects(const DrawArgs& parentArgs, const Rect& bgRect,
                       std::vector<Rect>* outRects) {
  if (parentArgs.renderRects != nullptr && !parentArgs.renderRects->empty()) {
    for (const auto& r : *parentArgs.renderRects) {
      auto clipped = r;
      if (clipped.intersect(bgRect)) {
        outRects->push_back(clipped);
      }
    }
  } else {
    outRects->push_back(bgRect);
  }
}

// Builds the world-space rect and localToWorld matrix for a sub source nested under bgSource.
// Returns false if bgSource is missing.
bool ComputeSubGeometry(BackgroundSource* parentSource, const Rect& localBounds,
                        const Matrix& contentMatrix, Matrix* localToWorld, Rect* worldBounds) {
  if (parentSource == nullptr) {
    return false;
  }
  *localToWorld = parentSource->surfaceToWorldMatrix();
  localToWorld->preConcat(contentMatrix);
  *worldBounds = localToWorld->mapRect(localBounds);
  worldBounds->roundOut();
  return true;
}

}  // namespace

BackgroundHandler* BackgroundHandler::NoOp() {
  static NoOpImpl instance;
  return &instance;
}

void BackgroundHandler::DispatchOrSkip(const DrawArgs& args, Canvas* canvas, Layer* layer,
                                       float alpha, LayerStyle* style,
                                       const LayerStyleSource* source) {
  auto* handler = args.backgroundHandler ? args.backgroundHandler : BackgroundHandler::NoOp();
  handler->drawBackgroundStyle(args, canvas, layer, alpha, style, source);
}

int BackgroundHandler::lastCaptureChildIndex(const Layer* parent) const {
  return static_cast<int>(parent->children().size()) - 1;
}

int BackgroundCapturer::lastCaptureChildIndex(const Layer* parent) const {
  if (isForcedCapture()) {
    return static_cast<int>(parent->children().size()) - 1;
  }
  const auto& children = parent->children();
  for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
    if (children[static_cast<size_t>(i)]->hasBackgroundStyle()) {
      return i;
    }
  }
  return -1;
}

bool BackgroundCapturer::needsSurface(Layer* layer) const {
  return layer != nullptr && layer->hasDescendantBackgroundStyle();
}

std::unique_ptr<BackgroundCapturer> BackgroundCapturer::buildSubCapturer(
    const DrawArgs& parentArgs, std::shared_ptr<BackgroundSource> subSource) const {
  if (subSource == nullptr) {
    return nullptr;
  }
  auto subRect = subSource->getBackgroundRect();
  auto clone = std::make_unique<BackgroundCapturer>(snapshots, std::move(subSource));
  NarrowRenderRects(parentArgs, subRect, &clone->_renderRects);
  // Forced-capture state must propagate so pass-through subtrees still feed the parent bgSource.
  if (isForcedCapture()) {
    clone->beginForceDrawChildren();
  }
  return clone;
}

std::unique_ptr<BackgroundHandler> BackgroundCapturer::createSubHandler(
    Surface* surface, const DrawArgs& parentArgs, const Rect& localBounds,
    const Matrix& localToWorld, const Matrix& localToSurface) const {
  Matrix toWorld = Matrix::I();
  Rect worldBounds = Rect::MakeEmpty();
  if (!ComputeSubGeometry(bgSource.get(), localBounds, localToWorld, &toWorld, &worldBounds)) {
    DEBUG_ASSERT(false);
    return nullptr;
  }
  auto subSource = bgSource->createFromSurface(surface, worldBounds, toWorld, localToSurface);
  DEBUG_ASSERT(subSource != nullptr);
  return buildSubCapturer(parentArgs, std::move(subSource));
}

void BackgroundCapturer::drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer,
                                             float /*alpha*/, LayerStyle* style,
                                             const LayerStyleSource* source) {
  if (snapshots == nullptr || bgSource == nullptr || source == nullptr || canvas == nullptr) {
    return;
  }
  auto surfaceScale = bgSource->surfaceScale();
  auto contentScale = source->contentScale;
  auto localToWorld = bgSource->surfaceToWorldMatrix();
  localToWorld.preConcat(canvas->getMatrix());
  if (!FloatNearlyZero(surfaceScale) && surfaceScale != 1.0f) {
    contentScale /= surfaceScale;
  }
  if (FloatNearlyZero(contentScale)) {
    return;
  }
  auto layerBounds = layer->getBounds();
  auto bounds = layerBounds;
  bounds.scale(contentScale, contentScale);
  bounds.roundOut();
  // Use the runtime canvas chain (capture canvas matrix · bgSource->surfaceToWorldMatrix) so
  // capture and consume share the same frame of reference. Do NOT use getGlobalMatrix(): it walks
  // the static layer tree with per-step z-flattening and diverges from the runtime concat chain
  // when multiple non-preserve3D perspective matrices stack up.
  Matrix worldToLocal = Matrix::I();
  if (!localToWorld.invert(&worldToLocal)) {
    return;
  }
  auto bgImage = bgSource->getBackgroundImage();
  if (bgImage == nullptr) {
    return;
  }
  Matrix bgPixelToLocal = worldToLocal;
  bgPixelToLocal.preConcat(bgSource->backgroundMatrix());
  Point smallBgOffset = {};
  auto smallBgImage = MakeDetachedBgCopy(args.context, bgImage, bgPixelToLocal, layerBounds,
                                         &smallBgOffset, args.dstColorSpace);
  PictureRecorder recorder = {};
  auto* recording = recorder.beginRecording();
  recording->scale(contentScale, contentScale);
  recording->concat(worldToLocal);
  recording->concat(bgSource->backgroundMatrix());
  if (smallBgImage != nullptr) {
    recording->translate(smallBgOffset.x, smallBgOffset.y);
    recording->drawImage(std::move(smallBgImage));
  } else {
    // Fallback when detached copy is unavailable; original path may trigger COW.
    recording->drawImage(bgImage);
  }
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    return;
  }
  Point offset = {};
  auto image = ToImageWithOffset(std::move(picture), &offset, &bounds, args.dstColorSpace);
  if (image == nullptr) {
    return;
  }
  snapshots->emplace(BackgroundSnapshotKey{layer, style},
                     BackgroundSnapshotEntry{std::move(image), offset});
}

const LayerStyleSource* BackgroundCapturer::getCachedLayerStyleSource(Layer* layer) const {
  if (snapshots == nullptr) {
    return nullptr;
  }
  auto it = snapshots->layerStyleSources.find(layer);
  if (it == snapshots->layerStyleSources.end()) {
    return nullptr;
  }
  return it->second.get();
}

bool BackgroundCapturer::canCacheLayerStyleSource(Layer* /*layer*/) const {
  // When the bg source was down-sampled (extreme blur outset), capture and consume run at
  // different densities, so a cached source built in capture cannot be reused in consume.
  return snapshots != nullptr && (bgSource == nullptr || bgSource->surfaceScale() == 1.0f);
}

const LayerStyleSource* BackgroundCapturer::cacheLayerStyleSource(
    Layer* layer, std::unique_ptr<LayerStyleSource> source) {
  DEBUG_ASSERT(canCacheLayerStyleSource(layer));
  if (source == nullptr) {
    return nullptr;
  }
  // Wrap each picture-backed image as Rasterized so the first draw uploads a GPU texture and the
  // second pass reuses it instead of replaying the picture. makeRasterized() on an already-
  // rasterized image is a no-op, so wrapping content/contour independently still hits one cache.
  for (auto& group : source->groups) {
    if (group == nullptr) {
      continue;
    }
    if (group->content.image != nullptr) {
      group->content.image = group->content.image->makeRasterized();
    }
    if (group->contour.has_value() && group->contour->image != nullptr) {
      group->contour->image = group->contour->image->makeRasterized();
    }
  }
  auto* raw = source.get();
  snapshots->layerStyleSources.emplace(layer, std::move(source));
  return raw;
}

const LayerStyleSource* BackgroundConsumer::getCachedLayerStyleSource(Layer* layer) const {
  if (snapshots == nullptr) {
    return nullptr;
  }
  auto it = snapshots->layerStyleSources.find(layer);
  if (it == snapshots->layerStyleSources.end()) {
    return nullptr;
  }
  return it->second.get();
}

void BackgroundConsumer::drawBackgroundStyle(const DrawArgs& /*args*/, Canvas* canvas, Layer* layer,
                                             float alpha, LayerStyle* style,
                                             const LayerStyleSource* source) {
  if (snapshots == nullptr || source == nullptr || FloatNearlyZero(source->contentScale)) {
    return;
  }
  auto it = snapshots->find(BackgroundSnapshotKey{layer, style});
  if (it == snapshots->end()) {
    return;
  }
  const auto& snapshot = it->second;
  auto groupIndex = static_cast<int>(style->excludeChildEffects());
  auto* group = source->groups[groupIndex].get();
  if (group == nullptr) {
    return;
  }
  const auto& contentEntry = group->content;
  AutoCanvasRestore restoreCanvas(canvas);
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(contentEntry.offset.x, contentEntry.offset.y);
  canvas->concat(matrix);
  auto backgroundOffset = snapshot.offset - contentEntry.offset;
  style->drawWithExtraSource(canvas, contentEntry.image, source->contentScale, snapshot.image,
                             backgroundOffset, alpha);
}

void BackgroundCapturer::Run(Layer* captureRoot, const DrawArgs& baseArgs,
                             std::shared_ptr<BackgroundSource> bgSource,
                             BackgroundSnapshotMap* snapshots,
                             const std::vector<Rect>& renderRects) {
  DEBUG_ASSERT(captureRoot != nullptr);
  DEBUG_ASSERT(bgSource != nullptr);
  DEBUG_ASSERT(snapshots != nullptr);
  if (renderRects.empty()) {
    return;
  }

  auto* bgCanvas = bgSource->getCanvas();
  AutoCanvasRestore autoRestore(bgCanvas);
  BackgroundCapturer capturer(snapshots, std::move(bgSource));
  DrawArgs captureArgs = baseArgs;
  captureArgs.backgroundHandler = &capturer;
  captureArgs.renderRects = &renderRects;
  captureRoot->drawLayer(captureArgs, bgCanvas, 1.0f, BlendMode::SrcOver);
}

}  // namespace tgfx
