/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "ComposeImageFilter.h"
#include "core/filters/Transform3DImageFilter.h"
#include "core/images/FilterImage.h"

namespace tgfx {
std::shared_ptr<ImageFilter> ImageFilter::Compose(std::shared_ptr<ImageFilter> inner,
                                                  std::shared_ptr<ImageFilter> outer) {
  if (outer == nullptr && inner == nullptr) {
    return nullptr;
  }
  if (outer == nullptr) {
    return inner;
  }
  if (inner == nullptr) {
    return outer;
  }
  std::vector<std::shared_ptr<ImageFilter>> filters = {};
  if (inner->type() == Type::Compose) {
    auto innerFilters = static_cast<ComposeImageFilter*>(inner.get())->filters;
    filters = innerFilters;
  } else {
    filters.push_back(std::move(inner));
  }
  if (outer->type() == Type::Compose) {
    auto outerFilters = static_cast<ComposeImageFilter*>(outer.get())->filters;
    filters.insert(filters.end(), outerFilters.begin(), outerFilters.end());
  } else {
    filters.push_back(std::move(outer));
  }
  return std::make_shared<ComposeImageFilter>(std::move(filters));
}

std::shared_ptr<ImageFilter> ImageFilter::Compose(
    std::vector<std::shared_ptr<ImageFilter>> filters) {
  if (filters.empty()) {
    return nullptr;
  }
  if (filters.size() == 1) {
    return filters[0];
  }
  return std::make_shared<ComposeImageFilter>(std::move(filters));
}

ComposeImageFilter::ComposeImageFilter(std::vector<std::shared_ptr<ImageFilter>> filters)
    : filters(std::move(filters)) {
}

Rect ComposeImageFilter::onFilterBounds(const Rect& rect, MapDirection mapDirection) const {
  auto bounds = rect;
  if (mapDirection == MapDirection::Forward) {
    for (auto& filter : filters) {
      bounds = filter->filterBounds(bounds);
    }
    return bounds;
  }
  for (auto it = filters.rbegin(); it != filters.rend(); ++it) {
    bounds = (*it)->filterBounds(bounds, mapDirection);
  }
  return bounds;
}

PlacementPtr<FragmentProcessor> ComposeImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    SrcRectConstraint constraint, const Matrix* uvMatrix) const {
  auto lastSource = source;
  Point lastOffset = {};
  for (auto& filter : filters) {
    Point offset = {};
    if (filter->type() == Type::Transform3D) {
      // The filter's matrix is defined with the anchor point at (0, 0) of the input image's
      // top-left corner in the filter group. However, when rendering with a 3D filter, there is no
      // concept of X and Y coordinates, only width and height definitions. This means it processes
      // an image with lastOffset as the origin, and the width and height of lastSource as its
      // dimensions. In this case, the transformation anchor point of the filter in the image
      // coordinate system becomes -lastOffset, instead of (0, 0). The matrix needs to be
      // recalculated based on this anchor point, and a new image filter must be constructed.
      auto transform3DFilter = static_cast<Transform3DImageFilter*>(filter.get());
      Matrix3D matrix = transform3DFilter->matrix();
      auto offsetMatrix = Matrix3D::MakeTranslate(lastOffset.x, lastOffset.y, 0);
      auto invOffsetMatrix = Matrix3D::MakeTranslate(-lastOffset.x, -lastOffset.y, 0);
      auto adjustedMatrix3D = invOffsetMatrix * matrix * offsetMatrix;
      auto newMatrixFilter =
          ImageFilter::Transform3D(adjustedMatrix3D, transform3DFilter->hideBackFace());
      lastSource = FilterImage::MakeFrom(std::move(lastSource), newMatrixFilter, &offset);
    } else {
      lastSource = FilterImage::MakeFrom(std::move(lastSource), filter, &offset);
    }
    if (!lastSource) {
      return nullptr;
    }
    lastOffset.offset(offset.x, offset.y);
  }
  auto matrix = Matrix::MakeTrans(-lastOffset.x, -lastOffset.y);
  if (uvMatrix) {
    matrix.preConcat(*uvMatrix);
  }
  return FragmentProcessor::Make(std::move(lastSource), args, sampling, constraint, &matrix);
}
}  // namespace tgfx
