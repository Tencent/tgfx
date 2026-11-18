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
//  either express or implied. see the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/chain/ChainBodyEmitter.h"
#include "gpu/shaders/contract/ChainComposition.h"

namespace tgfx {

// The decoded program as the contract layer's shape description: one row per slot with the
// selector precomputed by the one encoder-side definition (ChainSlotSelector), so the emitter and
// the runtime binder read the same bit layout without the contract layer depending on the IR.
ChainComposition DescribeChainComposition(const AOTProgram& program, bool xpNeedsBlendPass) {
  ChainComposition composition = {};
  composition.slots.reserve(program.slots.size());
  for (const auto& slot : program.slots) {
    ChainCompositionSlot described = {};
    described.op = static_cast<int>(slot.op);
    described.in0 = slot.in0;
    described.in1 = slot.in1;
    described.outRegister = slot.outRegister;
    described.selector = ChainSlotSelector(slot);
    described.blendMode = slot.blend.blendMode;
    described.multiplyInputAlpha = slot.blend.multiplyInputAlpha;
    described.inputMode = slot.constColor.inputMode;
    composition.slots.push_back(described);
  }
  composition.rootRegister = static_cast<int>(program.rootRegister);
  composition.coverageRootRegister = program.coverageRootRegister;
  composition.clipCoverageRegister = program.clipCoverageRegister;
  composition.xpNeedsBlendPass = xpNeedsBlendPass;
  // The masks come off the one derivation the runtime's own specialization reads, so a described
  // program and its live resolution can never disagree about a gate's value.
  const auto masks = ChainFeatureMasksOf(program);
  composition.usedOps = masks.usedOps;
  composition.blendModes = masks.blendModes;
  composition.textureFlags = masks.textureFlags;
  composition.gradientKinds = masks.gradientKinds;
  return composition;
}

std::string EmitComposedChainBodyGLSL(const AOTProgram& program, bool xpNeedsBlendPass) {
  return EmitComposedChainBodyGLSL(DescribeChainComposition(program, xpNeedsBlendPass));
}

bool ComposeChainFragmentGLSL(const AOTProgram& program, bool xpNeedsBlendPass, std::string* code) {
  return ComposeChainFragmentGLSL(DescribeChainComposition(program, xpNeedsBlendPass), code);
}

}  // namespace tgfx
