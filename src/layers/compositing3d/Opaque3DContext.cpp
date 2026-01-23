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
#include "tgfx/core/ImageFilter.h"
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
                                    const Point&, bool) {
  _opaqueImages.push_back({std::move(image), imageTransform});
}

void Opaque3DContext::finishAndDrawTo(Canvas* canvas, bool antialiasing) {
  auto invScale = 1.0f / _contentScale;
  Paint paint = {};
  paint.setAntiAlias(antialiasing);
  for (const auto& entry : _opaqueImages) {
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
