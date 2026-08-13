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

#include "GlassShader.h"
#include "core/utils/Types.h"
#include "gpu/processors/FragmentProcessor.h"
#include "layers/imagefilters/GlassRefractionImageFilter.h"

namespace tgfx {

std::shared_ptr<GlassShader> GlassShader::Make(std::shared_ptr<GlassRefractionImageFilter> filter,
                                               std::shared_ptr<Image> source, const Matrix& matrix,
                                               const SamplingOptions& sampling) {
  if (filter == nullptr || source == nullptr) {
    return nullptr;
  }
  auto shader =
      std::shared_ptr<GlassShader>(new GlassShader(std::move(filter), std::move(source), matrix,
                                                   sampling));
  shader->weakThis = shader;
  return shader;
}

GlassShader::GlassShader(std::shared_ptr<GlassRefractionImageFilter> filter,
                         std::shared_ptr<Image> source, const Matrix& matrix,
                         const SamplingOptions& sampling)
    : filter(std::move(filter)), source(std::move(source)), matrix(matrix), sampling(sampling) {
}

bool GlassShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::Glass) {
    return false;
  }
  auto other = static_cast<const GlassShader*>(shader);
  return filter == other->filter && source == other->source && matrix == other->matrix &&
         sampling == other->sampling;
}

PlacementPtr<FragmentProcessor> GlassShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix,
    const std::shared_ptr<ColorSpace>& /*dstColorSpace*/) const {
  Matrix totalMatrix = {};
  if (!matrix.invert(&totalMatrix)) {
    return nullptr;
  }
  if (uvMatrix) {
    totalMatrix.preConcat(*uvMatrix);
  }
  auto newArgs = args;
  newArgs.drawScale *= matrix.getMaxScale();
  return filter->makeFragmentProcessor(source, newArgs, sampling, SrcRectConstraint::Fast,
                                       &totalMatrix);
}

}  // namespace tgfx
