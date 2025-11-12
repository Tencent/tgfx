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

#include "ColorShader.h"
#include "core/utils/Types.h"
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
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::Color) {
    return false;
  }
  auto other = static_cast<const ColorShader*>(shader);
  return color == other->color;
}

PlacementPtr<FragmentProcessor> ColorShader::asFragmentProcessor(
    const FPArgs& args, const Matrix*, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto dstColor = ColorSpaceXformSteps::ConvertColorSpace(
      ColorSpace::MakeSRGB(), AlphaType::Unpremultiplied, std::move(dstColorSpace),
      AlphaType::Premultiplied, color);
  return ConstColorProcessor::Make(args.context->drawingAllocator(), dstColor,
                                   InputMode::ModulateA);
}

}  // namespace tgfx
