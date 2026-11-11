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
#include "gpu/aot/spec/SpecValues.h"
#include "gpu/shaders/Family.h"
#include "tgfx/gpu/Context.h"

namespace tgfx {

class ProgramWriter;

/// Attempts to create a Program from a precompiled shader bundle. Returns nullptr if the
/// PrecompiledShaderCache is empty, the ProgramInfo does not match any known permutation, or
/// the bundle does not contain the required variant.
class PrecompiledProgramCreator {
 public:
  static std::shared_ptr<Program> CreateProgram(Context* context, const ProgramInfo* programInfo);

  /**
   * Creates the Program a resolved selection points at: the deny list, the blob lookup, the
   * shader modules and the pipeline. The caller supplies the matched variant (the plain route
   * from the selection walk, the AOT route from the compiled program's variant) and an info
   * carrying the geometry processor and the pipeline state; no fragment-processor list is read.
   * Returns nullptr when the variant is denied, missing from the bundle, or fails to build.
   *
   * When spec carries captured values (see SpecCapture) and the backend stores GLSL text, the
   * fragment source is specialized before compilation; a specialized module or pipeline that
   * fails to build falls back to this call with a null spec, so specialization stays strictly an
   * optimization.
   */
  static std::shared_ptr<Program> CreateProgramForVariant(Context* context,
                                                          const ProgramInfo* programInfo,
                                                          const Variant& variant,
                                                          const SpecValues* spec = nullptr);
};

}  // namespace tgfx
