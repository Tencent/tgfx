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

#include "RasterImage.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "images/RGBAAAImage.h"
#include "images/TextureImage.h"

namespace tgfx {
RasterImage::RasterImage(ResourceKey resourceKey) : resourceKey(std::move(resourceKey)) {
}

std::shared_ptr<Image> RasterImage::makeRasterized(float rasterizationScale) const {
  if (rasterizationScale == 1.0f) {
    return weakThis.lock();
  }
  return Image::makeRasterized(rasterizationScale);
}

std::shared_ptr<Image> RasterImage::makeTextureImage(Context* context) const {
  return TextureImage::MakeFrom(lockTextureProxy(context));
}

std::shared_ptr<TextureProxy> RasterImage::lockTextureProxy(tgfx::Context* context,
                                                            uint32_t renderFlags) const {
  if (context == nullptr) {
    return nullptr;
  }
  return onLockTextureProxy(context, renderFlags);
}

std::shared_ptr<Image> RasterImage::onMakeRGBAAA(int displayWidth, int displayHeight,
                                                 int alphaStartX, int alphaStartY) const {
  if (isAlphaOnly()) {
    return nullptr;
  }
  auto rasterImage = std::static_pointer_cast<RasterImage>(weakThis.lock());
  return RGBAAAImage::MakeFrom(std::move(rasterImage), displayWidth, displayHeight, alphaStartX,
                               alphaStartY);
}

std::unique_ptr<FragmentProcessor> RasterImage::asFragmentProcessor(const ImageFPArgs& args,
                                                                    const Matrix* localMatrix,
                                                                    const Rect*) const {
  auto proxy = lockTextureProxy(args.context, args.renderFlags);
  return TiledTextureEffect::Make(std::move(proxy), args.tileModeX, args.tileModeY, args.sampling,
                                  localMatrix);
}
}  // namespace tgfx
