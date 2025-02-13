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

#include "ColorShader.h"
#include "core/utils/Caster.h"
#include "gpu/processors/ConstColorProcessor.h"

namespace tgfx {
std::shared_ptr<Shader> Shader::MakeColorShader(Color color) {
  auto shader = std::make_shared<ColorShader>(color);
  shader->weakThis = shader;
  return shader;
}

bool ColorShader::isOpaque() const {
  return color.isOpaque();
}

bool ColorShader::asColor(Color* output) const {
  *output = color;
  return true;
}

bool ColorShader::isEqual(const Shader* shader) const {
  auto other = Caster::AsColorShader(shader);
  return other && color == other->color;
}

std::unique_ptr<FragmentProcessor> ColorShader::asFragmentProcessor(const FPArgs&,
                                                                    const Matrix*) const {
  return ConstColorProcessor::Make(color.premultiply(), InputMode::ModulateA);
}

}  // namespace tgfx
