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

#include "tgfx/core/Shader.h"
#include "shaders/ColorFilterShader.h"
#include "shaders/MatrixShader.h"

namespace tgfx {
std::shared_ptr<Shader> Shader::makeWithMatrix(const Matrix& viewMatrix) const {
  return MatrixShader::MakeFrom(weakThis.lock(), viewMatrix);
}

std::shared_ptr<Shader> Shader::makeWithColorFilter(
    std::shared_ptr<ColorFilter> colorFilter) const {
  auto source = weakThis.lock();
  if (colorFilter == nullptr) {
    return source;
  }
  auto shader = std::make_shared<ColorFilterShader>(std::move(source), std::move(colorFilter));
  shader->weakThis = shader;
  return shader;
}
}  // namespace tgfx
