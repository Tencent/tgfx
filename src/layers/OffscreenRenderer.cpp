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

#include "layers/OffscreenRenderer.h"
#include "layers/BackgroundHandler.h"
#include "layers/BackgroundSource.h"
#include "layers/LayerStyleSource.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

namespace {

// ─── Shared helpers ───────────────────────────────────────────────────────────────────────────

std::shared_ptr<Image> SnapshotOf(Canvas* canvas) {
  auto surface = canvas->getSurface();
  if (surface == nullptr) {
    return nullptr;
  }
  return surface->makeImageSnapshot();
}

// Pre-paints the parent backdrop into a pass-through carrier so the subtree draws on top.
void SeedBackdrop(Canvas* canvas, const std::shared_ptr<Image>& backdrop, const Rect& surfaceRect) {
  canvas->save();
  canvas->setMatrix(Matrix::MakeTrans(-surfaceRect.x(), -surfaceRect.y()));
  canvas->drawImage(backdrop);
  canvas->restore();
}

std::shared_ptr<Image> ApplyImageFilterIfNeeded(std::shared_ptr<Image> image,
                                                const std::shared_ptr<ImageFilter>& imageFilter,
                                                const std::optional<Rect>& clipBounds,
                                                Matrix* imageMatrix) {
  if (image == nullptr || imageFilter == nullptr) {
    return image;
  }
  std::optional<Rect> filterClipBounds = std::nullopt;
  if (clipBounds.has_value()) {
    Matrix invertMatrix = Matrix::I();
    imageMatrix->invert(&invertMatrix);
    filterClipBounds = invertMatrix.mapRect(*clipBounds);
    filterClipBounds->roundOut();
  }
  Point filterOffset = {};
  image = image->makeWithFilter(imageFilter, &filterOffset,
                                filterClipBounds.has_value() ? &*filterClipBounds : nullptr);
  imageMatrix->preTranslate(filterOffset.x, filterOffset.y);
  return image;
}

// Derives a child handler that samples a sub BackgroundSource built off the given Surface or
// Recorder. Returns nullptr when the current handler has no source (BackgroundConsumer / NoOp),
// when the parent can't form a sub (e.g. out-of-bounds), or when the handler refuses to clone.
// `surface` and `recorder` are mutually exclusive (exactly one must be non-null).
//
// `contentMatrix` maps layer-local coords to the parent source's *surface pixel* space (that is
// the matrix installed on the capture canvas by BackgroundCapturer::Run or an outer
// OffscreenRenderer, not `local → world`). Composing with surfaceToWorldMatrix() therefore
// yields a correct `local → world` matrix even when the parent source is itself a sub whose
// image pixel grid differs from its surface pixel grid.
std::unique_ptr<BackgroundHandler> DeriveSubHandler(const DrawArgs& args, Surface* surface,
                                                    PictureRecorder* recorder,
                                                    const Rect& localBounds,
                                                    const Matrix& contentMatrix,
                                                    const Matrix& localToSurface) {
  if (args.background.handler == nullptr) {
    return nullptr;
  }
  auto* parentSource = args.background.handler->source();
  if (parentSource == nullptr) {
    return nullptr;
  }
  auto localToWorld = parentSource->surfaceToWorldMatrix();
  localToWorld.preConcat(contentMatrix);
  auto worldBounds = localToWorld.mapRect(localBounds);
  worldBounds.roundOut();

  std::shared_ptr<BackgroundSource> subSource;
  if (surface != nullptr) {
    subSource = parentSource->createSubSurface(surface, worldBounds, localToWorld, localToSurface);
  } else if (recorder != nullptr) {
    subSource = parentSource->createSubPicture(recorder, worldBounds, localToWorld, localToSurface);
  }
  if (subSource == nullptr) {
    return nullptr;
  }
  return args.background.handler->cloneWithSource(std::move(subSource));
}

}  // namespace

// ─── Entry points ─────────────────────────────────────────────────────────────────────────────

OffscreenResult OffscreenRenderer::RenderContent(Layer* layer, const DrawArgs& args,
                                                 const Matrix& contentMatrix,
                                                 const std::optional<Rect>& clipBounds) {
  auto inputBounds = layer->computeContentBounds(clipBounds, args.excludeEffects);
  if (!inputBounds.has_value() || inputBounds->isEmpty()) {
    return {};
  }

  // density: local → surface-pixel. With an image filter, force an isotropic scale so blur /
  // drop-shadow etc. operate in pixel space; without a filter keep the full contentMatrix so
  // rotation/skew fidelity is preserved.
  auto imageFilter =
      args.excludeEffects ? nullptr : layer->getImageFilter(contentMatrix.getMaxScale());
  Matrix density = imageFilter
                       ? Matrix::MakeScale(contentMatrix.getMaxScale(), contentMatrix.getMaxScale())
                       : contentMatrix;
  auto imageClip = density.mapRect(*inputBounds);
  imageClip.roundOut();

  // A Surface backing is only needed when a descendant Background-sourced style will read back
  // through this subtree. Otherwise a PictureRecorder is cheaper.
  bool wantsSubBackground = args.background.handler != nullptr &&
                            args.background.handler->source() != nullptr &&
                            layer->hasDescendantBackgroundStyle();
  if (wantsSubBackground && args.context != nullptr) {
    auto surface =
        Surface::Make(args.context, static_cast<int>(imageClip.width()),
                      static_cast<int>(imageClip.height()), false, 1, false, 0, args.dstColorSpace);
    if (surface != nullptr) {
      return RenderContentOnSurface(layer, args, std::move(surface), density, imageClip,
                                    imageFilter, clipBounds, *inputBounds, contentMatrix,
                                    /*wantsSubBackground=*/true);
    }
  }
  return RenderContentOnPicture(layer, args, density, imageClip, imageFilter, clipBounds,
                                *inputBounds, contentMatrix, wantsSubBackground);
}

OffscreenResult OffscreenRenderer::RenderPassThrough(Layer* layer, const DrawArgs& args,
                                                     Canvas* parentCanvas,
                                                     const std::optional<Rect>& clipBounds) {
  auto inputBounds = layer->computeContentBounds(clipBounds, true);
  if (!inputBounds.has_value() || inputBounds->isEmpty()) {
    return {};
  }
  auto backdrop = SnapshotOf(parentCanvas);
  if (!backdrop) {
    return {};
  }
  auto parentMatrix = parentCanvas->getMatrix();
  auto surfaceRect = parentMatrix.mapRect(*inputBounds);
  surfaceRect.roundOut();
  surfaceRect.intersect(Rect::MakeWH(backdrop->width(), backdrop->height()));
  if (surfaceRect.isEmpty()) {
    return {};
  }

  if (args.context != nullptr) {
    auto surface = Surface::Make(args.context, static_cast<int>(surfaceRect.width()),
                                 static_cast<int>(surfaceRect.height()), false, 1, false, 0,
                                 args.dstColorSpace);
    if (surface != nullptr) {
      return RenderPassThroughOnSurface(layer, args, std::move(surface), backdrop, parentMatrix,
                                        surfaceRect, *inputBounds);
    }
  }
  return RenderPassThroughOnPicture(layer, args, backdrop, parentMatrix, surfaceRect, *inputBounds);
}

// ─── renderContent backings ───────────────────────────────────────────────────────────────────

OffscreenResult OffscreenRenderer::RenderContentOnSurface(
    Layer* layer, const DrawArgs& args, std::shared_ptr<Surface> surface, const Matrix& density,
    const Rect& imageClip, const std::shared_ptr<ImageFilter>& imageFilter,
    const std::optional<Rect>& clipBounds, const Rect& inputBounds, const Matrix& contentMatrix,
    bool wantsSubBackground) {
  // Surface normalises the basis so (0,0) maps to imageClip.topLeft, giving the allocated pixel
  // grid a canonical origin.
  auto localToSurface = density;
  localToSurface.postTranslate(-imageClip.x(), -imageClip.y());
  auto* canvas = surface->getCanvas();
  canvas->setMatrix(localToSurface);

  auto drawArgs = args;
  std::unique_ptr<BackgroundHandler> subHandler;
  if (wantsSubBackground) {
    subHandler = DeriveSubHandler(args, surface.get(), /*recorder=*/nullptr, inputBounds,
                                  contentMatrix, localToSurface);
    if (subHandler) {
      drawArgs.background.handler = subHandler.get();
    }
  }

  layer->drawDirectly(drawArgs, canvas, 1.0f);

  OffscreenResult result;
  result.image = surface->makeImageSnapshot();
  if (result.image == nullptr) {
    return {};
  }
  localToSurface.invert(&result.imageMatrix);
  result.image =
      ApplyImageFilterIfNeeded(result.image, imageFilter, clipBounds, &result.imageMatrix);
  return result;
}

OffscreenResult OffscreenRenderer::RenderContentOnPicture(
    Layer* layer, const DrawArgs& args, const Matrix& density, const Rect& imageClip,
    const std::shared_ptr<ImageFilter>& imageFilter, const std::optional<Rect>& clipBounds,
    const Rect& inputBounds, const Matrix& contentMatrix, bool wantsSubBackground) {
  PictureRecorder recorder;
  auto* canvas = recorder.beginRecording();
  if (!imageClip.isEmpty()) {
    canvas->clipRect(imageClip, false);
  }
  canvas->setMatrix(density);

  auto drawArgs = args;
  std::unique_ptr<BackgroundHandler> subHandler;
  if (wantsSubBackground) {
    subHandler =
        DeriveSubHandler(args, /*surface=*/nullptr, &recorder, inputBounds, contentMatrix, density);
    if (subHandler) {
      drawArgs.background.handler = subHandler.get();
      // If the sub-bg flushed a segment, the recording canvas pointer may have been swapped.
      // Re-query so follow-on drawing never goes through a stale pointer.
      canvas = recorder.getRecordingCanvas();
    }
  }

  layer->drawDirectly(drawArgs, canvas, 1.0f);

  OffscreenResult result;
  Point offset = {};
  auto dstColorSpace = args.dstColorSpace;
  result.image = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                   std::move(dstColorSpace));
  if (result.image == nullptr) {
    return {};
  }
  density.invert(&result.imageMatrix);
  result.imageMatrix.preTranslate(offset.x, offset.y);
  result.image =
      ApplyImageFilterIfNeeded(result.image, imageFilter, clipBounds, &result.imageMatrix);
  return result;
}

// ─── renderPassThrough backings ───────────────────────────────────────────────────────────────

OffscreenResult OffscreenRenderer::RenderPassThroughOnSurface(
    Layer* layer, const DrawArgs& args, std::shared_ptr<Surface> surface,
    const std::shared_ptr<Image>& backdrop, const Matrix& parentMatrix, const Rect& surfaceRect,
    const Rect& inputBounds) {
  auto localToSurface = parentMatrix;
  localToSurface.postTranslate(-surfaceRect.x(), -surfaceRect.y());

  auto* canvas = surface->getCanvas();
  SeedBackdrop(canvas, backdrop, surfaceRect);
  canvas->setMatrix(localToSurface);

  auto drawArgs = args;
  auto subHandler = DeriveSubHandler(args, surface.get(), /*recorder=*/nullptr, inputBounds,
                                     parentMatrix, localToSurface);
  if (subHandler) {
    drawArgs.background.handler = subHandler.get();
  }

  layer->drawDirectly(drawArgs, canvas, 1.0f);

  OffscreenResult result;
  result.image = surface->makeImageSnapshot();
  if (result.image == nullptr) {
    return {};
  }
  localToSurface.invert(&result.imageMatrix);
  return result;
}

OffscreenResult OffscreenRenderer::RenderPassThroughOnPicture(
    Layer* layer, const DrawArgs& args, const std::shared_ptr<Image>& backdrop,
    const Matrix& parentMatrix, const Rect& surfaceRect, const Rect& inputBounds) {
  auto localToSurface = parentMatrix;
  localToSurface.postTranslate(-surfaceRect.x(), -surfaceRect.y());

  PictureRecorder recorder;
  auto* canvas = recorder.beginRecording();
  SeedBackdrop(canvas, backdrop, surfaceRect);
  canvas->setMatrix(localToSurface);

  auto drawArgs = args;
  auto subHandler = DeriveSubHandler(args, /*surface=*/nullptr, &recorder, inputBounds,
                                     parentMatrix, localToSurface);
  if (subHandler) {
    drawArgs.background.handler = subHandler.get();
    canvas = recorder.getRecordingCanvas();
  }

  layer->drawDirectly(drawArgs, canvas, 1.0f);

  OffscreenResult result;
  Point offset = {};
  auto dstColorSpace = args.dstColorSpace;
  result.image = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                   std::move(dstColorSpace));
  if (result.image == nullptr) {
    return {};
  }
  localToSurface.invert(&result.imageMatrix);
  result.imageMatrix.preTranslate(offset.x, offset.y);
  return result;
}

}  // namespace tgfx
