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

#include "Opaque3DContext.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/BackgroundHandler.h"
#include "layers/DrawArgs.h"
#include "layers/OpaqueContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"
#include "tgfx/core/Picture.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {

Opaque3DContext::Opaque3DContext(const Rect& renderRect, float contentScale,
                                 std::shared_ptr<ColorSpace> colorSpace)
    : Layer3DContext(renderRect, contentScale, std::move(colorSpace)) {
}

void Opaque3DContext::emitNode(Layer* layer, const Rect& localBounds, const Matrix3D& transform,
                               float alpha, int /*depth*/, bool /*hasBackgroundStyle*/) {
  PendingNode node;
  node.layer = layer;
  node.transform = transform;
  node.localBounds = localBounds;
  node.alpha = alpha;
  _pendingNodes.push_back(node);
}

void Opaque3DContext::finishAndDrawTo(const DrawArgs& args, Canvas* canvas) {
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  if (_pendingNodes.empty()) {
    return;
  }

  // Rasterize each node's contour into an opaque picture, then convert to image so it can be
  // composited with a 3D transform. drawChildren detects render3DContext != nullptr and stops
  // descending on preserve3D middle nodes; non-preserve3D leaves draw their full subtree.
  struct OpaqueImage {
    std::shared_ptr<Image> image = nullptr;
    Matrix3D transform = {};
    Point pictureOffset = {};
  };
  std::vector<OpaqueImage> opaqueImages;
  opaqueImages.reserve(_pendingNodes.size());

  DrawArgs leafArgs = args;
  leafArgs.opaqueContext = nullptr;
  leafArgs.backgroundHandler = BackgroundHandler::NoOp();

  for (const auto& node : _pendingNodes) {
    OpaqueContext opaqueContext;
    auto* leafCanvas = opaqueContext.beginRecording();
    leafCanvas->scale(_contentScale, _contentScale);
    DrawArgs nodeArgs = leafArgs;
    nodeArgs.opaqueContext = &opaqueContext;
    nodeArgs.render3DContext = shared_from_this();
    drawWithFunc(node.layer, nodeArgs, leafCanvas, node.alpha, BlendMode::SrcOver);
    auto picture = opaqueContext.finishRecordingAsPicture();
    if (picture == nullptr) {
      continue;
    }
    auto bounds = picture->getBounds();
    bounds.roundOut();
    if (bounds.isEmpty()) {
      continue;
    }
    auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
    auto image = Image::MakeFrom(std::move(picture), static_cast<int>(bounds.width()),
                                 static_cast<int>(bounds.height()), &matrix, _colorSpace);
    if (image == nullptr) {
      continue;
    }
    opaqueImages.push_back({std::move(image), node.transform, {bounds.left, bounds.top}});
  }

  if (opaqueImages.empty()) {
    return;
  }

  Paint paint = {};
  AutoCanvasRestore outerRestore(canvas);
  // The caller's canvas is already scaled by contentScale (see getContentContourImage / mask
  // recording paths), so polygon vertices computed below stay in layer-local coordinates and the
  // existing canvas scale takes them to scaled-canvas pixels.
  for (const auto& entry : opaqueImages) {
    AutoCanvasRestore autoRestore(canvas);
    auto matrix = entry.transform.asMatrix();
    matrix.preTranslate(entry.pictureOffset.x / _contentScale,
                        entry.pictureOffset.y / _contentScale);
    matrix.preScale(1.0f / _contentScale, 1.0f / _contentScale);
    canvas->concat(matrix);
    canvas->drawImage(entry.image, &paint);
  }
}

}  // namespace tgfx
