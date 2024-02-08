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

#include "BlendShader.h"
#include "gpu/processors/FragmentProcessor.h"
#include "gpu/processors/XfermodeFragmentProcessor.h"

namespace tgfx {
std::shared_ptr<Shader> Shader::MakeBlend(BlendMode mode, std::shared_ptr<Shader> dst,
                                          std::shared_ptr<Shader> src) {
  switch (mode) {
    case BlendMode::Clear:
      return MakeColorShader(Color::Transparent());
    case BlendMode::Dst:
      return dst;
    case BlendMode::Src:
      return src;
    default:
      break;
  }
  if (dst == nullptr || src == nullptr) {
    return nullptr;
  }
  auto shader = std::make_shared<BlendShader>(mode, std::move(dst), std::move(src));
  shader->weakThis = shader;
  return shader;
}

std::unique_ptr<FragmentProcessor> BlendShader::onMakeFragmentProcessor(
    const DrawArgs& args, const Matrix* localMatrix) const {
  auto fpA = FragmentProcessor::Make(dst, args, localMatrix);
  if (fpA == nullptr) {
    return nullptr;
  }
  auto fpB = FragmentProcessor::Make(src, args, localMatrix);
  if (fpB == nullptr) {
    return nullptr;
  }
  return XfermodeFragmentProcessor::MakeFromTwoProcessors(std::move(fpB), std::move(fpA), mode);
}
}  // namespace tgfx
