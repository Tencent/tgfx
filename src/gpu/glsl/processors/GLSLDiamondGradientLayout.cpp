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

#include "GLSLDiamondGradientLayout.h"

namespace tgfx {
PlacementPtr<DiamondGradientLayout> DiamondGradientLayout::Make(BlockAllocator* allocator,
                                                                Matrix matrix) {
  return allocator->make<GLSLDiamondGradientLayout>(matrix);
}

GLSLDiamondGradientLayout::GLSLDiamondGradientLayout(Matrix matrix)
    : DiamondGradientLayout(matrix) {
}

void GLSLDiamondGradientLayout::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  const auto coord = (*args.transformedCoords)[0].name();
  fragBuilder->codeAppendf("vec2 rotated = %s;", coord.c_str());
  fragBuilder->codeAppendf("float t = max(abs(rotated.x), abs(rotated.y));");
  fragBuilder->codeAppendf("%s = vec4(t, 1.0, 0.0, 0.0);", args.outputColor.c_str());
}

}  // namespace tgfx
