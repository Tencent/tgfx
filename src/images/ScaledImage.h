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

#include "images/RasterImage.h"

namespace tgfx {
class ScaledImage : public RasterImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source,
                                         float rasterizationScale = 1.0f);

  int width() const override;

  int height() const override;

  bool hasMipmaps() const override {
    return source->hasMipmaps();
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool isLazyGenerated() const override {
    return source->isLazyGenerated();
  }

  std::shared_ptr<Image> makeRasterized(float rasterizationScale = 1.0f) const override;

 protected:
  std::shared_ptr<Image> onMakeDecoded(Context* context) const override;

  std::shared_ptr<Image> onMakeMipMapped() const override;

  std::shared_ptr<TextureProxy> onLockTextureProxy(Context* context,
                                                   uint32_t renderFlags) const override;

 private:
  std::shared_ptr<Image> source = nullptr;
  float rasterizationScale = 1.0f;

  ScaledImage(ResourceKey resourceKey, std::shared_ptr<Image> source, float rasterizationScale);
};
}  // namespace tgfx
