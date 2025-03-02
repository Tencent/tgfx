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

#include "GeneratorImage.h"
#include "DecodedImage.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<Image> Image::MakeFrom(std::shared_ptr<ImageGenerator> generator) {
  if (generator == nullptr) {
    return nullptr;
  }
  auto image = std::make_shared<GeneratorImage>(UniqueKey::Make(), std::move(generator));
  image->weakThis = image;
  return image;
}

GeneratorImage::GeneratorImage(UniqueKey uniqueKey, std::shared_ptr<ImageGenerator> generator)
    : ResourceImage(std::move(uniqueKey)), generator(std::move(generator)) {
}

std::shared_ptr<Image> GeneratorImage::onMakeDecoded(Context* context, bool tryHardware) const {
  if (context != nullptr) {
    auto proxy = context->proxyProvider()->findProxy(uniqueKey);
    if (proxy != nullptr && proxy->getUniqueKey() == uniqueKey) {
      return nullptr;
    }
    if (context->resourceCache()->hasUniqueResource(uniqueKey)) {
      return nullptr;
    }
  }
  return DecodedImage::MakeFrom(uniqueKey, generator, tryHardware, true);
}

std::shared_ptr<TextureProxy> GeneratorImage::onLockTextureProxy(const TPArgs& args,
                                                                 const UniqueKey& key) const {
  return args.context->proxyProvider()->createTextureProxy(key, generator, args.mipmapped,
                                                           args.renderFlags);
}
}  // namespace tgfx
