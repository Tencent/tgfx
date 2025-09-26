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
#include "gpu/opengl/GLGPU.h"
#include "gpu/opengl/GLResource.h"

namespace tgfx {
//TODO: Remove this class once all runtime effects have fully switched to using GPU commands.
class GLRuntimeProgram : public GLResource {
 public:
  explicit GLRuntimeProgram(std::unique_ptr<RuntimeProgram> program)
      : runtimeProgram(std::move(program)) {
  }

  RuntimeProgram* program() const {
    return runtimeProgram.get();
  }

 protected:
  void onRelease(GLGPU*) override {
    runtimeProgram->onReleaseGPU();
    runtimeProgram->context = nullptr;
  }

 private:
  std::unique_ptr<RuntimeProgram> runtimeProgram = nullptr;
};

std::shared_ptr<Program> RuntimeProgramWrapper::Wrap(std::unique_ptr<RuntimeProgram> program) {
  if (program == nullptr) {
    return nullptr;
  }
  auto context = program->getContext();
  auto glProgram =
      static_cast<GLGPU*>(context->gpu())->makeResource<GLRuntimeProgram>(std::move(program));
  return std::shared_ptr<RuntimeProgramWrapper>(new RuntimeProgramWrapper(std::move(glProgram)));
}

const RuntimeProgram* RuntimeProgramWrapper::Unwrap(const Program* program) {
  return static_cast<const RuntimeProgramWrapper*>(program)->runtimeProgram->program();
}
}  // namespace tgfx
