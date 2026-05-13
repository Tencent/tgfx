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
#include <unordered_map>
#include "Context3DCompositor.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/BackgroundHandler.h"
#include "layers/DrawArgs.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Surface.h"
#include "tgfx/layers/Layer.h"

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

void Render3DContext::finishAndDrawTo(const DrawArgs& args, Canvas* canvas) {
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  if (_pendingNodes.empty() || args.context == nullptr) {
    return;
  }

  // Register every node as a polygon (geometry only). The compositor's BSP tree may split
  // polygons, but each fragment retains a pointer to its source layer for raster lookup.
  for (const auto& node : _pendingNodes) {
    Matrix3D finalTransform = node.transform;
    finalTransform.postScale(_contentScale, _contentScale, 1.0f);
    finalTransform.postTranslate(-_renderRect.left, -_renderRect.top, 0);
    _compositor->addPolygon(node.layer, node.localBounds, finalTransform, node.depth, node.alpha,
                            node.antialiasing);
  }

  const auto& fragments = _compositor->prepareTraversal();

  // Rasterize each unique layer once on demand and feed it to the compositor in BSP order. We use
  // standard Layer::drawLayer so prepareMask, offscreen handling, and layerStyles all run as
  // usual. Layer::drawChildren detects args.render3DContext != nullptr on preserve3D middle nodes
  // and stops descending — descendants are independently registered nodes.
  std::unordered_map<Layer*, std::shared_ptr<Image>> layerImages;
  layerImages.reserve(_pendingNodes.size());
  DrawArgs leafArgs = args;
  leafArgs.render3DContext = nullptr;
  leafArgs.opaqueContext = nullptr;
  leafArgs.backgroundHandler = BackgroundHandler::NoOp();

  for (auto* fragment : fragments) {
    auto* layer = fragment->layer();
    auto cacheIt = layerImages.find(layer);
    if (cacheIt == layerImages.end()) {
      auto image = rasterLayer(layer, fragment->localBounds(), fragment->alpha(), leafArgs);
      cacheIt = layerImages.emplace(layer, std::move(image)).first;
    }
    if (cacheIt->second == nullptr) {
      continue;
    }
    _compositor->drawPolygon(fragment, cacheIt->second);
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
