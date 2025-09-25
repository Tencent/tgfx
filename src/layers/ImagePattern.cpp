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

#include "tgfx/layers/ImagePattern.h"

namespace tgfx {
std::shared_ptr<ImagePattern> ImagePattern::Make(std::shared_ptr<Image> image, TileMode tileModeX,
                                                 TileMode tileModeY,
                                                 const SamplingOptions& sampling) {
  return Make(std::move(image), tileModeX, tileModeY, sampling.minFilterMode,
              sampling.magFilterMode, sampling.mipmapMode);
}

std::shared_ptr<ImagePattern> ImagePattern::Make(std::shared_ptr<Image> image, TileMode tileModeX,
                                                 TileMode tileModeY, FilterMode minFilterMode,
                                                 FilterMode magFilterMode, MipmapMode mipmapMode) {
  if (image == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ImagePattern>(new ImagePattern(std::move(image), tileModeX, tileModeY,
                                                        minFilterMode, magFilterMode, mipmapMode));
}

ImagePattern::ImagePattern(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
                           FilterMode minFilterMode, FilterMode magFilterMode,
                           MipmapMode mipmapMode)
    : _image(std::move(image)), _tileModeX(tileModeX), _tileModeY(tileModeY),
      _minFilterMode(minFilterMode), _magFilterMode(magFilterMode), _mipmapMode(mipmapMode) {
}

std::shared_ptr<Shader> ImagePattern::onGetShader() const {
  return Shader::MakeImageShader(_image, _tileModeX, _tileModeY,
                                 SamplingOptions(_minFilterMode, _magFilterMode, _mipmapMode));
}

}  // namespace tgfx
