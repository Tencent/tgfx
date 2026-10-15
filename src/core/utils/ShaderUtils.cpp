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

#include "ShaderUtils.h"
#include "core/shaders/MatrixShader.h"
#include "core/utils/Types.h"

namespace tgfx {

std::pair<std::shared_ptr<Shader>, Matrix> ShaderUtils::UnwrapMatrixShader(
    std::shared_ptr<Shader> shader) {
  Matrix matrix = Matrix::I();
  auto current = std::move(shader);
  while (Types::Get(current.get()) == Types::ShaderType::Matrix) {
    const auto* matrixShader = static_cast<const MatrixShader*>(current.get());
    matrix.preConcat(matrixShader->matrix);
    current = matrixShader->source;
  }
  return {std::move(current), matrix};
}

}  // namespace tgfx
