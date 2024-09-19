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
#include "gpu/ops/FillRectOp.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "images/MipmapImage.h"
#include "images/RGBAAAImage.h"
#include "images/TextureImage.h"

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

std::shared_ptr<TextureProxy> ResourceImage::lockTextureProxy(Context* context,
                                                              uint32_t renderFlags) const {
  if (context == nullptr) {
    return nullptr;
  }
  return onLockTextureProxy(context, uniqueKey, hasMipmaps(), renderFlags);
}

std::shared_ptr<Image> ResourceImage::onMakeMipmapped(bool enabled) const {
  auto source = std::static_pointer_cast<ResourceImage>(weakThis.lock());
  return enabled ? MipmapImage::MakeFrom(std::move(source)) : source;
}

std::shared_ptr<Image> ResourceImage::onMakeRGBAAA(int displayWidth, int displayHeight,
                                                   int alphaStartX, int alphaStartY) const {
  if (isAlphaOnly()) {
    return nullptr;
  }
  auto resourceImage = std::static_pointer_cast<ResourceImage>(weakThis.lock());
  return RGBAAAImage::MakeFrom(std::move(resourceImage), displayWidth, displayHeight, alphaStartX,
                               alphaStartY);
}

std::unique_ptr<FragmentProcessor> ResourceImage::asFragmentProcessor(
    const FPArgs& args, TileMode tileModeX, TileMode tileModeY, const SamplingOptions& sampling,
    const Matrix* uvMatrix) const {
  auto proxy = lockTextureProxy(args.context, args.renderFlags);
  if (proxy == nullptr) {
    return nullptr;
  }
  auto processor = TiledTextureEffect::Make(proxy, tileModeX, tileModeY, sampling, uvMatrix);
  if (isAlphaOnly() && !proxy->isAlphaOnly()) {
    return FragmentProcessor::MulInputByChildAlpha(std::move(processor));
  }
  return processor;
}
}  // namespace tgfx
