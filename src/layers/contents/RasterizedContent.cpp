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
void RasterizedContent::draw(Canvas* canvas, bool antiAlias, float alpha,
                             const std::shared_ptr<MaskFilter>& mask, BlendMode blendMode,
                             const Matrix3D* transform) const {
  auto oldMatrix = canvas->getMatrix();
  canvas->concat(matrix);
  Paint paint = {};
  paint.setAntiAlias(antiAlias);
  paint.setAlpha(alpha);
  paint.setBlendMode(blendMode);
  if (mask) {
    auto invertMatrix = Matrix::I();
    if (matrix.invert(&invertMatrix)) {
      paint.setMaskFilter(mask->makeWithMatrix(invertMatrix));
    }
  }
  if (transform == nullptr) {
    canvas->drawImage(image, &paint);
  } else {
    // Transform describes a transformation based on the layer's coordinate system, but the
    // rasterized content is only a small sub-rectangle within the layer. We need to calculate an
    // equivalent affine transformation matrix referenced to the local coordinate system with the
    // top-left vertex of this sub-rectangle as the origin.
    auto adaptedMatrix = *transform;
    auto offsetMatrix = Matrix3D::MakeTranslate(matrix.getTranslateX(), matrix.getTranslateY(), 0);
    auto invOffsetMatrix =
        Matrix3D::MakeTranslate(-matrix.getTranslateX(), -matrix.getTranslateY(), 0);
    auto scaleMatrix = Matrix3D::MakeScale(matrix.getScaleX(), matrix.getScaleY(), 1.0f);
    auto invScaleMatrix =
        Matrix3D::MakeScale(1.0f / matrix.getScaleX(), 1.0f / matrix.getScaleY(), 1.0f);
    adaptedMatrix = invScaleMatrix * invOffsetMatrix * adaptedMatrix * offsetMatrix * scaleMatrix;
    // Layer visibility is handled in the CPU stage, update the matrix to keep the Z-axis of
    // vertices sent to the GPU at 0.
    adaptedMatrix.setRow(2, {0, 0, 0, 0});
    auto imageFilter = ImageFilter::Transform3D(adaptedMatrix);
    auto offSet = Point();
    auto filteredeImage = image->makeWithFilter(imageFilter, &offSet);
    canvas->concat(Matrix::MakeTrans(offSet.x, offSet.y));
    canvas->drawImage(filteredeImage, &paint);
  }
  canvas->setMatrix(oldMatrix);
}
}  // namespace tgfx
