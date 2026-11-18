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

#include "ProviderImage.h"
#include "gpu/TPArgs.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
std::shared_ptr<Image> ProviderImage::Make(std::shared_ptr<ImageProvider> provider) {
  if (provider == nullptr || provider->width() <= 0 || provider->height() <= 0) {
    return nullptr;
  }
  auto image = std::shared_ptr<ProviderImage>(new ProviderImage(std::move(provider), false));
  image->weakThis = image;
  return image;
}

ProviderImage::ProviderImage(std::shared_ptr<ImageProvider> provider, bool mipmapped)
    : provider(std::move(provider)), imageColorSpace(this->provider->colorSpace()),
      mipmapped(mipmapped) {
  if (imageColorSpace == nullptr) {
    imageColorSpace = ColorSpace::SRGB();
  }
}

std::shared_ptr<TextureProxy> ProviderImage::lockTextureProxy(const TPArgs& args) const {
  auto variant = resolveVariant(args.drawScale);
  if (variant == nullptr) {
    return nullptr;
  }
  auto variantArgs = args;
  variantArgs.drawScale =
      args.drawScale * static_cast<float>(width()) / static_cast<float>(variant->width());
  variantArgs.mipmapped = variantArgs.mipmapped || mipmapped;
  return variant->lockTextureProxy(variantArgs);
}

PlacementPtr<FragmentProcessor> ProviderImage::asFragmentProcessor(const FPArgs& args,
                                                                   const SamplingArgs& samplingArgs,
                                                                   const Matrix* uvMatrix) const {
  auto variant = resolveVariant(args.drawScale);
  if (variant == nullptr) {
    return nullptr;
  }
  // A variant shows the same content at a different pixel density, so the logical space has to be
  // mapped onto the variant's pixel space before the variant applies its own uv matrix.
  auto variantMatrix =
      Matrix::MakeScale(static_cast<float>(variant->width()) / static_cast<float>(width()),
                        static_cast<float>(variant->height()) / static_cast<float>(height()));
  SamplingArgs variantSamplingArgs = samplingArgs;
  if (samplingArgs.sampleArea.has_value()) {
    auto subset = *samplingArgs.sampleArea;
    variantMatrix.mapRect(&subset);
    variantSamplingArgs.sampleArea = subset;
  }
  if (uvMatrix != nullptr) {
    variantMatrix.preConcat(*uvMatrix);
  }
  auto variantArgs = args;
  variantArgs.drawScale =
      args.drawScale * static_cast<float>(width()) / static_cast<float>(variant->width());
  return variant->asFragmentProcessor(variantArgs, variantSamplingArgs, &variantMatrix);
}

std::shared_ptr<Image> ProviderImage::onMakeMipmapped(bool enabled) const {
  auto image = std::shared_ptr<ProviderImage>(new ProviderImage(provider, enabled));
  image->weakThis = image;
  return image;
}

std::shared_ptr<Image> ProviderImage::resolveVariant(float drawScale) const {
  if (drawScale <= 0.0f) {
    return nullptr;
  }
  auto variant = provider->getImage(drawScale);
  if (variant == nullptr || variant->width() <= 0 || variant->height() <= 0) {
    return nullptr;
  }
  return variant;
}
}  // namespace tgfx
