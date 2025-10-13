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

#include "core/images/PixelImage.h"
#include "gpu/resources/ResourceKey.h"

namespace tgfx {
/**
 * RasterizedImage is an image that rasterizes another image and stores the result as a GPU texture
 * for repeated rendering.
 */
class RasterizedImage : public Image {
 public:
  RasterizedImage(UniqueKey uniqueKey, std::shared_ptr<Image> source);

  int width() const override {
    return source->width();
  }

  int height() const override {
    return source->height();
  }

  bool isAlphaOnly() const override {
    return source->isAlphaOnly();
  }

  bool isFullyDecoded() const override {
    return source->isFullyDecoded();
  }

  bool hasMipmaps() const override {
    return source->hasMipmaps();
  }

  std::shared_ptr<Image> makeRasterized() const override;

 protected:
  Type type() const override {
    return Type::Rasterized;
  }

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

  std::shared_ptr<Image> onMakeScaled(int newWidth, int newHeight,
                                      const SamplingOptions& sampling) const override;

  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

 private:
  UniqueKey getTextureKey(float cacheScale = 1.0f) const;

  UniqueKey uniqueKey;

  std::shared_ptr<Image> source = nullptr;
};
}  // namespace tgfx
