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
#include "core/Matrix3DUtils.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/Canvas.h"
#include "tgfx/core/Paint.h"

namespace tgfx {

Opaque3DContext::Opaque3DContext(const Rect& renderRect, float contentScale,
                                 std::shared_ptr<ColorSpace> colorSpace)
    : Layer3DContext(renderRect, contentScale, std::move(colorSpace)) {
}

OpaqueContext* Opaque3DContext::currentOpaqueContext() {
  return _opaqueStack.empty() ? nullptr : &_opaqueStack.top();
}

Canvas* Opaque3DContext::onBeginRecording() {
  _opaqueStack.emplace();
  return _opaqueStack.top().beginRecording();
}

std::shared_ptr<Picture> Opaque3DContext::onFinishRecording() {
  if (_opaqueStack.empty()) {
    return nullptr;
  }
  auto picture = _opaqueStack.top().finishRecordingAsPicture();
  _opaqueStack.pop();
  return picture;
}

void Opaque3DContext::onImageReady(std::shared_ptr<Image> image, const Matrix3D& imageTransform,
                                   const Point&, int, bool, Layer*) {
  _opaqueImages.push_back({std::move(image), imageTransform});
}

void Opaque3DContext::finishAndDrawTo(Canvas* canvas, bool antialiasing) {
  Paint paint = {};
  paint.setAntiAlias(antialiasing);
  AutoCanvasRestore outerRestore(canvas);
  // The imageMatrix already contains the scale needed for drawing the image to the canvas.
  // Apply inverse scale to cancel out the canvas's existing scale transformation.
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  canvas->concat(Matrix::MakeScale(1.0f / _contentScale, 1.0f / _contentScale));
  for (const auto& entry : _opaqueImages) {
    AutoCanvasRestore autoRestore(canvas);
    canvas->concat(entry.transform.asMatrix());
    canvas->drawImage(entry.image, &paint);
  }
}

}  // namespace tgfx
