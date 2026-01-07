/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "layers/Render3DContext.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Image.h"

namespace tgfx {

static Matrix3D OriginAdaptedMatrix3D(const Matrix3D& matrix3D, const Point& newOrigin) {
  auto offsetMatrix = Matrix3D::MakeTranslate(newOrigin.x, newOrigin.y, 0);
  auto invOffsetMatrix = Matrix3D::MakeTranslate(-newOrigin.x, -newOrigin.y, 0);
  return invOffsetMatrix * matrix3D * offsetMatrix;
}

static std::shared_ptr<Image> PictureToImage(std::shared_ptr<Picture> picture, Point* offset,
                                             std::shared_ptr<ColorSpace> colorSpace) {
  if (picture == nullptr) {
    return nullptr;
  }
  auto bounds = picture->getBounds();
  bounds.roundOut();
  auto matrix = Matrix::MakeTrans(-bounds.x(), -bounds.y());
  auto image = Image::MakeFrom(std::move(picture), FloatCeilToInt(bounds.width()),
                               FloatCeilToInt(bounds.height()), &matrix, std::move(colorSpace));
  if (offset) {
    offset->x = bounds.left;
    offset->y = bounds.top;
  }
  return image;
}

Canvas* Render3DContext::beginRecording(const Matrix3D& childTransform, bool antialiasing) {
  auto baseTransform = _stateStack.empty() ? Matrix3D::I() : _stateStack.top().transform;
  auto newTransform = childTransform;
  newTransform.postConcat(baseTransform);
  _stateStack.push({});
  _stateStack.top().transform = newTransform;
  _stateStack.top().antialiasing = antialiasing;
  auto canvas = _stateStack.top().recorder.beginRecording();
  canvas->scale(_contentScale, _contentScale);
  return canvas;
}

void Render3DContext::endRecording() {
  if (_stateStack.empty()) {
    return;
  }
  auto& state = _stateStack.top();
  auto picture = state.recorder.finishRecordingAsPicture();
  auto layerTransform = state.transform;
  auto antialiasing = state.antialiasing;
  _stateStack.pop();

  Point pictureOffset = {};
  auto image = PictureToImage(std::move(picture), &pictureOffset, _colorSpace);
  if (image == nullptr) {
    return;
  }

  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  auto invScale = 1.0f / _contentScale;
  auto imageOrigin = Point::Make(pictureOffset.x * invScale, pictureOffset.y * invScale);
  auto imageTransform = OriginAdaptedMatrix3D(layerTransform, imageOrigin);
  if (!FloatNearlyEqual(invScale, 1.0f)) {
    auto invScaleMatrix = Matrix3D::MakeScale(invScale, invScale, 1.0f);
    auto scaleMatrix = Matrix3D::MakeScale(_contentScale, _contentScale, 1.0f);
    imageTransform = scaleMatrix * imageTransform * invScaleMatrix;
  }
  imageTransform.postTranslate(pictureOffset.x - _offset.x, pictureOffset.y - _offset.y, 0);
  _compositor->addImage(image, imageTransform, 1.0f, antialiasing);
}

}  // namespace tgfx
