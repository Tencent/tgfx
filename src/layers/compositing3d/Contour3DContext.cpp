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
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"
#include "tgfx/core/ImageFilter.h"

namespace tgfx {

void Contour3DContext::onBeginRecording(Canvas*, const Matrix3D&) {
  // No clipping needed for contour mode
}

void Contour3DContext::onEndRecording(std::shared_ptr<Image> image, const Matrix3D& transform,
                                      const Point&, bool) {
  _contourImages.push_back({std::move(image), transform});
}

void Contour3DContext::finishAndDrawTo(Canvas* canvas, bool antialiasing) {
  DEBUG_ASSERT(!FloatNearlyZero(_contentScale));
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
