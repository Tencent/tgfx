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

#include "ColorFilterShader.h"
#include "core/utils/Types.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {
std::shared_ptr<Shader> ColorFilterShader::makeWithMatrix(const Matrix& viewMatrix) const {
  auto newShader = shader->makeWithMatrix(viewMatrix);
  if (newShader == nullptr) {
    return nullptr;
  }
  return std::make_shared<ColorFilterShader>(std::move(newShader), colorFilter);
}

bool ColorFilterShader::isEqual(const Shader* otherShader) const {
  auto type = Types::Get(otherShader);
  if (type != Types::ShaderType::ColorFilter) {
    return false;
  }
  auto other = static_cast<const ColorFilterShader*>(otherShader);
  return colorFilter->isEqual(other->colorFilter.get()) && shader->isEqual(other->shader.get());
}

PlacementPtr<FragmentProcessor> ColorFilterShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> colorSpace) const {
  auto fp1 = FragmentProcessor::Make(shader, args, uvMatrix, colorSpace);
  if (fp1 == nullptr) {
    return nullptr;
  }
  auto fp2 = colorFilter->asFragmentProcessor(args.context, colorSpace);
  return FragmentProcessor::Compose(args.context->drawingBuffer(), std::move(fp1), std::move(fp2));
}
}  // namespace tgfx
