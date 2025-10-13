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

namespace tgfx {

PlacementPtr<FragmentProcessor> LumaFragmentProcessor::Make(BlockBuffer* buffer) {
  return buffer->make<GLSLLumaFragmentProcessor>();
}

void GLSLLumaFragmentProcessor::emitCode(EmitArgs& args) const {
  /** See ITU-R Recommendation BT.709 at http://www.itu.int/rec/R-REC-BT.709/ .*/
  args.fragBuilder->codeAppendf("float luma = dot(%s.rgb, vec3(0.2126, 0.7152, 0.0722));\n",
                                args.inputColor.c_str());
  args.fragBuilder->codeAppendf("%s = vec4(luma);\n", args.outputColor.c_str());
}

}  // namespace tgfx
