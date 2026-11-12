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

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>
#include "gpu/ProgramInfo.h"
#include "gpu/ProxyProvider.h"
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/chain/AOTChainInternal.h"
#include "gpu/aot/chain/AOTChainRegisterAllocator.h"
#include "gpu/aot/chain/AOTShapeRules.h"
#include "gpu/aot/chain/ChainContext.h"
#include "gpu/aot/ir/DrawIR.h"
#include "gpu/aot/ir/NodeContract.h"
#include "gpu/processors/ComposeFragmentProcessor.h"
#include "gpu/processors/ConstColorProcessor.h"
#include "gpu/processors/DeviceSpaceTextureEffect.h"
#include "gpu/processors/PerlinNoiseFragmentProcessor.h"
#include "gpu/processors/RRectEffect.h"
#include "gpu/processors/RectEffect.h"
#include "gpu/processors/TextureEffect.h"

namespace tgfx {

// The Registers stage: register allocation. The slot wiring built above uses instruction
// ordinals; this
// pass rewrites every reference into the kernel's recycled chainResults registers, so the
// per-profile instruction budget (16 for Chain and gradient, 32 for noise) and the live-result
// budget (MaxChainRegisters) are independent. A DAG exceeding the register budget is refused here.
bool AllocateRegisters(AOTChainContext* ctx) {
  if (ctx->slots.size() > MaxChainSlots) {
    return false;
  }
  std::vector<std::vector<int>> instructionInputs(ctx->slots.size());
  for (size_t index = 0; index < ctx->slots.size(); ++index) {
    for (int input : {ctx->slots[index].in0, ctx->slots[index].in1}) {
      if (input >= 0) {
        instructionInputs[index].push_back(input);
      }
    }
  }
  ctx->assignment = {};
  if (!AllocateChainRegisters(instructionInputs, static_cast<int>(ctx->rootIndex),
                              ctx->coverageRootSlot, ctx->clipCoverageSlot, MaxChainRegisters,
                              &ctx->assignment)) {
    return false;
  }
  for (size_t index = 0; index < ctx->slots.size(); ++index) {
    if (ctx->slots[index].in0 >= 0) {
      ctx->slots[index].in0 =
          ctx->assignment.outRegister[static_cast<size_t>(ctx->slots[index].in0)];
    }
    if (ctx->slots[index].in1 >= 0) {
      ctx->slots[index].in1 =
          ctx->assignment.outRegister[static_cast<size_t>(ctx->slots[index].in1)];
    }
    ctx->slots[index].outRegister = ctx->assignment.outRegister[index];
  }
  return true;
}

}  // namespace tgfx
