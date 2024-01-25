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
#include "gpu/processors/TiledTextureEffect.h"
#include "images/RGBAAAImage.h"
#include "images/TextureImage.h"

namespace tgfx {

ResourceImage::ResourceImage(ResourceKey resourceKey) : resourceKey(std::move(resourceKey)) {
}

std::shared_ptr<Image> ResourceImage::makeTextureImage(Context* context) const {
  auto proxy = onLockTextureProxy(context, 0);
  return TextureImage::MakeFrom(std::move(proxy));
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

std::unique_ptr<FragmentProcessor> ResourceImage::asFragmentProcessor(const ImageFPArgs& args,
                                                                      const Matrix* localMatrix,
                                                                      const Rect*) const {
  auto proxy = onLockTextureProxy(args.context, args.renderFlags);
  return TiledTextureEffect::Make(std::move(proxy), args.tileModeX, args.tileModeY, args.sampling,
                                  localMatrix);
}
}  // namespace tgfx
