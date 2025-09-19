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

#include "tgfx/layers/filters/PerspectiveFilter.h"

namespace tgfx {

std::shared_ptr<PerspectiveFilter> PerspectiveFilter::Make(const LayerPerspectiveInfo& info) {
  return std::shared_ptr<PerspectiveFilter>(new PerspectiveFilter(info));
}

void PerspectiveFilter::setInfo(const LayerPerspectiveInfo& info) {
  if (_info == info) {
    return;
  }

  _info = info;
  invalidateFilter();
}

void PerspectiveFilter::setXRotation(float xRotation) {
  if (_info.xRotation == xRotation) {
    return;
  }

  _info.xRotation = xRotation;
  invalidateFilter();
}

void PerspectiveFilter::setYRotation(float yRotation) {
  if (_info.yRotation == yRotation) {
    return;
  }

  _info.yRotation = yRotation;
  invalidateFilter();
}

void PerspectiveFilter::setZRotation(float zRotation) {
  if (_info.zRotation == zRotation) {
    return;
  }

  _info.zRotation = zRotation;
  invalidateFilter();
}

void PerspectiveFilter::setDepth(float depth) {
  if (_info.depth == depth) {
    return;
  }

  _info.depth = depth;
  invalidateFilter();
}

PerspectiveFilter::PerspectiveFilter(const LayerPerspectiveInfo& info) : _info(info) {
}

std::shared_ptr<ImageFilter> PerspectiveFilter::onCreateImageFilter(float) {
  auto projectType = PerspectiveType::Standard;
  switch (_info.projectType) {
    case LayerPerspectiveType::Standard:
      projectType = PerspectiveType::Standard;
      break;
    case LayerPerspectiveType::CSS:
      projectType = PerspectiveType::CSS;
      break;
    default:
      break;
  }
  return ImageFilter::Perspective(
      {projectType, _info.xRotation, _info.yRotation, _info.zRotation, _info.depth});
}

}  // namespace tgfx