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

#include "Render3DContext.h"
#include "Context3DCompositor.h"
#include "core/Matrix2D.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Image.h"

namespace tgfx {

static Matrix3D OriginAdaptedMatrix3D(const Matrix3D& matrix3D, const Point& newOrigin) {
  auto offsetMatrix = Matrix3D::MakeTranslate(newOrigin.x, newOrigin.y, 0);
  auto invOffsetMatrix = Matrix3D::MakeTranslate(-newOrigin.x, -newOrigin.y, 0);
  return invOffsetMatrix * matrix3D * offsetMatrix;
}

static Rect InverseMapRect(const Rect& rect, const Matrix3D& matrix) {
  float values[16] = {};
  matrix.getColumnMajor(values);
  auto matrix2D = Matrix2D::MakeAll(values[0], values[1], values[3], values[4], values[5],
                                    values[7], values[12], values[13], values[15]);
  Matrix2D inversedMatrix;
  if (!matrix2D.invert(&inversedMatrix)) {
    return Rect::MakeEmpty();
  }
  return inversedMatrix.mapRect(rect);
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
  _stateStack.emplace(newTransform, antialiasing);
  auto canvas = _stateStack.top().recorder.beginRecording();

  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  auto invScale = 1.0f / _contentScale;
  // The bounds of the 3D rendering context, inverse-mapped through newTransform
  // to get the clip rect in local layer coordinate space.
  auto contextBounds = Rect::MakeXYWH(_offset.x * invScale, _offset.y * invScale,
                                      static_cast<float>(_compositor->width()) * invScale,
                                      static_cast<float>(_compositor->height()) * invScale);
  auto localClipRect = InverseMapRect(contextBounds, newTransform);
  if (!localClipRect.isEmpty()) {
    canvas->clipRect(localClipRect);
  }
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
