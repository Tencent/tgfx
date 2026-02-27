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

#include "GLSLAARectEffect.h"

namespace tgfx {
PlacementPtr<AARectEffect> AARectEffect::Make(BlockAllocator* allocator, const Rect& rect) {
  return allocator->make<GLSLAARectEffect>(rect);
}

GLSLAARectEffect::GLSLAARectEffect(const Rect& rect) : AARectEffect(rect) {
}

void GLSLAARectEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto uniformHandler = args.uniformHandler;

  auto rectName = uniformHandler->addUniform("Rect", UniformFormat::Float4, ShaderStage::Fragment);
  fragBuilder->codeAppendf(
      "vec4 dists4 = clamp(vec4(1.0, 1.0, -1.0, -1.0) * vec4(gl_FragCoord.xyxy - %s), 0.0, 1.0);",
      rectName.c_str());
  fragBuilder->codeAppend("vec2 dists2 = dists4.xy + dists4.zw - 1.0;");
  fragBuilder->codeAppend("float coverage = dists2.x * dists2.y;");
  fragBuilder->codeAppendf("%s = %s * coverage;", args.outputColor.c_str(),
                           args.inputColor.c_str());
}

void GLSLAARectEffect::onSetData(UniformData* /*vertexUniformData*/,
                                 UniformData* fragmentUniformData) const {
  // The AA math in the shader evaluates to 0 at the uploaded coordinates, so outset by 0.5
  // to interpolate from 0 at a half pixel inset and 1 at a half pixel outset of rect.
  auto outRect = rect.makeOutset(0.5f, 0.5f);
  fragmentUniformData->setData("Rect", outRect);
}
}  // namespace tgfx
