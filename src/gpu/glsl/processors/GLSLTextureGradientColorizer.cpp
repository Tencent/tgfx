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

#include "GLSLTextureGradientColorizer.h"
#include "GeneratedOperators.h"

namespace tgfx {
PlacementPtr<TextureGradientColorizer> TextureGradientColorizer::Make(
    BlockAllocator* allocator, std::shared_ptr<TextureProxy> gradient) {
  if (gradient == nullptr) {
    return nullptr;
  }
  return allocator->make<GLSLTextureGradientColorizer>(std::move(gradient));
}

GLSLTextureGradientColorizer::GLSLTextureGradientColorizer(std::shared_ptr<TextureProxy> gradient)
    : TextureGradientColorizer(std::move(gradient)) {
}

void GLSLTextureGradientColorizer::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  fragBuilder->codeAppendf("highp vec2 coord = vec2(%s.x, 0.5);", args.inputColor.c_str());
  fragBuilder->codeAppendf("%s = ", args.outputColor.c_str());
  fragBuilder->appendTextureLookup((*args.textureSamplers)[0], "coord");
  fragBuilder->codeAppend(";");
  // The IR route: the ramp coordinate from the input's x, a fixed half, one texture read.
  if (args.irBuilder != nullptr && args.inputValue != INVALID_VALUE_ID &&
      args.outputValue != INVALID_VALUE_ID && args.textureSamplers != nullptr &&
      args.textureSamplers->count() > 0) {
    auto ir = args.irBuilder;
    const auto samplerValue = args.samplerValue((*args.textureSamplers)[0]);
    if (samplerValue != INVALID_VALUE_ID) {
      const auto t = ir->CreateSwizzle(args.inputValue, SLType::Float, "x");
      const auto half = ir->CreateConstant(0.5f);
      const auto coord = ir->CreateConstruct(SLType::Float2, {t, half});
      const auto result = ir->CreateTextureSample(
          samplerValue, coord, SLType::Float4, "gradientColor", SLType::Texture2DSampler);
      ir->StoreOutput(args.outputValue, result);
    }
  }
}
}  // namespace tgfx
