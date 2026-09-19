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

#include "gpu/AOTChainRegisterAllocator.h"

namespace tgfx {

bool AllocateChainRegisters(const std::vector<std::vector<int>>& instructionInputs,
                            int rootInstruction, int coverageRootInstruction,
                            int clipCoverageInstruction, size_t registerCount,
                            AOTChainRegisterAssignment* assignment) {
  if (assignment == nullptr) {
    return false;
  }
  const size_t instructionCount = instructionInputs.size();
  const auto inRange = [instructionCount](int index) {
    return index >= 0 && static_cast<size_t>(index) < instructionCount;
  };
  if ((rootInstruction != -1 && !inRange(rootInstruction)) ||
      (coverageRootInstruction != -1 && !inRange(coverageRootInstruction)) ||
      (clipCoverageInstruction != -1 && !inRange(clipCoverageInstruction))) {
    return false;
  }
  std::vector<int> lastUse(instructionCount, -1);
  for (size_t index = 0; index < instructionCount; ++index) {
    for (int input : instructionInputs[index]) {
      if (input < 0) {
        continue;
      }
      if (static_cast<size_t>(input) >= index) {
        return false;
      }
      lastUse[static_cast<size_t>(input)] = static_cast<int>(index);
    }
  }
  // The root, coverage-root and clip-coverage results are read after the whole loop, so their
  // registers outlive every release point inside it.
  if (rootInstruction >= 0) {
    lastUse[static_cast<size_t>(rootInstruction)] = static_cast<int>(instructionCount);
  }
  if (coverageRootInstruction >= 0) {
    lastUse[static_cast<size_t>(coverageRootInstruction)] = static_cast<int>(instructionCount);
  }
  if (clipCoverageInstruction >= 0) {
    lastUse[static_cast<size_t>(clipCoverageInstruction)] = static_cast<int>(instructionCount);
  }
  std::vector<std::vector<int>> releaseAt(instructionCount);
  for (size_t index = 0; index < instructionCount; ++index) {
    if (lastUse[index] >= 0 && lastUse[index] < static_cast<int>(instructionCount)) {
      releaseAt[static_cast<size_t>(lastUse[index])].push_back(static_cast<int>(index));
    }
  }
  std::vector<bool> freeRegister(registerCount, true);
  assignment->outRegister.assign(instructionCount, -1);
  for (size_t index = 0; index < instructionCount; ++index) {
    // A consumer releases its producers' registers before allocating its own: the kernel
    // evaluates the instruction (reading its inputs) before writing the result slot, so
    // reusing a just-consumed register is well-defined and lets a linear chain share a small
    // register set indefinitely.
    for (int producer : releaseAt[index]) {
      freeRegister[static_cast<size_t>(assignment->outRegister[static_cast<size_t>(producer)])] =
          true;
    }
    if (lastUse[index] == -1) {
      continue;
    }
    int reg = -1;
    for (size_t candidate = 0; candidate < registerCount; ++candidate) {
      if (freeRegister[candidate]) {
        reg = static_cast<int>(candidate);
        break;
      }
    }
    if (reg < 0) {
      return false;
    }
    freeRegister[static_cast<size_t>(reg)] = false;
    assignment->outRegister[index] = reg;
  }
  assignment->rootRegister =
      rootInstruction >= 0 ? assignment->outRegister[static_cast<size_t>(rootInstruction)] : -1;
  assignment->coverageRootRegister =
      coverageRootInstruction >= 0
          ? assignment->outRegister[static_cast<size_t>(coverageRootInstruction)]
          : -1;
  assignment->clipCoverageRegister =
      clipCoverageInstruction >= 0
          ? assignment->outRegister[static_cast<size_t>(clipCoverageInstruction)]
          : -1;
  return true;
}

}  // namespace tgfx
