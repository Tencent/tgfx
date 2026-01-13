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

#include "Base3DContext.h"
#include "Contour3DContext.h"
#include "Render3DContext.h"
#include "core/Matrix3DUtils.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "layers/compositing3d/Context3DCompositor.h"

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

std::shared_ptr<Base3DContext> Base3DContext::Make(
    bool contourMode, Context* context, const Rect& renderRect, const Point& offset,
    float contentScale, std::shared_ptr<ColorSpace> colorSpace,
    std::shared_ptr<BackgroundContext> backgroundContext) {
  if (contourMode) {
    return std::make_shared<Contour3DContext>(contentScale, std::move(colorSpace));
  }
  auto compositor = std::make_shared<Context3DCompositor>(
      *context, static_cast<int>(renderRect.width()), static_cast<int>(renderRect.height()));
  return std::make_shared<Render3DContext>(std::move(compositor), offset, contentScale,
                                           std::move(colorSpace), std::move(backgroundContext));
}

Canvas* Base3DContext::beginRecording(const Matrix3D& childTransform, bool antialiasing) {
  auto baseTransform = _stateStack.empty() ? Matrix3D::I() : _stateStack.top().transform;
  auto newTransform = childTransform;
  newTransform.postConcat(baseTransform);
  _stateStack.emplace(newTransform, antialiasing);
  auto canvas = _stateStack.top().recorder.beginRecording();

  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
  onBeginRecording(canvas, newTransform);
  canvas->scale(_contentScale, _contentScale);
  return canvas;
}

void Base3DContext::endRecording() {
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

  onEndRecording(std::move(image), imageTransform, pictureOffset, antialiasing);
}

}  // namespace tgfx
