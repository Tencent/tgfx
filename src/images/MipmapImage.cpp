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

#include "MipmapImage.h"

namespace tgfx {
std::shared_ptr<Image> MipmapImage::MakeFrom(std::shared_ptr<RasterImage> source) {
  if (source == nullptr) {
    return nullptr;
  }
  auto image =
      std::shared_ptr<MipmapImage>(new MipmapImage(ResourceKey::NewWeak(), std::move(source)));
  image->weakThis = image;
  return image;
}

MipmapImage::MipmapImage(ResourceKey resourceKey, std::shared_ptr<RasterImage> source)
    : RasterImage(std::move(resourceKey)), source(std::move(source)) {
}

std::shared_ptr<Image> MipmapImage::makeRasterized(float rasterizationScale,
                                                   SamplingOptions sampling) const {
  auto newSource =
      std::static_pointer_cast<RasterImage>(source->makeRasterized(rasterizationScale, sampling));
  if (newSource == source) {
    return weakThis.lock();
  }
  return MipmapImage::MakeFrom(std::move(newSource));
}

std::shared_ptr<Image> MipmapImage::onMakeDecoded(Context* context, bool) const {
  auto newSource = std::static_pointer_cast<RasterImage>(source->onMakeDecoded(context, false));
  if (newSource == nullptr) {
    return nullptr;
  }
  auto newImage = std::shared_ptr<MipmapImage>(new MipmapImage(resourceKey, std::move(newSource)));
  newImage->weakThis = newImage;
  return newImage;
}

std::shared_ptr<Image> MipmapImage::onMakeMipmapped(bool enabled) const {
  return enabled ? weakThis.lock() : source;
}

std::shared_ptr<TextureProxy> MipmapImage::onLockTextureProxy(Context* context,
                                                              const ResourceKey& key, bool,
                                                              uint32_t renderFlags) const {
  return source->onLockTextureProxy(context, key, true, renderFlags);
}
}  // namespace tgfx
