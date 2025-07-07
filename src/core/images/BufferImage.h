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

#pragma once

#include "ResourceImage.h"

namespace tgfx {
/**
 * BufferImage wraps a fully decoded ImageBuffer that can generate textures on demand.
 */
class BufferImage : public ResourceImage {
 public:
  BufferImage(UniqueKey uniqueKey, std::shared_ptr<ImageBuffer> buffer);

  int width() const override {
    return imageBuffer->width();
  }

  int height() const override {
    return imageBuffer->height();
  }

  bool isAlphaOnly() const override {
    return imageBuffer->isAlphaOnly();
  }

 protected:
  Type type() const override {
    return Type::Buffer;
  }

  std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args,
                                                   const UniqueKey& key) const override;

 private:
  std::shared_ptr<ImageBuffer> imageBuffer = nullptr;
};
}  // namespace tgfx
