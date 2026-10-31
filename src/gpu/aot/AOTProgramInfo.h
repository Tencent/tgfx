/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in
//  compliance with the License. You may obtain a copy of the License at
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

namespace tgfx {

class Program;
class ProgramInfo;
struct AOTProgram;

/**
 * The interpreter's program resolution for one draw, computed entirely at construction: the
 * variant derives from the compiled chain program and the draw's geometry and xfer processors
 * (never from a fragment-processor list — the program is the whole expression), the pipeline key
 * from the variant plus the pipeline state, and the program from the global cache, created from
 * the bundle on a miss. There is no mutable state and no lazy field: a draw either resolves here
 * or its caller falls back to the plain route.
 */
class AOTProgramInfo {
 public:
  AOTProgramInfo(const ProgramInfo& info, const AOTProgram& program);

  /**
   * Whether the route resolved. False means the draw's expression is not servable this way (no
   * variant, a permutation outside the enumerated set, or an artifact the bundle lacks) and the
   * plain route must serve the draw.
   */
  bool valid() const {
    return resolvedProgram != nullptr;
  }

  /** The resolved program: always a precompiled artifact. */
  const std::shared_ptr<Program>& program() const {
    return resolvedProgram;
  }

 private:
  std::shared_ptr<Program> resolvedProgram = nullptr;
};

}  // namespace tgfx
