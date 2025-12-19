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

#include "tgfx/layers/ColorSource.h"

namespace tgfx {
void ColorSource::setAlpha(float value) {
  if (_alpha == value) {
    return;
  }
  _alpha = value;
  invalidateContent();
}

void ColorSource::setBlendMode(BlendMode value) {
  if (_blendMode == value) {
    return;
  }
  _blendMode = value;
  invalidateContent();
}

void ColorSource::setMatrix(const Matrix& value) {
  if (_matrix == value) {
    return;
  }
  _matrix = value;
  invalidateContent();
}

std::shared_ptr<Shader> ColorSource::getShader() const {
  auto shader = onGetShader();
  if (!shader) {
    return nullptr;
  }
  return shader->makeWithMatrix(_matrix);
}

}  // namespace tgfx
