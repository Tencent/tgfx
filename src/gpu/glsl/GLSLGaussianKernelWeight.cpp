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

#include "GLSLGaussianKernelWeight.h"

namespace tgfx {

void AppendKernelWeight(FragmentShaderBuilder* fragBuilder, const std::string& kernelName,
                        const std::string& offsetName, const std::string& weightName,
                        int slotCount) {
  auto index = weightName + "Index";
  auto slot = weightName + "Slot";
  auto lane = weightName + "Lane";
  auto values = weightName + "Values";
  fragBuilder->codeAppendf("int %s = abs(%s);", index.c_str(), offsetName.c_str());
  fragBuilder->codeAppendf("int %s = %s / 4;", slot.c_str(), index.c_str());
  fragBuilder->codeAppendf("vec4 %s = %s[0];", values.c_str(), kernelName.c_str());
  for (int i = 1; i < slotCount; ++i) {
    fragBuilder->codeAppendf("if (%s == %d) { %s = %s[%d]; }", slot.c_str(), i, values.c_str(),
                             kernelName.c_str(), i);
  }
  fragBuilder->codeAppendf("int %s = %s - %s * 4;", lane.c_str(), index.c_str(), slot.c_str());
  fragBuilder->codeAppendf("float %s = %s.x;", weightName.c_str(), values.c_str());
  fragBuilder->codeAppendf("if (%s == 1) { %s = %s.y; }", lane.c_str(), weightName.c_str(),
                           values.c_str());
  fragBuilder->codeAppendf("if (%s == 2) { %s = %s.z; }", lane.c_str(), weightName.c_str(),
                           values.c_str());
  fragBuilder->codeAppendf("if (%s == 3) { %s = %s.w; }", lane.c_str(), weightName.c_str(),
                           values.c_str());
}

}  // namespace tgfx
