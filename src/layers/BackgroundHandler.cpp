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

// Returns the style-space visible region for a (layer, style) pair, or null when the capture
// pass could not determine one.
const Rect* GetVisibleStyle(BackgroundSnapshotMap* snapshots, Layer* layer, LayerStyle* style) {
  auto it = snapshots->styleVisibleBounds.find(BackgroundSnapshotKey{layer, style});
  return it != snapshots->styleVisibleBounds.end() ? &it->second : nullptr;
}

// Records the style's output in style space, clipped to the visible region. The picture carries
// no device transform, so replaying it on any style-space canvas lands the style exactly where
// it drew. A null visibleStyle skips the clip.
std::shared_ptr<Picture> RecordStyleOutput(LayerStyle* style, const LayerStyleInput& styleInput,
                                           float alpha, const Rect* visibleStyle) {
  PictureRecorder recorder = {};
  auto* recording = recorder.beginRecording();
  if (visibleStyle != nullptr) {
    recording->clipRect(*visibleStyle, false);
  }
  style->draw(recording, styleInput, alpha);
  return recorder.finishRecordingAsPicture();
}

// Rasterizes a style-space picture at device resolution and stores it in the frame's style
// output cache, so later passes composite one texture with SrcOver instead of re-running the
// style.
void CacheStyleOutput(BackgroundSnapshotMap* snapshots, Context* context, Layer* layer,
                      LayerStyle* style, const std::shared_ptr<Picture>& picture,
                      const Matrix& recordMatrix, const Rect& shapeRect,
                      std::shared_ptr<ColorSpace> dstColorSpace) {
  PictureRecorder deviceRecorder = {};
  auto* deviceRecording = deviceRecorder.beginRecording();
  deviceRecording->concat(recordMatrix);
  deviceRecording->drawPicture(picture);
  auto devicePicture = deviceRecorder.finishRecordingAsPicture();
  if (devicePicture == nullptr) {
    return;
  }
  auto deviceShape = recordMatrix.mapRect(shapeRect);
  deviceShape.roundOut();
  if (deviceShape.isEmpty()) {
    return;
  }
  // The visible-region bound normally keeps the cached texture at on-screen size, but it is
  // unavailable when the background surface is downsampled (the capture density no longer matches
  // the consumer's style space) and shapeRect then spans the whole content. Under zoom that is
  // orders of magnitude larger than the render target, so a texture past the GPU limit would fail
  // to allocate, be dropped silently and take the style down with it. Draw those directly instead:
  // the direct path rasterizes only the part each pass covers, which always fits in one target.
  // Clipping deviceShape to the render target is not an option here: the device space of a tile's
  // canvas is not the render target's space, and Rect::intersect leaves the rect unchanged when
  // the two do not overlap, which is exactly the far-out zoom case.
  if (context != nullptr && context->gpu() != nullptr) {
    auto limit = static_cast<float>(context->gpu()->limits()->maxTextureDimension2D);
    if (deviceShape.width() > limit || deviceShape.height() > limit) {
      return;
    }
  }
  // A texture that fits both dimensions can still be enormous (e.g. 16000x16000 RGBA is ~1GB) and
  // this cache is rebuilt every frame, so cap the total area as well.
  constexpr double MaxAreaFactor = 4.0;
  auto targetArea = static_cast<double>(snapshots->renderTargetWidth) *
                    static_cast<double>(snapshots->renderTargetHeight);
  auto areaBudget = targetArea * MaxAreaFactor;
  if (areaBudget > 0.0 &&
      static_cast<double>(deviceShape.width()) * static_cast<double>(deviceShape.height()) >
          areaBudget) {
    return;
  }
  Point imageOffset = {};
  auto image =
      ToImageWithOffset(std::move(devicePicture), &imageOffset, &deviceShape, dstColorSpace);
  Matrix drawMatrix = Matrix::I();
  if (image == nullptr || !recordMatrix.invert(&drawMatrix)) {
    return;
  }
  drawMatrix.preTranslate(imageOffset.x, imageOffset.y);
  snapshots->styleOutputs[BackgroundSnapshotKey{layer, style}] = {image->makeRasterized(),
                                                                  drawMatrix};
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
  // When the layer subtree is being recorded through the offscreen content path, the canvas
  // matrix starts at the layer-local origin and no longer carries the layer's world placement;
  // use the layer-local-to-capture-canvas transform published by that path instead.
  if (_captureWorldMatrix.has_value()) {
    localToWorld.preConcat(*_captureWorldMatrix);
  } else {
    localToWorld.preConcat(canvas->getMatrix());
  }
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
  // Precompute the visible region in style space for the consumer. The style space is defined
  // by the style's excludeChildEffects bucket, shared by every pass in the frame, so one rect
  // per (layer, style) pair bounds the recorded style output for all of them. Use the frame's
  // on-screen rects rather than args.renderRects: the latter is widened by maxBackgroundOutset so
  // the capture pass also paints the blur sampling margin, and a style's own output is never
  // visible outside the on-screen rects. Bounding to the widened rects would size the cached
  // texture by that margin, which under zoom grows far past the render target.
  auto* visibleGroup = source->groups[static_cast<int>(style->excludeChildEffects())].get();
  // The capture-side content offset is rasterized at capture density, which no longer matches
  // the consumer's style space once the background surface is downsampled, so the consumer
  // falls back to the unclipped path (visibleStyle == nullptr).
  // Only frames that share the style output read this bound, so skip it when the frame renders a
  // single pass or its render rects are too scattered to share: GetVisibleStyle is then never
  // called. Sharing also requires an opaque background, which is a DisplayList decision this pass
  // cannot see.
  if (snapshots->multiPass && snapshots->styleShareCompact && surfaceScale == 1.0f &&
      !snapshots->visibleRects.empty() && visibleGroup != nullptr) {
    Rect visibleWorld = Rect::MakeEmpty();
    for (const auto& renderRect : snapshots->visibleRects) {
      visibleWorld.join(renderRect);
    }
    auto visibleLocal = worldToLocal.mapRect(visibleWorld);
    auto& contentOffset = visibleGroup->content.offset;
    snapshots->styleVisibleBounds[BackgroundSnapshotKey{layer, style}] =
        Rect::MakeXYWH(visibleLocal.left * contentScale - contentOffset.x,
                       visibleLocal.top * contentScale - contentOffset.y,
                       visibleLocal.width() * contentScale, visibleLocal.height() * contentScale);
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
  snapshots->snapshots[BackgroundSnapshotKey{layer, style}].push_back(
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

void BackgroundConsumer::drawBackgroundStyle(const DrawArgs& args, Canvas* canvas, Layer* layer,
                                             float alpha, LayerStyle* style,
                                             const LayerStyleSource* source) {
  if (source == nullptr || FloatNearlyZero(source->contentScale)) {
    return;
  }
  auto groupIndex = static_cast<int>(style->excludeChildEffects());
  auto* group = source->groups[groupIndex].get();
  if (group == nullptr) {
    return;
  }
  const auto& contentEntry = group->content;
  std::shared_ptr<Image> bgImage = nullptr;
  Point bgOffset = {};
  if (snapshots != nullptr) {
    BackgroundSnapshotKey key{layer, style};
    auto it = snapshots->snapshots.find(key);
    if (it == snapshots->snapshots.end()) {
      // Surface path: the map is authoritative, so a miss is a capture-side coverage bug.
      // Silently skip rather than masking the bug with on-the-fly synthesis.
      return;
    }
    auto& cursor = readCursors[key];
    DEBUG_ASSERT(cursor < it->second.size() && "capture/consume push-pop count mismatch");
    if (cursor >= it->second.size()) {
      return;
    }
    auto& entry = it->second[cursor++];
    bgImage = entry.image;
    bgOffset = entry.offset;
  } else {
    // Picture-canvas path: capture was skipped because there is no GPU context. Synthesize the
    // backdrop on the fly by walking ancestors and prior siblings via PictureRecorder.
    bgImage = layer->synthesizeBackgroundImage(args, source->contentScale, &bgOffset);
    if (bgImage == nullptr) {
      return;
    }
  }
  AutoCanvasRestore restoreCanvas(canvas);
  auto matrix = Matrix::MakeScale(1.f / source->contentScale, 1.f / source->contentScale);
  matrix.preTranslate(contentEntry.offset.x, contentEntry.offset.y);
  canvas->concat(matrix);

  auto backgroundOffset = bgOffset - contentEntry.offset;
  LayerStyleInput styleInput = {};
  styleInput.content = contentEntry.image;
  styleInput.contentOffset = contentEntry.offset;
  styleInput.contentScale = source->contentScale;
  auto sourceFlags = style->extraSourceType();
  styleInput.extraSources.push_back(
      std::make_shared<StyleInputSource>(std::move(bgImage), backgroundOffset));
  if ((sourceFlags & static_cast<uint32_t>(LayerStyleExtraSourceType::Contour)) != 0) {
    std::shared_ptr<Image> contourImage = nullptr;
    Point contourOffset = {};
    if (group->contour.has_value()) {
      contourImage = group->contour->image;
      contourOffset = group->contour->offset - contentEntry.offset;
    }
    styleInput.extraSources.push_back(std::make_shared<ContourInputSource>(
        std::move(contourImage), contourOffset, source->contentShape));
  }

  // On the first pass that needs it, rasterize the style's output once and cache it; every
  // pass — this one included — composites the same texture back with SrcOver. The image carries
  // the style's own mask as its alpha, so compositing it once reproduces the direct draw for
  // opaque backdrops. Passes within a frame normally differ by an integer translation, which
  // keeps the blit 1:1; a projective recordMatrix instead resamples the cached texture on blit,
  // which is accepted: the cached output still covers every visible pixel, and a non-degenerate
  // projective map of the visible rect stays inside its corner hull, so deviceShape remains a
  // valid bound.
  auto recordMatrix = canvas->getMatrix();
  if (snapshots != nullptr && shareStyleOutput) {
    BackgroundSnapshotKey key{layer, style};
    auto output = snapshots->styleOutputs.find(key);
    if (output == snapshots->styleOutputs.end()) {
      // Capture pushes one entry per dispatch, so more than one entry for this key means the style
      // was dispatched repeatedly in this frame, which a 3D subtree does by splitting the layer
      // across BSP fragments. Each dispatch sampled a different backdrop, and one texture cannot
      // serve them all, so let the null placeholder below force the direct path.
      auto entryIt = snapshots->snapshots.find(key);
      auto singleDispatch = entryIt == snapshots->snapshots.end() || entryIt->second.size() <= 1;
      if (singleDispatch) {
        auto visibleStyle = GetVisibleStyle(snapshots, layer, style);
        auto picture = RecordStyleOutput(style, styleInput, alpha, visibleStyle);
        if (picture != nullptr) {
          // Bound the cached texture to the visible region, so small dirty rects do not allocate
          // full-content textures.
          auto shapeRect = Rect::MakeWH(static_cast<float>(contentEntry.image->width()),
                                        static_cast<float>(contentEntry.image->height()));
          if (visibleStyle == nullptr || shapeRect.intersect(*visibleStyle)) {
            if (!shapeRect.isEmpty()) {
              CacheStyleOutput(snapshots, args.context, layer, style, picture, recordMatrix,
                               shapeRect, args.dstColorSpace);
            }
          }
        }
      }
      output = snapshots->styleOutputs.find(key);
      if (output == snapshots->styleOutputs.end()) {
        // Nothing was cached: no picture was recorded, the visible region does not meet the
        // content, the device shape is empty, or the texture was rejected as too large. Remember
        // the decision with a null entry so later passes draw the style directly instead of
        // re-recording it only to discard it again.
        snapshots->styleOutputs[key] = BackgroundSnapshotMap::StyleOutput();
        output = snapshots->styleOutputs.find(key);
      }
    }
    if (output != snapshots->styleOutputs.end() && output->second.image != nullptr) {
      AutoCanvasRestore restoreBlit(canvas);
      canvas->concat(output->second.drawMatrix);
      Paint paint = {};
      paint.setAntiAlias(false);
      paint.setBlendMode(BlendMode::SrcOver);
      canvas->drawImage(output->second.image, 0.0f, 0.0f, &paint);
      return;
    }
  }

  style->draw(canvas, styleInput, alpha);
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
  BackgroundCapturer capturer(snapshots, std::move(bgSource));
  AutoCanvasRestore autoRestore(bgCanvas);
  DrawArgs captureArgs = baseArgs;
  captureArgs.backgroundHandler = &capturer;
  captureArgs.renderRects = &renderRects;
  captureRoot->drawLayer(captureArgs, bgCanvas, 1.0f, BlendMode::SrcOver);
}

}  // namespace tgfx
