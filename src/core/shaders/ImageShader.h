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

#include "gpu/Texture.h"
#include "tgfx/core/Image.h"
#include "tgfx/core/Shader.h"

namespace tgfx {
class ImageShader : public Shader {
 protected:
  std::unique_ptr<FragmentProcessor> asFragmentProcessor(const FPArgs& args,
                                                         const Matrix* uvMatrix) const override;

  ShaderType type() const override {
    return ShaderType::Image;
  }

  std::tuple<std::shared_ptr<Image>, TileMode, TileMode> asImage() const override {
    return {image, tileModeX, tileModeY};
  }

 private:
  ImageShader(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
              const SamplingOptions& sampling)
      : image(std::move(image)), tileModeX(tileModeX), tileModeY(tileModeY), sampling(sampling) {
  }

  std::shared_ptr<Image> image = nullptr;
  TileMode tileModeX = TileMode::Clamp;
  TileMode tileModeY = TileMode::Clamp;
  SamplingOptions sampling = {};

  friend class Shader;
};
}  // namespace tgfx
