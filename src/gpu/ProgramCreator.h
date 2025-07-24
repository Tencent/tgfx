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

#include "gpu/Blend.h"
#include "gpu/Program.h"
#include "gpu/SamplerState.h"
#include "gpu/TextureSampler.h"
#include "gpu/UniformBuffer.h"

namespace tgfx {
/**
 * ProgramCreator is an interface for creating GPU programs. It provides methods to compute the
 * key for the program and to create a Program instance.
 */
class ProgramCreator {
 public:
  virtual ~ProgramCreator() = default;

  /**
   * Computes the key for the program, which is used to cache the program in the GlobalCache.
   */
  virtual void computeProgramKey(Context* context, BytesKey* programKey) const = 0;

  /**
   * Creates a Program instance based on the current state of the ProgramCreator.
   */
  virtual std::unique_ptr<Program> createProgram(Context* context) const = 0;
};
}  // namespace tgfx
