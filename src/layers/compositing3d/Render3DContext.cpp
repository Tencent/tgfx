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
#include "tgfx/core/Paint.h"

namespace tgfx {

Render3DContext::Render3DContext(std::shared_ptr<Context3DCompositor> compositor,
                                 const Point& offset, float contentScale,
                                 std::shared_ptr<ColorSpace> colorSpace,
                                 std::shared_ptr<BackgroundContext> backgroundContext)
    : Base3DContext(contentScale, std::move(colorSpace)), _compositor(std::move(compositor)),
      _offset(offset), _backgroundContext(std::move(backgroundContext)) {
}

void Render3DContext::onBeginRecording(Canvas* canvas, const Matrix3D& transform) {
  auto invScale = 1.0f / _contentScale;
  // The bounds of the 3D rendering context, inverse-mapped through transform
  // to get the clip rect in local layer coordinate space.
  auto contextBounds = Rect::MakeXYWH(_offset.x * invScale, _offset.y * invScale,
                                      static_cast<float>(_compositor->width()) * invScale,
                                      static_cast<float>(_compositor->height()) * invScale);
  auto localClipRect = Matrix3DUtils::InverseMapRect(contextBounds, transform);
  if (!localClipRect.isEmpty()) {
    canvas->clipRect(localClipRect);
  }
}

void Render3DContext::onEndRecording(std::shared_ptr<Image> image, const Matrix3D& transform,
                                     const Point& pictureOffset, bool antialiasing) {
  auto imageTransform = transform;
  imageTransform.postTranslate(pictureOffset.x - _offset.x, pictureOffset.y - _offset.y, 0);
  _compositor->addImage(std::move(image), imageTransform, 1.0f, antialiasing);
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
