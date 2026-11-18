/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/core/ImageProvider.h"

namespace tgfx {
/**
 * ProviderImage displays the variant supplied by an ImageProvider for the draw scale. Its size and
 * attributes come from the provider and never depend on the draw scale.
 */
class ProviderImage : public Image {
 public:
  static std::shared_ptr<Image> Make(std::shared_ptr<ImageProvider> provider);

  int width() const override {
    return provider->width();
  }

  int height() const override {
    return provider->height();
  }

  bool isAlphaOnly() const override {
    return provider->isAlphaOnly();
  }

  const std::shared_ptr<ColorSpace>& colorSpace() const override {
    return imageColorSpace;
  }

  bool hasMipmaps() const override {
    return mipmapped;
  }

 protected:
  Type type() const override {
    return Type::Provider;
  }

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

 private:
  ProviderImage(std::shared_ptr<ImageProvider> provider, bool mipmapped);

  std::shared_ptr<Image> resolveVariant(float drawScale) const;

  std::shared_ptr<ImageProvider> provider;
  std::shared_ptr<ColorSpace> imageColorSpace;
  bool mipmapped = false;
};
}  // namespace tgfx
