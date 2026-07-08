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

#include <memory>
#include "gpu/Program.h"
#include "gpu/ProgramInfo.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

/// Attempts to create a Program from a precompiled shader bundle. Returns nullptr if the
/// PrecompiledShaderCache is empty, the ProgramInfo does not match any known permutation, or
/// the bundle does not contain the required variant.
class PrecompiledProgramCreator {
 public:
  static std::shared_ptr<Program> CreateProgram(Context* context, const ProgramInfo* programInfo);
};

}  // namespace tgfx
