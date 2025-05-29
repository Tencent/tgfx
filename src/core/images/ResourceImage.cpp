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

#include "ResourceImage.h"
#include "core/images/MipmapImage.h"
#include "gpu/ops/RectDrawOp.h"
#include "gpu/processors/AtlasMaskEffect.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
ResourceImage::ResourceImage(UniqueKey uniqueKey) : uniqueKey(std::move(uniqueKey)) {
}

std::shared_ptr<Image> ResourceImage::makeRasterized(float rasterizationScale,
                                                     const SamplingOptions& sampling) const {
  if (rasterizationScale == 1.0f) {
    return weakThis.lock();
  }
  return Image::makeRasterized(rasterizationScale, sampling);
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

PlacementPtr<FragmentProcessor> ResourceImage::asFragmentProcessor(const FPArgs& args,
                                                                   TileMode tileModeX,
                                                                   TileMode tileModeY,
                                                                   const SamplingOptions& sampling,
                                                                   const Matrix* uvMatrix) const {
  TPArgs tpArgs(args.context, args.renderFlags, hasMipmaps());
  auto proxy = onLockTextureProxy(tpArgs, uniqueKey);
  if (args.atlas) {
    return AtlasMaskEffect::Make(std::move(proxy), sampling);
  }
  return TiledTextureEffect::Make(std::move(proxy), tileModeX, tileModeY, sampling, uvMatrix,
                                  isAlphaOnly());
}
}  // namespace tgfx
