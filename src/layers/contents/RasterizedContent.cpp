/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "RasterizedContent.h"

namespace tgfx {
void RasterizedContent::draw(Canvas* canvas, bool antiAlias, float alpha, BlendMode blendMode,
                             const Matrix3D* transform) const {
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(matrix);
  Paint paint = {};
  paint.setAntiAlias(antiAlias);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  if (transform != nullptr) {
    paint.setImageFilter(ImageFilter::Transform3D(*transform));
  }
  canvas->drawImage(image, &paint);
  canvas->setMatrix(oldMatrix);
}
}  // namespace tgfx
