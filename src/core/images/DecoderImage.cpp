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

#include "DecoderImage.h"
#include "BufferImage.h"
#include "gpu/ProxyProvider.h"

namespace tgfx {
std::shared_ptr<Image> DecoderImage::MakeFrom(UniqueKey uniqueKey,
                                              std::shared_ptr<ImageDecoder> decoder) {
  TRACE_EVENT;
  if (decoder == nullptr) {
    return nullptr;
  }
  auto image =
      std::shared_ptr<DecoderImage>(new DecoderImage(std::move(uniqueKey), std::move(decoder)));
  image->weakThis = image;
  return image;
}

DecoderImage::DecoderImage(UniqueKey uniqueKey, std::shared_ptr<ImageDecoder> decoder)
    : ResourceImage(std::move(uniqueKey)), decoder(std::move(decoder)) {
}

std::shared_ptr<TextureProxy> DecoderImage::onLockTextureProxy(const TPArgs& args) const {
  TRACE_EVENT;
  return args.context->proxyProvider()->createTextureProxy(args.uniqueKey, decoder, args.mipmapped,
                                                           args.renderFlags);
}
}  // namespace tgfx
