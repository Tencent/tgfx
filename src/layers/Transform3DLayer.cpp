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
#include "RegionTransformer.h"
#include "tgfx/layers/filters/Transform3DFilter.h"

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

void Transform3DLayer::setHideBackFace(bool hideBackFace) {
  if (_hideBackFace == hideBackFace) {
    return;
  }
  _hideBackFace = hideBackFace;
  invalidateContent();
}

void Transform3DLayer::onUpdateContent(LayerRecorder* recorder) {
  if (!_content) {
    return;
  }

  _content->onUpdateContent(recorder);
}

void Transform3DLayer::onUpdateContentBounds(Rect& bounds, float contentScale) const {
  if (_matrix3D == Matrix3D::I()) {
    return;
  }

  auto filter = Transform3DFilter::Make(_matrix3D);
  auto transformer = RegionTransformer::MakeFromFilters({std::move(filter)}, contentScale, nullptr);
  transformer->transform(&bounds);
}

bool Transform3DLayer::needDrawOffScreen(const DrawArgs& args, float alpha,
                                         BlendMode blendMode) const {
  if (_matrix3D == Matrix3D::I()) {
    return Layer::needDrawOffScreen(args, alpha, blendMode);
  }

  // Currently, 3D layers only support applying 3D transformation effects uniformly to the content
  // of this node and all its child elements, so offscreen rendering is required.
  return true;
}

void Transform3DLayer::onMakeImageFilters(std::vector<std::shared_ptr<ImageFilter>>& filters,
                                          float contentScale) const {
  if (_matrix3D == Matrix3D::I()) {
    return;
  }

  auto filter = Transform3DFilter::Make(_matrix3D);
  filter->setHideBackFace(_hideBackFace);
  auto imageFilter = filter->getImageFilter(contentScale);
  filters.push_back(std::move(imageFilter));
}

}  // namespace tgfx