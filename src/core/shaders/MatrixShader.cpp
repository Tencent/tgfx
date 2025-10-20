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

#include "MatrixShader.h"
#include "core/utils/Types.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
std::shared_ptr<Shader> MatrixShader::MakeFrom(std::shared_ptr<Shader> source,
                                               const Matrix& viewMatrix) {
  if (source == nullptr) {
    return nullptr;
  }
  if (viewMatrix.isIdentity()) {
    return source;
  }
  auto shader = std::shared_ptr<MatrixShader>(new MatrixShader(std::move(source), viewMatrix));
  shader->weakThis = shader;
  return shader;
}

MatrixShader::MatrixShader(std::shared_ptr<Shader> source, const Matrix& matrix)
    : source(std::move(source)), matrix(matrix) {
}

std::shared_ptr<Shader> MatrixShader::makeWithMatrix(const Matrix& viewMatrix) const {
  if (viewMatrix.isIdentity()) {
    return weakThis.lock();
  }
  auto totalMatrix = matrix;
  totalMatrix.postConcat(viewMatrix);
  return MatrixShader::MakeFrom(source, totalMatrix);
}

bool MatrixShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::Matrix) {
    return false;
  }
  auto other = static_cast<const MatrixShader*>(shader);
  return matrix == other->matrix && source->isEqual(other->source.get());
}

PlacementPtr<FragmentProcessor> MatrixShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  Matrix totalMatrix = {};
  if (!matrix.invert(&totalMatrix)) {
    return nullptr;
  }
  if (uvMatrix) {
    totalMatrix.preConcat(*uvMatrix);
  }
  return FragmentProcessor::Make(source, args, &totalMatrix, dstColorSpace);
}
}  // namespace tgfx
