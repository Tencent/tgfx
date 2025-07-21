/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "tgfx/core/Image.h"

namespace tgfx {
class ScaleImage : public Image {
 public:
  static std::shared_ptr<Image> MakeFrom(std::shared_ptr<Image> image, float scale,
                                         const SamplingOptions& sampling, bool mipmapped = false);

  static int GetScaledSize(int size, float scale);

  ScaleImage(std::shared_ptr<Image> image, float scale, const SamplingOptions& sampling,
             bool mipmapped);

  ~ScaleImage() override = default;

  int width() const override;

  int height() const override;

  bool isAlphaOnly() const override {
    return false;
  }

  bool hasMipmaps() const override {
    return mipmapped;
  }

  bool isFullyDecoded() const override {
    return source->isFullyDecoded();
  }

  std::shared_ptr<Image> onMakeMipmapped(bool enabled) const override;

  std::shared_ptr<Image> onMakeDecoded(Context* context, bool tryHardware) const override;

 protected:
  Type type() const override {
    return Type::Scale;
  }

  PlacementPtr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                      const SamplingArgs& samplingArgs,
                                                      const Matrix* uvMatrix) const override;

  std::shared_ptr<TextureProxy> lockTextureProxy(const TPArgs& args) const override;

 private:
  std::shared_ptr<Image> source = nullptr;
  float scale = 1.0f;
  SamplingOptions sampling = {};
  bool mipmapped = false;
};
}  // namespace tgfx
