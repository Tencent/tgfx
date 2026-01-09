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
#include "core/Matrix3DUtils.h"
#include "core/utils/MathExtra.h"
#include "layers/BackgroundContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Paint.h"

namespace tgfx {

static std::shared_ptr<Image> PictureToImage(std::shared_ptr<Picture> picture, Point* offset,
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
  auto localClipRect = Matrix3DUtils::InverseMapRect(contextBounds, newTransform);
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
  auto imageTransform = Matrix3DUtils::OriginAdaptedMatrix3D(layerTransform, imageOrigin);
  imageTransform = Matrix3DUtils::ScaleAdaptedMatrix3D(imageTransform, _contentScale);
  imageTransform.postTranslate(pictureOffset.x - _offset.x, pictureOffset.y - _offset.y, 0);
  _compositor->addImage(image, imageTransform, 1.0f, antialiasing);
}

void Render3DContext::finishAndDrawTo(Canvas* canvas, bool antialiasing) {
  auto context3DImage = _compositor->finish();
  // The final texture has been scaled proportionally during generation, so draw it at its actual
  // size on the canvas.
  AutoCanvasRestore autoRestore(canvas);
  auto imageMatrix = Matrix::MakeScale(1.0f / _contentScale, 1.0f / _contentScale);
  imageMatrix.preTranslate(_offset.x, _offset.y);
  canvas->concat(imageMatrix);
  canvas->drawImage(context3DImage);

  if (_backgroundContext) {
    Paint paint = {};
    paint.setAntiAlias(antialiasing);
    auto backgroundCanvas = _backgroundContext->getCanvas();
    AutoCanvasRestore autoRestoreBg(backgroundCanvas);
    backgroundCanvas->concat(imageMatrix);
    backgroundCanvas->drawImage(context3DImage, &paint);
  }
}

}  // namespace tgfx
