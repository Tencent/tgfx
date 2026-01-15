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

#include "Contour3DContext.h"
#include "tgfx/core/ImageFilter.h"
#include "tgfx/core/Paint.h"

namespace tgfx {

Contour3DContext::Contour3DContext(const Rect& renderRect, float contentScale,
                                   std::shared_ptr<ColorSpace> colorSpace)
    : Layer3DContext(renderRect, contentScale, std::move(colorSpace)) {
}

ContourContext* Contour3DContext::currentContourContext() {
  return _contourStack.empty() ? nullptr : &_contourStack.top();
}

Canvas* Contour3DContext::onBeginRecording() {
  _contourStack.emplace();
  return _contourStack.top().beginRecording();
}

std::shared_ptr<Picture> Contour3DContext::onFinishRecording() {
  if (_contourStack.empty()) {
    return nullptr;
  }
  auto picture = _contourStack.top().finishRecordingAsPicture();
  _contourStack.pop();
  return picture;
}

void Contour3DContext::onImageReady(std::shared_ptr<Image> image, const Matrix3D& imageTransform,
                                    const Point&, bool) {
  _contourImages.push_back({std::move(image), imageTransform});
}

void Contour3DContext::finishAndDrawTo(Canvas* canvas, bool antialiasing) {
  auto invScale = 1.0f / _contentScale;
  Paint paint = {};
  paint.setAntiAlias(antialiasing);
  for (const auto& entry : _contourImages) {
    auto imageFilter = ImageFilter::Transform3D(entry.transform);
    auto offset = Point::Zero();
    auto transformedImage = entry.image->makeWithFilter(imageFilter, &offset);
    if (transformedImage == nullptr) {
      continue;
    }
    canvas->drawImage(transformedImage, offset.x * invScale, offset.y * invScale, &paint);
  }
}

}  // namespace tgfx
