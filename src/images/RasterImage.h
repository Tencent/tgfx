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

#include "images/ResourceImage.h"
#include "tgfx/core/SamplingOptions.h"

namespace tgfx {
/**
 * RasterImage takes an Image and rasterizes it to a new Image with a different scale and sampling
 * options.
 */
class RasterImage : public ResourceImage {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, bool mipmapped = false,
                                         const SamplingOptions& sampling = {});

  int width() const override;

  int height() const override;

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool isFullyDecoded() const override {
    return source->isFullyDecoded();
  }

 protected:
  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

  std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args) const override;

 private:
  std::shared_ptr<Image> source = nullptr;
  SamplingOptions sampling = {};

  RasterImage(UniqueKey uniqueKey, std::shared_ptr<Image> source, const SamplingOptions& sampling);
};
}  // namespace tgfx
