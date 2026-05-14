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

#include "Render3DContext.h"
#include "Context3DCompositor.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/BackgroundHandler.h"
#include "layers/DrawArgs.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/PictureRecorder.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/layerstyles/LayerStyle.h"

namespace tgfx {

Render3DContext::Render3DContext(std::shared_ptr<Context3DCompositor> compositor,
                                 const Rect& renderRect, float contentScale,
                                 std::shared_ptr<ColorSpace> colorSpace)
    : Layer3DContext(renderRect, contentScale, std::move(colorSpace)),
      _compositor(std::move(compositor)) {
}

void Render3DContext::addLayer(Layer* layer, const Matrix3D& transform, float alpha,
                               LayerDrawFunc drawFunc) {
  _drawFunc = drawFunc;
  collectNodes(layer, transform, alpha, /*depth=*/0);
}

void Render3DContext::collectNodes(Layer* layer, const Matrix3D& transform, float alpha,
                                   int depth) {
  if (layer == nullptr) {
    return;
  }
  Rect bounds = {};
  if (layer->canPreserve3D()) {
    auto contentBounds = layer->computeContentBounds({}, false);
    if (contentBounds.has_value()) {
      bounds = *contentBounds;
    }
  } else {
    bounds = layer->getBounds();
  }
  if (!bounds.isEmpty()) {
    PendingNode node;
    node.layer = layer;
    node.transform = transform;
    node.localBounds = bounds;
    node.depth = depth;
    node.alpha = alpha;
    node.antialiasing = layer->allowsEdgeAntialiasing();
    _pendingNodes.push_back(node);
  }
  if (!layer->canPreserve3D()) {
    return;
  }
  for (auto& child : layer->_children) {
    if (child->maskOwner || !child->visible() || child->_alpha <= 0) {
      continue;
    }
    auto childTransform = child->getMatrixWithScrollRect();
    childTransform.postConcat(transform);
    collectNodes(child.get(), childTransform, alpha * child->_alpha, depth + 1);
  }
}

bool Render3DContext::SubtreeHasBackgroundStyle(Layer* layer) {
  // The cached presence flag is set whenever a Background-sourced style exists in this layer or
  // any of its descendants, so we can skip the recursion. Friend access reads it directly.
  return layer != nullptr && layer->maxBackgroundOutset > 0;
}

void Render3DContext::finishAndDrawTo(const DrawArgs& args, Canvas* canvas) {
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  if (_pendingNodes.empty() || args.context == nullptr) {
    return;
  }

  // Register every node as a polygon (geometry only). The compositor's BSP tree may split
  // polygons; each fragment retains a Layer* (for raster lookup) and node.alpha. node.alpha is
  // carried as the polygon's nominal alpha but applied differently in the raster vs compositor
  // passes depending on the layer's group-opacity setting (decided per-stage below).
  for (const auto& node : _pendingNodes) {
    Matrix3D finalTransform = node.transform;
    finalTransform.postScale(_contentScale, _contentScale, 1.0f);
    finalTransform.postTranslate(-_renderRect.left, -_renderRect.top, 0);
    _compositor->addPolygon(node.layer, node.localBounds, finalTransform, node.depth, node.alpha,
                            node.antialiasing);
  }

  const auto& fragments = _compositor->prepareTraversal();

  // Detect whether the subtree contains any Background-sourced layer style. The Compositor3DCapturer
  // only needs to be installed when there's something inside the subtree that will dispatch.
  bool subtreeNeedsBackdrop = false;
  for (const auto& node : _pendingNodes) {
    if (SubtreeHasBackgroundStyle(node.layer)) {
      subtreeNeedsBackdrop = true;
      break;
    }
  }

  // Prime the compositor target with the outer canvas snapshot so any in-subtree BackgroundBlur
  // dispatch sees "outside the 3D subtree" content as part of its backdrop. We project the outer
  // snapshot into compositor pixel space via PictureRecorder, then ask the compositor to lay it
  // down as the very first quad — subsequent BSP fragments composite on top via SrcOver.
  if (subtreeNeedsBackdrop) {
    if (auto* outerSurface = canvas->getSurface()) {
      auto outerSnapshot = outerSurface->makeImageSnapshot();
      if (outerSnapshot != nullptr) {
        auto targetWidth = static_cast<int>(_renderRect.width());
        auto targetHeight = static_cast<int>(_renderRect.height());
        if (targetWidth > 0 && targetHeight > 0) {
          // Compose: device pixel → compositor pixel.
          //   compositor pixel → outer canvas-local: Scale(1/contentScale).preTranslate(rr.tl)
          //   outer canvas-local → device pixel: outerCanvasMatrix
          // Combined the other direction (outer device → compositor pixel) by inverting both.
          Matrix compositorToOuterDevice = canvas->getMatrix();
          compositorToOuterDevice.preScale(1.0f / _contentScale, 1.0f / _contentScale);
          compositorToOuterDevice.preTranslate(_renderRect.left, _renderRect.top);
          Matrix outerDeviceToCompositor = Matrix::I();
          if (compositorToOuterDevice.invert(&outerDeviceToCompositor)) {
            PictureRecorder primeRecorder = {};
            auto* primeRec = primeRecorder.beginRecording();
            primeRec->concat(outerDeviceToCompositor);
            primeRec->drawImage(outerSnapshot);
            auto picture = primeRecorder.finishRecordingAsPicture();
            if (picture != nullptr) {
              Matrix imageMatrix = Matrix::I();
              auto primeImage = Image::MakeFrom(std::move(picture), targetWidth, targetHeight,
                                                &imageMatrix, _colorSpace);
              if (primeImage != nullptr) {
                _compositor->primeWithImage(primeImage);
              }
            }
          }
        }
      }
    }
  }
  // Build a single shared sub-handler for the whole BSP traversal. dispatch fires per-layer per-
  // fragment; the handler reads the compositor target on demand inside drawBackgroundStyle so
  // each dispatch sees the up-to-date BSP accumulation. Capture writes one entry per dispatch
  // into the shared snapshot list; consume reads them back in the same dispatch order, so the
  // list cursor stays in lock-step.
  std::unique_ptr<Compositor3DCapturer> compositorCapturer;
  bool capturePass = args.backgroundHandler != nullptr && args.backgroundHandler->isCapturer();
  if (subtreeNeedsBackdrop && capturePass) {
    auto* outerCapturer = static_cast<BackgroundCapturer*>(args.backgroundHandler);
    compositorCapturer = std::make_unique<Compositor3DCapturer>(
        outerCapturer->snapshotMap(), _compositor.get(), _renderRect, _contentScale, _colorSpace);
  }

  std::unordered_map<Layer*, std::shared_ptr<Image>> layerImages;
  layerImages.reserve(_pendingNodes.size());
  DrawArgs leafArgs = args;
  leafArgs.opaqueContext = nullptr;
  // Replace the outer handler with the compositor-aware one during raster. Capture: write entries
  // into the snapshot list. Consume: keep the outer consumer (it reads the same list under the
  // same key, advancing its own cursor). NoOp paths fall through to the parent NoOp.
  if (compositorCapturer) {
    leafArgs.backgroundHandler = compositorCapturer.get();
  }

  for (auto* fragment : fragments) {
    auto* layer = fragment->layer();
    // Per-fragment raster (no caching) is required whenever the subtree carries BackgroundBlur:
    // each BSP slice's raster sees a different mid-traversal compositor state, so cached images
    // would drift from the snapshot the consume pass expects. Plain layers without backdrop
    // dependency keep the shared cache for cheap re-use across split fragments.
    bool perFragmentRaster = SubtreeHasBackgroundStyle(layer);

    // Raster alpha mirrors the compositor's vertex-color logic: when the layer opts into group
    // opacity, raster the subtree opaque (alpha=1) and let the compositor multiply by node.alpha
    // at composite time; otherwise bake node.alpha into the raster image so the compositor stays
    // at alpha=1.
    float rasterAlpha = layer->allowsGroupOpacity() ? 1.0f : fragment->alpha();
    std::shared_ptr<Image> image;
    if (perFragmentRaster) {
      image = rasterLayer(layer, fragment->localBounds(), rasterAlpha, leafArgs);
    } else {
      auto cacheIt = layerImages.find(layer);
      if (cacheIt == layerImages.end()) {
        image = rasterLayer(layer, fragment->localBounds(), rasterAlpha, leafArgs);
        layerImages.emplace(layer, image);
      } else {
        image = cacheIt->second;
      }
    }
    if (image == nullptr) {
      continue;
    }
    _compositor->drawPolygon(fragment, image);
  }

  auto contextImage = _compositor->flushToImage();
  if (contextImage == nullptr) {
    return;
  }

  AutoCanvasRestore autoRestore(canvas);
  auto imageMatrix = Matrix::MakeScale(1.0f / _contentScale, 1.0f / _contentScale);
  imageMatrix.preTranslate(_renderRect.left, _renderRect.top);
  canvas->concat(imageMatrix);
  canvas->drawImage(contextImage);
}

std::shared_ptr<Image> Render3DContext::rasterLayer(Layer* layer, const Rect& localBounds,
                                                    float alpha, DrawArgs& leafArgs) {
  auto scaledBounds = localBounds;
  scaledBounds.scale(_contentScale, _contentScale);
  scaledBounds.roundOut();
  auto width = static_cast<int>(scaledBounds.width());
  auto height = static_cast<int>(scaledBounds.height());
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  auto surface = Surface::Make(leafArgs.context, width, height, false, 1, false, 0, _colorSpace);
  if (surface == nullptr) {
    return nullptr;
  }
  auto* leafCanvas = surface->getCanvas();
  auto density = Matrix::MakeScale(_contentScale, _contentScale);
  density.postTranslate(-localBounds.left * _contentScale, -localBounds.top * _contentScale);
  leafCanvas->setMatrix(density);

  // Set render3DContext so that Layer::drawChildren detects "I am a preserve3D middle node being
  // rasterized inside a 3D context" and stops descending. Non-preserve3D leaves still draw their
  // children normally because the guard checks canPreserve3D() too.
  leafArgs.render3DContext = shared_from_this();
  auto blendMode = static_cast<BlendMode>(layer->bitFields.blendMode);
  (layer->*_drawFunc)(leafArgs, leafCanvas, alpha, blendMode);
  leafArgs.render3DContext = nullptr;
  return surface->makeImageSnapshot();
}

}  // namespace tgfx
