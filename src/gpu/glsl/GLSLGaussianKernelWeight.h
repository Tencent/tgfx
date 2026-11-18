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

#pragma once
#include <string>
#include "gpu/FragmentShaderBuilder.h"

namespace tgfx {
// Appends GLSL code that reads the half-kernel weight for the signed loop offset held in
// offsetName into a float named weightName, selecting from a vec4[slotCount] uniform array named
// kernelName. The slot and the lane inside it are picked with chains of compile-time indices
// rather than a dynamic index into the uniform array, which SwiftShader miscompiles (it silently
// reads zeros and drops the blur).
void AppendKernelWeight(FragmentShaderBuilder* fragBuilder, const std::string& kernelName,
                        const std::string& offsetName, const std::string& weightName,
                        int slotCount);
}  // namespace tgfx
