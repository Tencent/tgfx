/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "GLAtlasMaskEffect.h"

namespace tgfx {

PlacementPtr<FragmentProcessor> AtlasMaskEffect::Make(std::shared_ptr<TextureProxy> proxy,
                                                      const SamplingOptions& sampling) {

  if (proxy == nullptr) {
    return nullptr;
  }
  auto drawingBuffer = proxy->getContext()->drawingBuffer();
  auto processor = drawingBuffer->make<GLAtlasMaskEffect>(std::move(proxy), sampling);
  return processor;
}

static void EmitTextureCode(const FragmentProcessor::EmitArgs& args) {
  auto* fragBuilder = args.fragBuilder;
  auto& textureSampler = (*args.textureSamplers)[0];
  fragBuilder->codeAppend("vec4 color = ");
  fragBuilder->appendTextureLookup(textureSampler, "textureCoords_P0");
  fragBuilder->codeAppend(";");
  fragBuilder->codeAppendf("%s = color;", args.outputColor.c_str());
}

GLAtlasMaskEffect::GLAtlasMaskEffect(std::shared_ptr<TextureProxy> proxy,
                                     const SamplingOptions& sampling)
    : AtlasMaskEffect(std::move(proxy), sampling) {
}

void GLAtlasMaskEffect::emitCode(EmitArgs& args) const {
  auto texture = getTexture();
  auto fragBuilder = args.fragBuilder;
  if (texture == nullptr) {
    // emit a transparent color as the output color.
    fragBuilder->codeAppendf("%s = vec4(0.0);", args.outputColor.c_str());
  }
  EmitTextureCode(args);
  if (textureProxy->isAlphaOnly()) {
    fragBuilder->codeAppendf("%s = %s.a * %s;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  } else {
    fragBuilder->codeAppendf("%s = %s * %s.a;", args.outputColor.c_str(), args.outputColor.c_str(),
                             args.inputColor.c_str());
  }
}
}  // namespace tgfx
