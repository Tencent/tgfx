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
    _subtreeNeedsBackdrop = _subtreeNeedsBackdrop || layer->hasBackgroundStyle();
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

void Render3DContext::finishAndDrawTo(const DrawArgs& args, Canvas* canvas) {
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  if (_pendingNodes.empty() || args.context == nullptr) {
    return;
  }

  // Register every collected node as a polygon. The compositor's BSP tree may split polygons
  // further; each fragment carries the layer pointer and node alpha for the raster pass below.
  for (const auto& node : _pendingNodes) {
    Matrix3D finalTransform = node.transform;
    finalTransform.postScale(_contentScale, _contentScale, 1.0f);
    finalTransform.postTranslate(-_renderRect.left, -_renderRect.top, 0);
    _compositor->addPolygon(node.layer, node.localBounds, finalTransform, node.depth, node.alpha,
                            node.antialiasing);
  }

  const auto& fragments = _compositor->prepareTraversal();

  // BackgroundBlur dispatches inside the subtree see two layers of backdrop: outer-canvas content
  // (primed once before the BSP loop) and intra-subtree fragments accumulated as the loop runs.
  if (_subtreeNeedsBackdrop) {
    primeCompositorFromOuterCanvas(canvas);
  }

  // Capture-pass: install a Compositor3DCapturer so each in-subtree dispatch writes one entry
  // into the shared snapshot list. Consume-pass keeps the outer consumer; both walk the list in
  // dispatch order via a cursor, so capture and consume stay in lock-step.
  std::unique_ptr<Compositor3DCapturer> compositorCapturer;
  bool capturePass = args.backgroundHandler != nullptr && args.backgroundHandler->isCapturer();
  if (_subtreeNeedsBackdrop && capturePass) {
    auto* outerCapturer = static_cast<BackgroundCapturer*>(args.backgroundHandler);
    compositorCapturer = std::make_unique<Compositor3DCapturer>(
        outerCapturer->snapshotMap(), _compositor.get(), _renderRect, _contentScale, _colorSpace);
  }

  std::unordered_map<Layer*, std::shared_ptr<Image>> layerImages;
  layerImages.reserve(_pendingNodes.size());
  DrawArgs leafArgs = args;
  leafArgs.opaqueContext = nullptr;
  if (compositorCapturer) {
    leafArgs.backgroundHandler = compositorCapturer.get();
  }

  for (auto* fragment : fragments) {
    auto* layer = fragment->layer();
    // Layers carrying BackgroundBlur must raster per-fragment because each BSP slice samples a
    // different mid-traversal compositor state; cached images would drift from what the consume
    // pass expects. Plain layers keep the shared cache for cheap re-use across split fragments.
    bool perFragmentRaster = layer->hasBackgroundStyle();
    float rasterAlpha = fragment->rasterAlpha();
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

  // render3DContext signals Layer::drawChildren that we're rasterizing inside a 3D context, so
  // preserve3D middle nodes are not recursed into (they're already registered as polygons).
  // Non-preserve3D leaves still draw their own children normally.
  leafArgs.render3DContext = shared_from_this();
  auto blendMode = static_cast<BlendMode>(layer->bitFields.blendMode);
  (layer->*_drawFunc)(leafArgs, leafCanvas, alpha, blendMode);
  leafArgs.render3DContext = nullptr;
  return surface->makeImageSnapshot();
}

void Render3DContext::primeCompositorFromOuterCanvas(Canvas* outerCanvas) {
  auto* outerSurface = outerCanvas->getSurface();
  if (outerSurface == nullptr) {
    return;
  }
  auto outerSnapshot = outerSurface->makeImageSnapshot();
  if (outerSnapshot == nullptr) {
    return;
  }
  auto targetWidth = static_cast<int>(_renderRect.width());
  auto targetHeight = static_cast<int>(_renderRect.height());
  if (targetWidth <= 0 || targetHeight <= 0) {
    return;
  }
  // Map outer-device pixels into compositor pixel space:
  //   compositor pixel → outer canvas-local: Scale(1/contentScale).preTranslate(rr.tl)
  //   outer canvas-local → outer device:   outerCanvas->getMatrix()
  Matrix compositorToOuterDevice = outerCanvas->getMatrix();
  compositorToOuterDevice.preScale(1.0f / _contentScale, 1.0f / _contentScale);
  compositorToOuterDevice.preTranslate(_renderRect.left, _renderRect.top);
  Matrix outerDeviceToCompositor = Matrix::I();
  if (!compositorToOuterDevice.invert(&outerDeviceToCompositor)) {
    return;
  }
  PictureRecorder recorder = {};
  recorder.beginRecording()->drawImage(std::move(outerSnapshot));
  auto picture = recorder.finishRecordingAsPicture();
  if (picture == nullptr) {
    return;
  }
  auto primeImage = Image::MakeFrom(std::move(picture), targetWidth, targetHeight,
                                    &outerDeviceToCompositor, _colorSpace);
  if (primeImage != nullptr) {
    _compositor->primeWithImage(primeImage);
  }
}

}  // namespace tgfx
