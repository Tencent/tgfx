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

#include "ImageShader.h"
#include "core/utils/ColorSpaceHelper.h"
#include "core/utils/Types.h"
#include "gpu/ops/DrawOp.h"
#include "gpu/processors/ColorSpaceXFormEffect.h"
#include "gpu/processors/TiledTextureEffect.h"
#include "tgfx/gpu/Texture.h"

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

bool ImageShader::isEqual(const Shader* shader) const {
  auto type = Types::Get(shader);
  if (type != Types::ShaderType::Image) {
    return false;
  }
  auto other = static_cast<const ImageShader*>(shader);
  return image == other->image && tileModeX == other->tileModeX && tileModeY == other->tileModeY &&
         sampling == other->sampling;
}

PlacementPtr<FragmentProcessor> ImageShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  SamplingArgs samplingArgs = {tileModeX, tileModeY, sampling, SrcRectConstraint::Fast};
  auto fp = image->asFragmentProcessor(args, samplingArgs, uvMatrix);
  if (!image->isAlphaOnly() && fp && !NeedConvertColorSpace(image->colorSpace(), dstColorSpace)) {
    auto xformEffect = ColorSpaceXformEffect::Make(
        args.context->drawingAllocator(), image->colorSpace().get(), AlphaType::Premultiplied,
        dstColorSpace.get(), AlphaType::Premultiplied);
    fp = FragmentProcessor::Compose(args.context->drawingAllocator(), std::move(xformEffect),
                                    std::move(fp));
  }
  return fp;
}
}  // namespace tgfx
