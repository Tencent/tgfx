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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. See the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include <vector>
#include "gpu/shaders/Families.h"
#include "gpu/shaders/KernelContract.h"
#include "gpu/shaders/contract/ChainISA.h"

namespace tgfx {

/// The capacities every compiled variant receives as defines, read from the declaration that the
/// assembler sizes the shipped arrays from: the Chain module's per-PROFILE slot counts are the one
/// definition of the instruction capacity, so the injected number cannot disagree with the array
/// the same variant declares. The runtime keeps only the register file (a local array the
/// declarations cannot express) and the pool layout in the kernel contract. Both the stage
/// compiler and the manifest's source digest prepend this list, so a capacity change moves the
/// bundle and its digest together.
inline std::vector<std::string> SlotCapacityDefines() {
  // PROFILE 0 is the plain chain (and the gradient shape, which shares its storage); PROFILE 6 is
  // the noise four-leaf cell, the 32-slot shape.
  return {"TGFX_CHAIN_SLOT_CAPACITY=" + std::to_string(ChainSlotPackedCounts[0]),
          "TGFX_NOISE_SLOT_CAPACITY=" + std::to_string(ChainSlotPackedCounts[6]),
          "TGFX_CHAIN_REGISTERS=" + std::to_string(MaxChainRegisters),
          "TGFX_CHAIN_DATA_SLOT_WORDS=" + std::to_string(ChainISA::PoolSlotWords),
          "TGFX_CHAIN_BLOCK_WORDS=" + std::to_string(ChainISA::BlockWords),
          "TGFX_CHAIN_BLOCK_GRADIENT=" + std::to_string(ChainISA::BlockGradientOffset)};
}

}  // namespace tgfx
