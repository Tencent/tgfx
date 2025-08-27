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

#include "RuntimeProgramWrapper.h"
#include "core/utils/Log.h"

namespace tgfx {
std::shared_ptr<Program> RuntimeProgramWrapper::Wrap(std::unique_ptr<RuntimeProgram> program) {
  if (program == nullptr) {
    return nullptr;
  }
  auto context = program->getContext();
  return Resource::AddToCache(context, new RuntimeProgramWrapper(std::move(program)));
}

const RuntimeProgram* RuntimeProgramWrapper::Unwrap(const Program* program) {
  return static_cast<const RuntimeProgramWrapper*>(program)->runtimeProgram.get();
}

void RuntimeProgramWrapper::onReleaseGPU() {
  runtimeProgram->onReleaseGPU();
  runtimeProgram->context = nullptr;
}
}  // namespace tgfx
