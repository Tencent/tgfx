/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "layers/filters/Transform3DFilter.h"
#include "core/filters/Transform3DImageFilter.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

std::shared_ptr<Transform3DFilter> Transform3DFilter::Make(const Matrix3D& matrix) {
  return std::shared_ptr<Transform3DFilter>(new Transform3DFilter(matrix));
}

void Transform3DFilter::setMatrix(const Matrix3D& matrix) {
  if (_matrix == matrix) {
    return;
  }
  _matrix = matrix;
  invalidateFilter();
}

void Transform3DFilter::setHideBackFace(bool hideBackFace) {
  if (_hideBackFace == hideBackFace) {
    return;
  }
  _hideBackFace = hideBackFace;
  invalidateFilter();
}

Transform3DFilter::Transform3DFilter(const Matrix3D& matrix) : _matrix(matrix) {
}

std::shared_ptr<ImageFilter> Transform3DFilter::onCreateImageFilter(float scale) {
  // The order of model scaling and projection affects the final result. Here, the expected behavior
  // is to project first and then scale. Multiple matrices are modified as follows.
  // For example, if a model is scaled by 2x and then rotated 90 degrees around the X axis, the
  // expected rendering result is that the projection of the scaled model is also enlarged by 2x
  // compared to the unscaled model. However, if the projection is applied to the already scaled
  // model, not only will its length be doubled, but it will also be closer to the observer,
  // resulting in a projection scale much greater than 2.
  auto adjustedMatrix = _matrix;
  if (!FloatNearlyEqual(scale, 1.0f)) {
    DEBUG_ASSERT(!FloatNearlyZero(scale));
    auto invScaleMatrix = Matrix3D::MakeScale(1.0f / scale, 1.0f / scale, 1.0f);
    auto scaleMatrix = Matrix3D::MakeScale(scale, scale, 1.0f);
    adjustedMatrix = scaleMatrix * _matrix * invScaleMatrix;
  }
  auto filter = std::make_shared<Transform3DImageFilter>(adjustedMatrix, _hideBackFace);
  return filter;
}

}  // namespace tgfx
