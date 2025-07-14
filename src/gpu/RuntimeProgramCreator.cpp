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

#include "RuntimeProgramCreator.h"
#include "core/utils/UniqueID.h"
#include "gpu/RuntimeProgramWrapper.h"

namespace tgfx {
void RuntimeProgramCreator::computeProgramKey(Context*, BytesKey* programKey) const {
  static auto RuntimeProgramType = UniqueID::Next();
  programKey->write(RuntimeProgramType);
  programKey->write(effect->programID());
}

std::unique_ptr<Program> RuntimeProgramCreator::createProgram(Context* context) const {
  auto program = effect->onCreateProgram(context);
  if (program == nullptr) {
    return nullptr;
  }
  return std::make_unique<RuntimeProgramWrapper>(std::move(program));
}
}  // namespace tgfx
