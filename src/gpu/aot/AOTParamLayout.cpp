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
//  either express or implied. see the license for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/AOTParamLayout.h"
#include <string>
#include "gpu/UniformData.h"
#include "gpu/aot/AOTParamRegistry.h"

namespace tgfx {
namespace {

/// The ordinal families are bounded by what a kernel can declare; the probe stops at the first
/// missing instance long before this.
constexpr size_t kMaxOrdinalProbe = 64;

}  // namespace

std::unique_ptr<AOTParamLayout> AOTParamLayout::Resolve(const UniformData& uniforms) {
  auto layout = std::make_unique<AOTParamLayout>();
  const auto& params = AllParamList();
  layout->_slots.resize(params.size());
  for (size_t id = 0; id < params.size(); ++id) {
    auto declared = uniforms.fieldMap.find(params[id].name);
    if (declared == uniforms.fieldMap.end()) {
      continue;
    }
    const auto& first = declared->second;
    auto& slot = layout->_slots[id];
    if (first.arraySize > 1) {
      // A reflected array: instance k sits at base + k * elementStride.
      slot.base = first.offset;
      slot.stride = first.elementStride;
      slot.elementSize = first.elementSize;
      slot.count = static_cast<uint16_t>(first.arraySize);
      continue;
    }
    // An ordinal family (Subset / Subset_1 / ...): the addressable form needs the declared
    // instances equidistant, so the probe collects them and checks before committing.
    const UniformData::Field* instances[kMaxOrdinalProbe] = {&first};
    size_t found = 1;
    while (found < kMaxOrdinalProbe) {
      auto suffixed =
          uniforms.fieldMap.find(std::string(params[id].name) + "_" + std::to_string(found));
      if (suffixed == uniforms.fieldMap.end()) {
        break;
      }
      instances[found] = &suffixed->second;
      ++found;
    }
    if (found == 1) {
      slot.base = first.offset;
      slot.stride = 0;
      slot.elementSize = first.elementSize;
      slot.count = 1;
      continue;
    }
    size_t stride = instances[1]->offset - instances[0]->offset;
    bool regular = stride > 0;
    for (size_t k = 1; k < found; ++k) {
      regular = regular && instances[k]->offset - instances[k - 1]->offset == stride &&
                instances[k]->elementSize == first.elementSize;
    }
    if (regular) {
      slot.base = first.offset;
      slot.stride = stride;
      slot.elementSize = first.elementSize;
      slot.count = static_cast<uint16_t>(found);
    }
    // An irregular family keeps count = 0: the name-addressed path stays correct for it.
  }
  return layout;
}

}  // namespace tgfx
