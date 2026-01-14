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

#include "Layer3DContext.h"
#include "Contour3DContext.h"
#include "Render3DContext.h"
#include "core/Matrix3DUtils.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/compositing3d/Context3DCompositor.h"

namespace tgfx {

std::shared_ptr<Layer3DContext> Layer3DContext::Make(
    bool contourMode, Context* context, const Rect& renderRect, float contentScale,
    std::shared_ptr<ColorSpace> colorSpace, std::shared_ptr<BackgroundContext> backgroundContext) {
  if (contourMode) {
    DEBUG_ASSERT(backgroundContext == nullptr);
    return std::make_shared<Contour3DContext>(renderRect, contentScale, std::move(colorSpace));
  }
  auto compositor = std::make_shared<Context3DCompositor>(
      *context, static_cast<int>(renderRect.width()), static_cast<int>(renderRect.height()));
  return std::make_shared<Render3DContext>(std::move(compositor), renderRect, contentScale,
                                           std::move(colorSpace), std::move(backgroundContext));
}

Layer3DContext::Layer3DContext(const Rect& renderRect, float contentScale,
                               std::shared_ptr<ColorSpace> colorSpace)
    : _renderRect(renderRect), _contentScale(contentScale), _colorSpace(std::move(colorSpace)) {
}

std::shared_ptr<Image> Layer3DContext::PictureToImage(std::shared_ptr<Picture> picture,
                                                      Point* offset,
                                                      std::shared_ptr<ColorSpace> colorSpace) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = picture->getBounds();
  bounds.roundOut();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), static_cast<int>(bounds.width()),
                               static_cast<int>(bounds.height()), &matrix, std::move(colorSpace));
  if (offset) {
    offset->x = bounds.left;
    offset->y = bounds.top;
  }
  return image;
}

Matrix3D Layer3DContext::currentTransform() const {
  return _transformStack.empty() ? Matrix3D::I() : _transformStack.top().transform;
}

bool Layer3DContext::isFinished() const {
  return _transformStack.empty();
}

Canvas* Layer3DContext::beginRecording(const Matrix3D& childTransform, bool antialiasing) {
  auto newTransform = childTransform;
  newTransform.postConcat(currentTransform());
  _transformStack.emplace(newTransform, antialiasing);

  auto canvas = onBeginRecording();
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  canvas->scale(_contentScale, _contentScale);
  auto invScale = 1.0f / _contentScale;
  auto contextBounds =
      Rect::MakeXYWH(_renderRect.x() * invScale, _renderRect.y() * invScale,
                     _renderRect.width() * invScale, _renderRect.height() * invScale);
  auto localClipRect = Matrix3DUtils::InverseMapRect(contextBounds, newTransform);
  if (!localClipRect.isEmpty()) {
    canvas->clipRect(localClipRect);
  }
  return canvas;
}

void Layer3DContext::endRecording() {
  auto picture = onFinishRecording();

  if (_transformStack.empty()) {
    return;
  }
  auto state = _transformStack.top();
  _transformStack.pop();

  Point pictureOffset = {};
  auto image = PictureToImage(std::move(picture), &pictureOffset, _colorSpace);
  if (image == nullptr) {
    return;
  }

  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  auto invScale = 1.0f / _contentScale;
  auto imageOrigin = Point::Make(pictureOffset.x * invScale, pictureOffset.y * invScale);
  auto imageTransform = Matrix3DUtils::OriginAdaptedMatrix3D(state.transform, imageOrigin);
  imageTransform = Matrix3DUtils::ScaleAdaptedMatrix3D(imageTransform, _contentScale);

  onImageReady(std::move(image), imageTransform, pictureOffset, state.antialiasing);
}

}  // namespace tgfx
