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

#pragma once

#include "GeneratorImage.h"
#include "core/ImageDecoder.h"

namespace tgfx {
/**
 * DecoderImage wraps an ImageDecoder that can decode ImageBuffers asynchronously.
 */
class DecoderImage : public TextureImage {
 public:
  static std::shared_ptr<Image> MakeFrom(ResourceKey resourceKey,
                                         std::shared_ptr<ImageDecoder> decoder);

  int width() const override {
    return decoder->width();
  }

  int height() const override {
    return decoder->height();
  }

  bool isAlphaOnly() const override {
    return decoder->isAlphaOnly();
  }

 protected:
  std::shared_ptr<TextureProxy> onLockTextureProxy(Context* context, const ResourceKey& key,
                                                   bool mipmapped,
                                                   uint32_t renderFlags) const override;

 private:
  std::shared_ptr<ImageDecoder> decoder = nullptr;

  DecoderImage(ResourceKey resourceKey, std::shared_ptr<ImageDecoder> decoder);
};
}  // namespace tgfx
