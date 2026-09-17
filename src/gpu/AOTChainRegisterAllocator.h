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

#include <cstddef>
#include <vector>

namespace tgfx {

// Register assignment for one flattened chain: instruction i writes its result to
// outRegister[i] in the kernel's chainResults array. -1 marks an instruction whose result
// nothing ever reads.
struct AOTChainRegisterAssignment {
  std::vector<int> outRegister = {};
  int rootRegister = -1;
  int coverageRootRegister = -1;
};

// Assigns result registers to a topologically ordered instruction sequence so that a result's
// register is recycled once its last consumer has executed, decoupling the instruction count
// from the register count. instructionInputs[i] lists the instruction indices instruction i
// reads; negative entries are the kernel's special inputs (geometry color, coverage unit, ...)
// and are ignored. rootInstruction and coverageRootInstruction name the instructions whose
// results the kernel reads after the evaluation loop; -1 when absent. Returns false when the
// live ranges exceed registerCount, the sequence is not topologically ordered, or a root index
// is out of range.
bool AllocateChainRegisters(const std::vector<std::vector<int>>& instructionInputs,
                            int rootInstruction, int coverageRootInstruction, size_t registerCount,
                            AOTChainRegisterAssignment* assignment);

}  // namespace tgfx
