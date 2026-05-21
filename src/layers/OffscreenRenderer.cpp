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
#include "layers/LayerStyleSource.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

namespace {

std::shared_ptr<Image> SnapshotOf(Canvas* canvas) {
  auto surface = canvas->getSurface();
  if (surface == nullptr) {
    return nullptr;
  }
  return surface->makeImageSnapshot();
}

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
    if (!imageMatrix->invert(&invertMatrix)) {
      return nullptr;
    }
    filterClipBounds = invertMatrix.mapRect(*clipBounds);
    filterClipBounds->roundOut();
  }
  Point filterOffset = {};
  image = image->makeWithFilter(imageFilter, &filterOffset,
                                filterClipBounds.has_value() ? &*filterClipBounds : nullptr);
  imageMatrix->preTranslate(filterOffset.x, filterOffset.y);
  return image;
}

}  // namespace

OffscreenResult OffscreenRenderer::RenderContent(Layer* layer, const DrawArgs& args,
                                                 const Matrix& contentMatrix,
                                                 const std::optional<Rect>& clipBounds) {
  auto inputBounds = layer->computeContentBounds(clipBounds, args.excludeEffects);
  if (!inputBounds.has_value() || inputBounds->isEmpty()) {
    return {};
  }

  // With an image filter, force an isotropic scale so blur / drop-shadow operate in pixel space;
  // without a filter keep the full contentMatrix to preserve rotation/skew fidelity.
  auto imageFilter =
      args.excludeEffects ? nullptr : layer->getImageFilter(contentMatrix.getMaxScale());
  Matrix density = imageFilter
                       ? Matrix::MakeScale(contentMatrix.getMaxScale(), contentMatrix.getMaxScale())
                       : contentMatrix;
  auto imageClip = density.mapRect(*inputBounds);
  imageClip.roundOut();

  // Need a Surface backing only when a descendant Background-sourced style will read back
  // through this subtree; otherwise a PictureRecorder is cheaper.
  if (args.backgroundHandler != nullptr && args.backgroundHandler->needsSurface(layer) &&
      args.context != nullptr) {
    auto surface =
        Surface::Make(args.context, static_cast<int>(imageClip.width()),
                      static_cast<int>(imageClip.height()), false, 1, false, 0, args.dstColorSpace);
    if (surface != nullptr) {
      return RenderContentOnSurface(layer, args, std::move(surface), density, imageClip,
                                    imageFilter, clipBounds, *inputBounds, contentMatrix);
    }
  }
  return RenderContentOnPicture(layer, args, density, imageClip, imageFilter, clipBounds);
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
  if (!surfaceRect.intersect(Rect::MakeWH(backdrop->width(), backdrop->height())) ||
      surfaceRect.isEmpty()) {
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
  return RenderPassThroughOnPicture(layer, args, backdrop, parentMatrix, surfaceRect);
}

OffscreenResult OffscreenRenderer::RenderContentOnSurface(
    Layer* layer, const DrawArgs& args, std::shared_ptr<Surface> surface, const Matrix& density,
    const Rect& imageClip, const std::shared_ptr<ImageFilter>& imageFilter,
    const std::optional<Rect>& clipBounds, const Rect& inputBounds, const Matrix& contentMatrix) {
  // Translate the basis so (0,0) maps to imageClip.topLeft, giving the allocated pixel grid a
  // canonical origin.
  auto localToSurface = density;
  localToSurface.postTranslate(-imageClip.x(), -imageClip.y());
  auto* canvas = surface->getCanvas();
  canvas->setMatrix(localToSurface);

  auto drawArgs = args;
  std::unique_ptr<BackgroundHandler> subHandler;
  if (args.backgroundHandler != nullptr) {
    subHandler = args.backgroundHandler->createSubHandler(surface.get(), args, inputBounds,
                                                          contentMatrix, localToSurface);
    if (subHandler != nullptr) {
      drawArgs.backgroundHandler = subHandler.get();
      if (auto* rects = subHandler->renderRects()) {
        drawArgs.renderRects = rects;
      }
    }
    // Null sub-handler means the parent handler does not need rebinding for this offscreen
    // subtree (e.g. Consumer keys snapshots by Layer*/Style*, independent of canvas matrix).
  }

  layer->drawDirectly(drawArgs, canvas, 1.0f);

  OffscreenResult result;
  result.image = surface->makeImageSnapshot();
  if (result.image == nullptr) {
    return {};
  }
  if (!localToSurface.invert(&result.imageMatrix)) {
    return {};
  }
  result.image =
      ApplyImageFilterIfNeeded(result.image, imageFilter, clipBounds, &result.imageMatrix);
  return result;
}

OffscreenResult OffscreenRenderer::RenderContentOnPicture(
    Layer* layer, const DrawArgs& args, const Matrix& density, const Rect& imageClip,
    const std::shared_ptr<ImageFilter>& imageFilter, const std::optional<Rect>& clipBounds) {
  PictureRecorder recorder;
  auto* canvas = recorder.beginRecording();
  if (!imageClip.isEmpty()) {
    canvas->clipRect(imageClip, false);
  }
  canvas->setMatrix(density);

  layer->drawDirectly(args, canvas, 1.0f);

  OffscreenResult result;
  Point offset = {};
  auto dstColorSpace = args.dstColorSpace;
  result.image = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                   std::move(dstColorSpace));
  if (result.image == nullptr) {
    return {};
  }
  if (!density.invert(&result.imageMatrix)) {
    return {};
  }
  result.imageMatrix.preTranslate(offset.x, offset.y);
  result.image =
      ApplyImageFilterIfNeeded(result.image, imageFilter, clipBounds, &result.imageMatrix);
  return result;
}

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
  std::unique_ptr<BackgroundHandler> subHandler;
  if (args.backgroundHandler != nullptr) {
    subHandler = args.backgroundHandler->createSubHandler(surface.get(), args, inputBounds,
                                                          parentMatrix, localToSurface);
    if (subHandler != nullptr) {
      drawArgs.backgroundHandler = subHandler.get();
      if (auto* rects = subHandler->renderRects()) {
        drawArgs.renderRects = rects;
      }
    }
  }

  layer->drawDirectly(drawArgs, canvas, 1.0f);

  OffscreenResult result;
  result.image = surface->makeImageSnapshot();
  if (result.image == nullptr) {
    return {};
  }
  if (!localToSurface.invert(&result.imageMatrix)) {
    return {};
  }
  return result;
}

OffscreenResult OffscreenRenderer::RenderPassThroughOnPicture(
    Layer* layer, const DrawArgs& args, const std::shared_ptr<Image>& backdrop,
    const Matrix& parentMatrix, const Rect& surfaceRect) {
  auto localToSurface = parentMatrix;
  localToSurface.postTranslate(-surfaceRect.x(), -surfaceRect.y());

  PictureRecorder recorder;
  auto* canvas = recorder.beginRecording();
  canvas->clipRect(Rect::MakeWH(surfaceRect.width(), surfaceRect.height()));
  SeedBackdrop(canvas, backdrop, surfaceRect);
  canvas->setMatrix(localToSurface);

  layer->drawDirectly(args, canvas, 1.0f);

  OffscreenResult result;
  Point offset = {};
  auto dstColorSpace = args.dstColorSpace;
  result.image = ToImageWithOffset(recorder.finishRecordingAsPicture(), &offset, nullptr,
                                   std::move(dstColorSpace));
  if (result.image == nullptr) {
    return {};
  }
  if (!localToSurface.invert(&result.imageMatrix)) {
    return {};
  }
  result.imageMatrix.preTranslate(offset.x, offset.y);
  return result;
}

}  // namespace tgfx
