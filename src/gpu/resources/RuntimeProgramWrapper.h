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

#pragma once

#include "gpu/resources/Program.h"
#include "tgfx/gpu/RuntimeProgram.h"

namespace tgfx {
class GLRuntimeProgram;

class RuntimeProgramWrapper : public Program {
 public:
  static std::shared_ptr<Program> Wrap(std::unique_ptr<RuntimeProgram> program);

  static const RuntimeProgram* Unwrap(const Program* program);

 private:
  std::shared_ptr<GLRuntimeProgram> runtimeProgram = nullptr;

  explicit RuntimeProgramWrapper(std::shared_ptr<GLRuntimeProgram> program)
      : runtimeProgram(std::move(program)) {
  }
};
}  // namespace tgfx
