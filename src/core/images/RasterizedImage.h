/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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
/**
 * RasterizedImage is an image that rasterizes another image with a scale and sampling options.
 */
class RasterizedImage : public ResourceImage {
 public:
  /**
   * Note that this method always returns a non-mipmapped image.
   */
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> source, float rasterizationScale,
                                         const SamplingOptions& sampling);

  int width() const override;

  int height() const override;

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool isFullyDecoded() const override {
    return source->isFullyDecoded();
  }

  std::shared_ptr<Image> makeRasterized(float rasterizationScale = 1.0f,
                                        const SamplingOptions& sampling = {}) const override;

 protected:
  Type type() const override {
    return Type::Rasterized;
  }

  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

  std::shared_ptr<TextureProxy> onLockTextureProxy(const TPArgs& args,
                                                   const UniqueKey& key) const final;

 private:
  std::shared_ptr<Image> source = nullptr;
  float rasterizationScale = 1.0f;
  SamplingOptions sampling = {};

  RasterizedImage(UniqueKey uniqueKey, std::shared_ptr<Image> source, float rasterizationScale,
                  const SamplingOptions& sampling);
};
}  // namespace tgfx
