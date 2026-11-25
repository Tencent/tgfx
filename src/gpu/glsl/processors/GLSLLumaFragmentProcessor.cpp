/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "GLSLLumaFragmentProcessor.h"
#include "GeneratedOperators.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

namespace tgfx {
PlacementPtr<FragmentProcessor> LumaFragmentProcessor::Make(
    BlockAllocator* allocator, std::shared_ptr<ColorSpace> colorSpace) {
  return allocator->make<GLSLLumaFragmentProcessor>(std::move(colorSpace));
}

void GLSLLumaFragmentProcessor::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  auto entry = fragBuilder->appendFunction("pmApplyLuma");
  if (entry.empty()) {
    return;
  }
  if (args.irBuilder == nullptr || args.inputValue == INVALID_VALUE_ID ||
      args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  LumaOp::UniformNames names = {};
  const auto result = LumaOp::Emit(args.irBuilder, args.uniformHandler, &names, args.inputValue);
  args.irBuilder->StoreOutput(args.outputValue, result);
  std::string expression = {};
  std::string error = {};
  if (result == INVALID_VALUE_ID ||
      !GLSLOperatorEmitter::EmitCall(args.irBuilder->IR(), result, &expression, &error)) {
    args.irBuilder->MarkFailed(error.empty() ? "the luma operator could not be emitted" : error);
    return;
  }
  fragBuilder->appendCall(
      args.outputColor, entry,
      {args.inputColorValue, ValueRef(names.kr, SLType::Float, ValueOrigin::Uniform),
       ValueRef(names.kg, SLType::Float, ValueOrigin::Uniform),
       ValueRef(names.kb, SLType::Float, ValueOrigin::Uniform)},
      "", expression);
}

void GLSLLumaFragmentProcessor::onSetData(UniformData* /*vertexUniformData*/,
                                          UniformData* fragmentUniformData) const {
  LumaUniforms uniforms = {};
  uniforms.kr = _lumaFactor.kr;
  uniforms.kg = _lumaFactor.kg;
  uniforms.kb = _lumaFactor.kb;
  uniforms.upload(fragmentUniformData);
}

}  // namespace tgfx
