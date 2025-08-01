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

#include "ResourceImage.h"
#include "core/images/MipmapImage.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
ResourceImage::ResourceImage(UniqueKey uniqueKey) : uniqueKey(std::move(uniqueKey)) {
}

std::shared_ptr<Image> ResourceImage::makeRasterized() const {
  return std::static_pointer_cast<Image>(weakThis.lock());
}

std::shared_ptr<TextureProxy> ResourceImage::lockTextureProxy(const TPArgs& args) const {
  auto newArgs = args;
  // ResourceImage has preset mipmaps.
  newArgs.mipmapped = hasMipmaps();
  return onLockTextureProxy(newArgs, uniqueKey);
}

std::shared_ptr<Image> ResourceImage::onMakeMipmapped(bool enabled) const {
  auto source = std::static_pointer_cast<ResourceImage>(weakThis.lock());
  return enabled ? MipmapImage::MakeFrom(std::move(source)) : source;
}

std::shared_ptr<Image> ResourceImage::onMakeScaled(int newWidth, int newHeight,
                                                   const SamplingOptions& sampling) const {
  auto result = Image::onMakeScaled(newWidth, newHeight, sampling);
  return result->makeRasterized();
}

PlacementPtr<FragmentProcessor> ResourceImage::asFragmentProcessor(const FPArgs& args,
                                                                   const SamplingArgs& samplingArgs,
                                                                   const Matrix* uvMatrix) const {

  TPArgs tpArgs(args.context, args.renderFlags, hasMipmaps());
  auto proxy = onLockTextureProxy(tpArgs, uniqueKey);
  return TiledTextureEffect::Make(std::move(proxy), samplingArgs, uvMatrix, isAlphaOnly());
}
}  // namespace tgfx
