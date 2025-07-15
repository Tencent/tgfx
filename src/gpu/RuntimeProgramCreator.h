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

#include "gpu/ProgramCreator.h"
#include "tgfx/gpu/RuntimeEffect.h"

namespace tgfx {
class RuntimeProgramCreator : public ProgramCreator {
 public:
  explicit RuntimeProgramCreator(std::shared_ptr<RuntimeEffect> effect)
      : effect(std::move(effect)) {
  }

  void computeProgramKey(Context* context, BytesKey* programKey) const override;

  std::unique_ptr<Program> createProgram(Context* context) const override;

 private:
  std::shared_ptr<RuntimeEffect> effect = nullptr;
};
}  // namespace tgfx
