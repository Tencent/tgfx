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

#include <utility>
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
class TextureGradientColorizer : public FragmentProcessor {
 public:
  static PlacementPtr<TextureGradientColorizer> Make(
      BlockBuffer* buffer, std::shared_ptr<TextureProxy> gradient,
      std::shared_ptr<ColorSpace> dstColorSpace = nullptr);

  std::string name() const override {
    return "TextureGradientColorizer";
  }

 protected:
  DEFINE_PROCESSOR_CLASS_ID

  explicit TextureGradientColorizer(std::shared_ptr<TextureProxy> gradient,
                                    std::shared_ptr<ColorSpace> colorSpace)
      : FragmentProcessor(ClassID()), gradient(std::move(gradient)),
        dstColorSpace(std::move(colorSpace)) {
  }

  size_t onCountTextureSamplers() const override {
    return 1;
  }

  void onComputeProcessorKey(BytesKey* bytesKey) const override;

  std::shared_ptr<GPUTexture> onTextureAt(size_t) const override {
    auto textureView = gradient->getTextureView();
    return textureView ? textureView->getTexture() : nullptr;
  }

  std::shared_ptr<TextureProxy> gradient;
  std::shared_ptr<ColorSpace> dstColorSpace = nullptr;
};
}  // namespace tgfx
