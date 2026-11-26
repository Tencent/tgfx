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

#include "GLSLConicGradientLayout.h"
#include "GeneratedOperators.h"

namespace tgfx {
PlacementPtr<ConicGradientLayout> ConicGradientLayout::Make(BlockAllocator* allocator,
                                                            Matrix matrix, float bias,
                                                            float scale) {
  return allocator->make<GLSLConicGradientLayout>(matrix, bias, scale);
}

GLSLConicGradientLayout::GLSLConicGradientLayout(Matrix matrix, float bias, float scale)
    : ConicGradientLayout(matrix, bias, scale) {
}

void GLSLConicGradientLayout::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  // The generated glue owns the uniforms (the names match on both routes).
  ConicGradientOp::UniformNames names = {};
  if (args.irBuilder != nullptr) {
    ConicGradientOp::EnsureUniforms(args.irBuilder, args.uniformHandler, &names);
  }
  const auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  auto entry = fragBuilder->appendFunction("tgfxConicGradientT");
  if (entry.empty()) {
    return;
  }
  fragBuilder->codeAppendf("%s = vec4(%s(%s, %s, %s), 1.0, 0.0, 0.0);", args.outputColor.c_str(),
                           entry.c_str(), coordName.c_str(), names.bias.c_str(),
                           names.scale.c_str());
  // The IR route: the angle position through the shared function, splatted to vec4.
  if (args.irBuilder != nullptr && args.outputValue != INVALID_VALUE_ID) {
    auto ir = args.irBuilder;
    const auto coord =
        ir->InternValue(coordName, SLType::Float2, Precision::High, IRValueKind::Varying);
    const auto t = ConicGradientOp::Emit(ir, args.uniformHandler, &names, coord);
    const auto one = ir->CreateConstant(1.0f);
    const auto zero = ir->CreateConstant(0.0f);
    const auto position = ir->CreateConstruct(SLType::Float4, {t, one, zero, zero});
    ir->StoreOutput(args.outputValue, position);
  }
}

void GLSLConicGradientLayout::onSetData(UniformData* /*vertexUniformData*/,
                                        UniformData* fragmentUniformData) const {
  ConicGradientUniforms uniforms = {};
  uniforms.bias = bias;
  uniforms.scale = scale;
  uniforms.upload(fragmentUniformData);
}
}  // namespace tgfx
