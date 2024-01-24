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

#include "ResourceImage.h"

namespace tgfx {
/**
 * BufferImage wraps a fully decoded ImageBuffer that can generate textures on demand.
 */
class BufferImage : public ResourceImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<ImageBuffer> buffer,
                                         bool mipMapped = false);

  int width() const override {
    return imageBuffer->width();
  }

  int height() const override {
    return imageBuffer->height();
  }

  bool hasMipmaps() const override {
    return mipMapped;
  }

  bool isAlphaOnly() const override {
    return imageBuffer->isAlphaOnly();
  }

 protected:
  std::shared_ptr<Image> onMakeMipMapped() const override;

  std::shared_ptr<TextureProxy> onLockTextureProxy(Context* context,
                                                   uint32_t renderFlags) const override;

 private:
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
  bool mipMapped = false;

  BufferImage(ResourceKey resourceKey, std::shared_ptr<ImageBuffer> buffer, bool mipMapped = false);
};
}  // namespace tgfx
