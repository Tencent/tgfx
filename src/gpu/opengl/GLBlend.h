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

#pragma once
#include "gpu/FragmentShaderBuilder.h"
#include "tgfx/core/BlendMode.h"

namespace tgfx {
// Appends GLSL code to fragment that assigns a specified blend of the srcColor, coverageColor and
// dstColor variables to the outColor variable.
void AppendMode(FragmentShaderBuilder* fsBuilder, const std::string& srcColor,
                const std::string& coverageColor, const std::string& dstColor,
                const std::string& outColor, BlendMode blendMode, bool hasCoverage);

const char* BlendModeName(BlendMode mode);
}  // namespace tgfx
