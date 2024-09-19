/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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
#include "gpu/OpContext.h"
#include "images/TextureImage.h"

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
  if (inner->isComposeFilter()) {
    auto innerFilters = static_cast<ComposeImageFilter*>(inner.get())->filters;
    filters = innerFilters;
  } else {
    filters.push_back(std::move(inner));
  }
  if (outer->isComposeFilter()) {
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

Rect ComposeImageFilter::onFilterBounds(const Rect& srcRect) const {
  auto bounds = srcRect;
  for (auto& filter : filters) {
    bounds = filter->filterBounds(bounds);
  }
  return bounds;
}

std::unique_ptr<FragmentProcessor> ComposeImageFilter::asFragmentProcessor(
    std::shared_ptr<Image> source, const FPArgs& args, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  auto bounds = Rect::MakeWH(source->width(), source->height());
  auto drawBounds = args.drawRect;
  if (uvMatrix) {
    drawBounds = uvMatrix->mapRect(drawBounds);
  }
  bool hasMipmaps = source->hasMipmaps() && sampling.mipmapMode != MipmapMode::None;
  auto count = filters.size() - 1;
  auto lastSource = source;
  Point lastOffset = {0, 0};
  for (size_t i = 0; i < count; ++i) {
    auto& filter = filters[i];
    if (!filter->applyCropRect(bounds, &bounds, &drawBounds)) {
      return nullptr;
    }
    bool mipmapped = (i == count - 1) && hasMipmaps;
    auto textureProxy =
        filter->onFilterImage(args.context, lastSource, bounds, mipmapped, args.renderFlags);
    if (!textureProxy) {
      return nullptr;
    }
    lastSource = TextureImage::Wrap(std::move(textureProxy));
    lastOffset.offset(bounds.x(), bounds.y());
  }
  auto matrix = Matrix::MakeTrans(-lastOffset.x, -lastOffset.y);
  if (uvMatrix) {
    matrix.preConcat(*uvMatrix);
  }
  return filters.back()->asFragmentProcessor(std::move(lastSource), args, sampling, &matrix);
}
}  // namespace tgfx
