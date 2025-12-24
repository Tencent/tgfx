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
  if (image == nullptr) {
    return nullptr;
  }
  return std::shared_ptr<ImagePattern>(
      new ImagePattern(std::move(image), tileModeX, tileModeY, sampling));
}

ImagePattern::ImagePattern(std::shared_ptr<Image> image, TileMode tileModeX, TileMode tileModeY,
                           const SamplingOptions& sampling)
    : _image(std::move(image)), _tileModeX(tileModeX), _tileModeY(tileModeY), _sampling(sampling) {
}

void ImagePattern::setMatrix(const Matrix& matrix) {
  if (_matrix == matrix) {
    return;
  }
  _matrix = matrix;
  invalidateContent();
}

std::shared_ptr<Shader> ImagePattern::getShader() const {
  auto shader = Shader::MakeImageShader(_image, _tileModeX, _tileModeY, _sampling);
  if (shader == nullptr || _matrix.isIdentity()) {
    return shader;
  }
  return shader->makeWithMatrix(_matrix);
}

}  // namespace tgfx
