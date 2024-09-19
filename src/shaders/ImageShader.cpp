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

#include "ImageShader.h"
#include "gpu/TextureSampler.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/processors/TiledTextureEffect.h"

namespace tgfx {
std::shared_ptr<Shader> Shader::MakeImageShader(std::shared_ptr<Image> image, TileMode tileModeX,
                                                TileMode tileModeY,
                                                const SamplingOptions& sampling) {
  if (image == nullptr) {
    return nullptr;
  }
  auto shader = std::shared_ptr<ImageShader>(
      new ImageShader(std::move(image), tileModeX, tileModeY, sampling));
  shader->weakThis = shader;
  return shader;
}

std::unique_ptr<FragmentProcessor> ImageShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  return image->asFragmentProcessor(args, tileModeX, tileModeY, sampling, uvMatrix);
}
}  // namespace tgfx
