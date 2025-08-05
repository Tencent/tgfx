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

#include "GeneratorImage.h"
#include "DecodedImage.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
GeneratorImage::GeneratorImage(std::shared_ptr<ImageGenerator> generator, bool mipmap)
    : ResourceImage(mipmap), generator(std::move(generator)) {
}

std::shared_ptr<Image> GeneratorImage::onMakeDecoded(Context*, bool tryHardware) const {
  return DecodedImage::MakeFrom(generator, tryHardware, true, mipmap);
}

std::shared_ptr<TextureProxy> GeneratorImage::onLockTextureProxy(const TPArgs& args) const {
  return args.context->proxyProvider()->createTextureProxy({}, generator, args.mipmapped,
                                                           args.renderFlags);
}

std::shared_ptr<Image> GeneratorImage::onCloneWith(bool mipmap) const {
  auto image = std::make_shared<GeneratorImage>(generator, mipmap);
  image->weakThis = image;
  return image;
}

}  // namespace tgfx
