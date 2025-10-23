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

Rect ComposeImageFilter::onGetOutputBounds(const Rect& inputRect) const {
  auto bounds = inputRect;
  for (auto& filter : filters) {
    bounds = filter->filterBounds(bounds);
  }
  return bounds;
}

Rect ComposeImageFilter::onGetInputBounds(const Rect& outputRect) const {
  auto bounds = outputRect;
  for (auto it = filters.rbegin(); it != filters.rend(); ++it) {
    bounds = (*it)->filterBounds(bounds, Reverse);
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
    lastSource = FilterImage::MakeFrom(std::move(lastSource), filter, &offset);
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
