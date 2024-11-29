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

#include "core/images/ResourceImage.h"

namespace tgfx {
class MipmapImage : public ResourceImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<ResourceImage> source);

  int width() const override {
    return source->width();
  }

  int height() const override {
    return source->height();
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool isYUV() const override {
    return source->isYUV();
  }

  bool hasMipmaps() const override {
    return true;
  }

  bool isFlat() const override {
    return source->isFlat();
  }

 protected:
  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args,
                                                   const UniqueKey& key) const override;

 private:
  std::shared_ptr<ResourceImage> source = nullptr;

  MipmapImage(UniqueKey uniqueKey, std::shared_ptr<ResourceImage> source);
};
}  // namespace tgfx
