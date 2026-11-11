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

#include "gpu/aot/chain/AOTChainSpec.h"
#include <string>

namespace tgfx {

SpecValues ResolveChainSpec(const AOTProgram& program, const std::vector<SpecFieldInfo>& fields) {
  SpecValues result;
  if (fields.empty()) {
    return result;
  }
  // The master switch: the injected compile is specialized.
  result.values.push_back(1);
  const auto& slots = program.slots;
  // The inactive tail's record is the zeroed slot, the same value the runtime binder uploads
  // (its OP_NONE marker and zero selector protect the static texture prefetch).
  const AOTChainSlot inactiveSlot = {};
  for (const auto& field : fields) {
    if (field.name == "SlotCount") {
      result.values.push_back(static_cast<int32_t>(slots.size()));
      continue;
    }
    if (field.name == "SlotPacked") {
      for (uint32_t index = 0; index < field.arraySize; ++index) {
        const auto& slot = index < slots.size() ? slots[index] : inactiveSlot;
        result.values.push_back(static_cast<int32_t>(slot.op));
        result.values.push_back(slot.in0);
        result.values.push_back(slot.in1);
        result.values.push_back(ChainSlotSelector(slot));
      }
      continue;
    }
    if (field.name == "CoverageRootIndex") {
      result.values.push_back(program.coverageRootRegister);
      continue;
    }
    if (field.name == "RootIndex") {
      result.values.push_back(static_cast<int32_t>(program.rootRegister));
      continue;
    }
    if (field.name == "ClipCoverageRegister") {
      result.values.push_back(program.clipCoverageRegister);
      continue;
    }
    // A field this family does not carry means the declaration and the subject drifted apart;
    // specializing with a partial set would inject the wrong constants.
    return SpecValues();
  }
  return result;
}

}  // namespace tgfx
