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

#include "FilterImage.h"
#include "SubsetImage.h"
#include "core/utils/AddressOf.h"
#include "core/utils/NeedMipmaps.h"
#include "gpu/OpContext.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
std::shared_ptr<Image> FilterImage::MakeFrom(std::shared_ptr<Image> source,
                                             std::shared_ptr<ImageFilter> filter, Point* offset,
                                             const Rect* clipRect) {
  if (source == nullptr) {
    return nullptr;
  }
  if (filter == nullptr) {
    return source;
  }
  auto bounds = Rect::MakeWH(source->width(), source->height());
  bounds = filter->filterBounds(bounds);
  if (bounds.isEmpty()) {
    return nullptr;
  }
  if (clipRect != nullptr) {
    if (!bounds.intersect(*clipRect)) {
      return nullptr;
    }
    bounds.roundOut();
  }
  if (offset != nullptr) {
    offset->x = bounds.left;
    offset->y = bounds.top;
  }
  return Wrap(std::move(source), bounds, std::move(filter));
}

std::shared_ptr<Image> FilterImage::Wrap(std::shared_ptr<Image> source, const Rect& bounds,
                                         std::shared_ptr<ImageFilter> filter) {
  auto image =
      std::shared_ptr<FilterImage>(new FilterImage(std::move(source), bounds, std::move(filter)));
  image->weakThis = image;
  return image;
}

FilterImage::FilterImage(std::shared_ptr<Image> source, const Rect& bounds,
                         std::shared_ptr<ImageFilter> filter)
    : SubsetImage(std::move(source), bounds), filter(std::move(filter)) {
}

std::shared_ptr<Image> FilterImage::onCloneWith(std::shared_ptr<Image> newSource) const {
  return FilterImage::Wrap(std::move(newSource), bounds, filter);
}

std::shared_ptr<Image> FilterImage::onMakeSubset(const Rect& subset) const {
  auto newBounds = subset;
  newBounds.offset(bounds.x(), bounds.y());
  return FilterImage::Wrap(source, newBounds, filter);
}

std::shared_ptr<Image> FilterImage::onMakeWithFilter(std::shared_ptr<ImageFilter> imageFilter,
                                                     Point* offset, const Rect* clipRect) const {
  if (imageFilter == nullptr) {
    return nullptr;
  }
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  if (filter->filterBounds(inputBounds) != bounds) {
    return FilterImage::MakeFrom(weakThis.lock(), std::move(imageFilter), offset, clipRect);
  }
  auto filterBounds = imageFilter->filterBounds(Rect::MakeWH(width(), height()));
  if (filterBounds.isEmpty()) {
    return nullptr;
  }
  bool hasClip = false;
  if (clipRect != nullptr) {
    auto oldBounds = filterBounds;
    if (!filterBounds.intersect(*clipRect)) {
      return nullptr;
    }
    filterBounds.roundOut();
    hasClip = (filterBounds != oldBounds);
  }
  if (offset != nullptr) {
    offset->x = filterBounds.left;
    offset->y = filterBounds.top;
  }
  if (hasClip) {
    return FilterImage::Wrap(weakThis.lock(), filterBounds, std::move(imageFilter));
  }
  filterBounds.offset(bounds.x(), bounds.y());
  auto composeFilter = ImageFilter::Compose(filter, std::move(imageFilter));
  return FilterImage::Wrap(source, filterBounds, std::move(composeFilter));
}

std::shared_ptr<TextureProxy> FilterImage::lockTextureProxy(const TPArgs& args) const {
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  auto filterBounds = filter->filterBounds(inputBounds);
  return filter->lockTextureProxy(source, filterBounds, args);
}

std::unique_ptr<FragmentProcessor> FilterImage::asFragmentProcessor(const FPArgs& args,
                                                                    TileMode tileModeX,
                                                                    TileMode tileModeY,
                                                                    const SamplingOptions& sampling,
                                                                    const Matrix* uvMatrix) const {
  auto fpMatrix = concatUVMatrix(uvMatrix);
  auto inputBounds = Rect::MakeWH(source->width(), source->height());
  auto drawBounds = args.drawRect;
  if (fpMatrix) {
    drawBounds = fpMatrix->mapRect(drawBounds);
  }
  auto clipBounds = bounds;
  if (!clipBounds.intersect(drawBounds)) {
    return nullptr;
  }
  Rect dstBounds = Rect::MakeEmpty();
  if (!filter->applyCropRect(inputBounds, &dstBounds, &clipBounds)) {
    return nullptr;
  }
  if (dstBounds.contains(drawBounds)) {
    return filter->asFragmentProcessor(source, args, sampling, AddressOf(fpMatrix));
  }
  auto mipmapped =
      source->hasMipmaps() && NeedMipmaps(sampling, args.viewMatrix, AddressOf(fpMatrix));
  TPArgs tpArgs(args.context, args.renderFlags, mipmapped);
  auto textureProxy = filter->lockTextureProxy(source, dstBounds, tpArgs);
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto matrix = Matrix::MakeTrans(-dstBounds.x(), -dstBounds.y());
  if (fpMatrix) {
    matrix.preConcat(*fpMatrix);
  }
  return TiledTextureEffect::Make(textureProxy, tileModeX, tileModeY, sampling, &matrix,
                                  source->isAlphaOnly());
}
}  // namespace tgfx
