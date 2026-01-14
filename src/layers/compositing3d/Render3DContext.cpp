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
#include "layers/BackgroundContext.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"

namespace tgfx {

Render3DContext::Render3DContext(std::shared_ptr<Context3DCompositor> compositor,
                                 const Rect& renderRect, float contentScale,
                                 std::shared_ptr<ColorSpace> colorSpace,
                                 std::shared_ptr<BackgroundContext> backgroundContext)
    : Layer3DContext(renderRect, contentScale, std::move(colorSpace)),
      _compositor(std::move(compositor)), _backgroundContext(std::move(backgroundContext)) {
}

Canvas* Render3DContext::onBeginRecording() {
  _recorderStack.emplace();
  return _recorderStack.top().beginRecording();
}

std::shared_ptr<Picture> Render3DContext::onFinishRecording() {
  if (_recorderStack.empty()) {
    return nullptr;
  }
  auto picture = _recorderStack.top().finishRecordingAsPicture();
  _recorderStack.pop();
  return picture;
}

void Render3DContext::onImageReady(std::shared_ptr<Image> image, const Matrix3D& imageTransform,
                                   const Point& pictureOffset, bool antialiasing) {
  auto finalTransform = imageTransform;
  finalTransform.postTranslate(pictureOffset.x - _renderRect.left, pictureOffset.y - _renderRect.top,
                               0);
  _compositor->addImage(std::move(image), finalTransform, 1.0f, antialiasing);
}

void Render3DContext::finishAndDrawTo(Canvas* canvas, bool antialiasing) {
  auto context3DImage = _compositor->finish();
  AutoCanvasRestore autoRestore(canvas);
  auto imageMatrix = Matrix::MakeScale(1.0f / _contentScale, 1.0f / _contentScale);
  imageMatrix.preTranslate(_renderRect.left, _renderRect.top);
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
