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

#include "GLSLLinearGradientLayout.h"

namespace tgfx {
PlacementPtr<LinearGradientLayout> LinearGradientLayout::Make(BlockAllocator* allocator,
                                                              Matrix matrix) {
  return allocator->make<GLSLLinearGradientLayout>(matrix);
}

GLSLLinearGradientLayout::GLSLLinearGradientLayout(Matrix matrix) : LinearGradientLayout(matrix) {
}

void GLSLLinearGradientLayout::emitCode(EmitArgs& args) const {
  auto fragBuilder = args.fragBuilder;
  const auto coordName = fragBuilder->emitPerspTextCoord((*args.transformedCoords)[0]);
  fragBuilder->codeAppendf("float t = %s.x + 1.0000000000000001e-05;", coordName.c_str());
  fragBuilder->codeAppendf("%s = vec4(t, 1.0, 0.0, 0.0);", args.outputColor.c_str());
}
}  // namespace tgfx
