/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "tgfx/layers/Transform3DLayer.h"
#include "filters/Transform3DFilter.h"

namespace tgfx {

std::shared_ptr<Transform3DLayer> Transform3DLayer::Make() {
  return std::shared_ptr<Transform3DLayer>(new Transform3DLayer());
}

void Transform3DLayer::setContent(const std::shared_ptr<Layer>& content) {
  if (_content == content) {
    return;
  }
  _content = content;
  invalidateContent();
}

void Transform3DLayer::setMatrix3D(const Matrix3D& matrix3D) {
  if (_matrix3D == matrix3D) {
    return;
  }
  _matrix3D = matrix3D;
  invalidateContent();
}

void Transform3DLayer::onUpdateContent(LayerRecorder* recorder) {
  if (!_content) {
    return;
  }

  _content->onUpdateContent(recorder);
  auto filters = _content->filters();
  if (_matrix3D != Matrix3D::I()) {
    filters.push_back(Transform3DFilter::Make(_matrix3D));
  }
  setFilters(std::move(filters));
}

}  // namespace tgfx