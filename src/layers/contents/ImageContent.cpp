/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "ImageContent.h"

namespace tgfx {
Rect ImageContent::getBounds() const {
  return Rect::MakeXYWH(0, 0, image->width(), image->height());
}

void ImageContent::draw(Canvas* canvas, const Paint& paint) const {
  canvas->drawImage(image, sampling, &paint);
}

bool ImageContent::hitTestPoint(float localX, float localY, bool /*pixelHitTest*/) {
  // The pixelHitTest flag is ignored because we cannot read pixels from images before they are drawn.

  const Rect imageBounds = Rect::MakeXYWH(0, 0, image->width(), image->height());
  return imageBounds.contains(localX, localY);
}
}  // namespace tgfx
