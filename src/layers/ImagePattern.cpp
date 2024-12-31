/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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
  if (image == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ImagePattern>(
      new ImagePattern(std::move(image), tileModeX, tileModeY, sampling));
}

ImagePattern::ImagePattern(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
                           const SamplingOptions& sampling)
    : image(std::move(image)), tileModeX(tileModeX), tileModeY(tileModeY), sampling(sampling) {
}

void ImagePattern::setMatrix(const Matrix& value) {
  if (value == _matrix) {
    return;
  }
  _matrix = value;
  invalidate();
}

std::shared_ptr<Shader> ImagePattern::getShader() const {
  auto shader = Shader::MakeImageShader(image, tileModeX, tileModeY, sampling);
  if (!shader) {
    return nullptr;
  }
  return shader->makeWithMatrix(_matrix);
}

}  // namespace tgfx
