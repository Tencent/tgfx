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

#include "Transform3DFilter.h"
#include "core/filters/Transform3DImageFilter.h"

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

Transform3DFilter::Transform3DFilter(const Matrix3D& matrix) : _matrix(matrix) {
}

std::shared_ptr<ImageFilter> Transform3DFilter::onCreateImageFilter(float) {
  return std::make_shared<Transform3DImageFilter>(_matrix);
}

}  // namespace tgfx
