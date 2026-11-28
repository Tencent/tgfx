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

#include "GLSLRadialGradientLayout.h"
#include "GeneratedOperators.h"

namespace tgfx {
PlacementPtr<RadialGradientLayout> RadialGradientLayout::Make(BlockAllocator* allocator,
                                                              Matrix matrix) {
  return allocator->make<GLSLRadialGradientLayout>(matrix);
}

GLSLRadialGradientLayout::GLSLRadialGradientLayout(Matrix matrix) : RadialGradientLayout(matrix) {
}

void GLSLRadialGradientLayout::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  const auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  // The IR route: the ramp position through the shared function, splatted to the layout's
  // vec4 form (t, 1, 0, 0).
  if (args.irBuilder != nullptr && args.outputValue != INVALID_VALUE_ID) {
    auto ir = args.irBuilder;
    const auto coord =
        ir->InternValue(coordName, SLType::Float2, Precision::High, IRValueKind::Varying);
    RadialGradientOp::UniformNames names = {};
    const auto t = RadialGradientOp::Emit(ir, args.uniformHandler, &names, coord);
    const auto one = ir->CreateConstant(1.0f);
    const auto zero = ir->CreateConstant(0.0f);
    const auto position = ir->CreateConstruct(SLType::Float4, {t, one, zero, zero});
    ir->StoreOutput(args.outputValue, position);
  }
}
}  // namespace tgfx
