/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "gpu/Resource.h"
#include "tgfx/gpu/RuntimeProgram.h"

namespace tgfx {
class RuntimeResource : public Resource {
 public:
  static std::shared_ptr<RuntimeResource> Wrap(const UniqueKey& uniqueKey,
                                               std::unique_ptr<RuntimeProgram> program);

  size_t memoryUsage() const override {
    return 0;
  }

  RuntimeProgram* getProgram() const {
    return program.get();
  }

 private:
  std::unique_ptr<RuntimeProgram> program = nullptr;

  explicit RuntimeResource(std::unique_ptr<RuntimeProgram> program) : program(std::move(program)) {
  }

  void onReleaseGPU() override;
};
}  // namespace tgfx
