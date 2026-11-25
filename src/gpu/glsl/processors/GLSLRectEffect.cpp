/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "GLSLRectEffect.h"
#include "GeneratedOperators.h"
#include "core/utils/Log.h"
#include "gpu/glsl/GLSLOperatorEmitter.h"

namespace tgfx {

PlacementPtr<RectEffect> RectEffect::Make(BlockAllocator* allocator, const Rect& localRect,
                                          const Matrix& matrix, bool antiAlias) {
  // The analytic AA relies on a linear device-to-local inverse map, which cannot represent a
  // perspective matrix.
  DEBUG_ASSERT(!matrix.hasPerspective());
  if (matrix.hasPerspective()) {
    return nullptr;
  }

  Rect adjustedRect = localRect;
  Matrix deviceToLocal = Matrix::I();
  if (!matrix.isIdentity()) {
    // Decompose matrix as M = M_axisFree * diag(axisScales). Bake axisScales into the local
    // rect so the deviceToLocal matrix used at runtime preserves the device-space half-pixel
    // AA band semantics.
    const auto scales = matrix.getAxisScales();
    if (scales.x == 0 || scales.y == 0) {
      return nullptr;
    }
    adjustedRect.scale(scales.x, scales.y);
    Matrix adjustedMatrix = matrix;
    adjustedMatrix.preScale(1.0f / scales.x, 1.0f / scales.y);
    if (!adjustedMatrix.invert(&deviceToLocal)) {
      return nullptr;
    }
  }
  return allocator->make<GLSLRectEffect>(adjustedRect, deviceToLocal, antiAlias);
}

GLSLRectEffect::GLSLRectEffect(const Rect& localRect, const Matrix& deviceToLocal, bool antiAlias)
    : RectEffect(localRect, deviceToLocal, antiAlias) {
}

void GLSLRectEffect::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  const auto entry = fragBuilder->appendFunction("pmRectEffect");
  if (entry.empty()) {
    return;
  }
  if (args.irBuilder == nullptr || args.inputValue == INVALID_VALUE_ID ||
      args.outputValue == INVALID_VALUE_ID) {
    return;
  }
  RectCoverageOp::UniformNames names = {};
  const auto fragCoordValue =
      args.irBuilder->CreateBuiltin("gl_FragCoord", SLType::Float4);
  const auto fragCoordXY =
      args.irBuilder->CreateSwizzle(fragCoordValue, SLType::Float2, "xy");
  const auto result = RectCoverageOp::Emit(args.irBuilder, args.uniformHandler, &names,
                                           args.inputValue, fragCoordXY);
  args.irBuilder->StoreOutput(args.outputValue, result);
  std::string expression = {};
  std::string error = {};
  if (result == INVALID_VALUE_ID ||
      !GLSLOperatorEmitter::EmitCall(args.irBuilder->IR(), result, &expression, &error)) {
    args.irBuilder->MarkFailed(error.empty() ? "the rect operator could not be emitted" : error);
    return;
  }
  fragBuilder->appendCall(
      args.outputColor, entry,
      {args.inputColorValue, ValueRef("gl_FragCoord.xy", SLType::Float2, ValueOrigin::Literal),
       ValueRef(names.rect, SLType::Float4, ValueOrigin::Uniform),
       ValueRef(names.antiAlias, SLType::Float, ValueOrigin::Uniform),
       ValueRef(names.deviceToLocal, SLType::Float3x3, ValueOrigin::Uniform)},
      "", expression);
}

void GLSLRectEffect::onSetData(UniformData*, UniformData* fragmentUniformData) const {
  RectCoverageUniforms uniforms = {};
  uniforms.rect = {localRect.left, localRect.top, localRect.right, localRect.bottom};
  uniforms.antiAlias = antiAlias ? 1.0f : 0.0f;
  uniforms.deviceToLocal = needTransform() ? deviceToLocal() : Matrix::I();
  uniforms.upload(fragmentUniformData);
}

}  // namespace tgfx
