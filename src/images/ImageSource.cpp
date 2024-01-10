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

#include "ImageSource.h"
#include "BufferSource.h"
#include "EncodedSource.h"
#include "TextureSource.h"
#include "gpu/Texture.h"

namespace tgfx {

std::shared_ptr<ImageSource> ImageSource::MakeFrom(UniqueKey uniqueKey,
                                                   std::shared_ptr<ImageGenerator> generator) {
  if (generator == nullptr) {
    return nullptr;
  }
  auto source =
      std::shared_ptr<EncodedSource>(new EncodedSource(std::move(uniqueKey), std::move(generator)));
  source->weakThis = source;
  return source;
}

std::shared_ptr<ImageSource> ImageSource::MakeFrom(UniqueKey uniqueKey,
                                                   std::shared_ptr<ImageBuffer> buffer) {
  if (buffer == nullptr) {
    return nullptr;
  }
  auto source =
      std::shared_ptr<BufferSource>(new BufferSource(std::move(uniqueKey), std::move(buffer)));
  source->weakThis = source;
  return source;
}

std::shared_ptr<ImageSource> ImageSource::MakeFrom(std::shared_ptr<TextureProxy> textureProxy) {
  if (textureProxy == nullptr) {
    return nullptr;
  }
  auto source = std::shared_ptr<TextureSource>(new TextureSource(std::move(textureProxy)));
  source->weakThis = source;
  return source;
}

ImageSource::ImageSource(UniqueKey uniqueKey) : uniqueKey(std::move(uniqueKey)) {
}

std::shared_ptr<ImageSource> ImageSource::makeTextureSource(Context* context) const {
  auto proxy = lockTextureProxy(context);
  if (proxy == nullptr) {
    return nullptr;
  }
  return MakeFrom(std::move(proxy));
}

std::shared_ptr<ImageSource> ImageSource::makeDecoded(Context* context) const {
  if (!isLazyGenerated()) {
    return std::static_pointer_cast<ImageSource>(weakThis.lock());
  }
  auto source = onMakeDecoded(context);
  if (source == nullptr) {
    return std::static_pointer_cast<ImageSource>(weakThis.lock());
  }
  source->weakThis = source;
  return source;
}

std::shared_ptr<ImageSource> ImageSource::onMakeDecoded(Context*) const {
  return nullptr;
}

std::shared_ptr<ImageSource> ImageSource::makeMipMapped() const {
  if (hasMipmaps()) {
    return std::static_pointer_cast<ImageSource>(weakThis.lock());
  }
  auto source = onMakeMipMapped();
  if (source == nullptr) {
    return std::static_pointer_cast<ImageSource>(weakThis.lock());
  }
  source->weakThis = source;
  return source;
}

std::shared_ptr<TextureProxy> ImageSource::lockTextureProxy(Context* context,
                                                            uint32_t renderFlags) const {
  if (context == nullptr) {
    return nullptr;
  }
  return onMakeTextureProxy(context, renderFlags);
}
}  // namespace tgfx
