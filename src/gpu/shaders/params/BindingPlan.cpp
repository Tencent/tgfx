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
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/params/BindingPlan.h"
#include <string>
#include "gpu/ProgramInfo.h"
#include "gpu/UniformData.h"
#include "gpu/processors/FragmentProcessor.h"

namespace tgfx {

void FlattenProcessors(const ProgramInfo& programInfo, std::vector<const FragmentProcessor*>* out) {
  out->clear();
  for (size_t i = 0; i < programInfo.numFragmentProcessors(); ++i) {
    auto root = programInfo.getFragmentProcessor(i);
    FragmentProcessor::Iter iter(const_cast<FragmentProcessor*>(root));
    while (auto* fp = iter.next()) {
      out->push_back(fp);
    }
  }
}

bool BindingPlan::bindsFlattened(uint32_t flattenedIndex) const {
  for (const auto& entry : _entries) {
    if (entry.flattenedIndex == flattenedIndex) {
      return true;
    }
  }
  return false;
}

void BindingPlan::bind(const std::vector<const FragmentProcessor*>& flattened,
                       uint8_t* fragmentBuffer) const {
  for (const auto& entry : _entries) {
    if (entry.bind == nullptr) {
      continue;
    }
    ParamWriter writer(fragmentBuffer, &entry.resolved);
    if (entry.flattenedIndex == BindingPlanEntry::ABSENT_PROCESSOR) {
      entry.bind(nullptr, &writer);
      continue;
    }
    if (entry.flattenedIndex >= flattened.size()) {
      continue;
    }
    entry.bind(flattened[entry.flattenedIndex], &writer);
  }
}

BindingPlanBuilder::BindingPlanBuilder(std::vector<const FragmentProcessor*> flattened,
                                       const UniformData* fragmentUniforms)
    : flattened(std::move(flattened)), fragmentUniforms(fragmentUniforms) {
}

std::shared_ptr<const BindingPlan> BindingPlanBuilder::finish() {
  if (failed || fragmentUniforms == nullptr) {
    return nullptr;
  }
  for (auto& entry : entries) {
    for (size_t i = 0; i < entry.resolved.descs.size(); ++i) {
      const auto& desc = entry.resolved.descs[i];
      if (!entry.resolved.mask.test(desc.index)) {
        continue;
      }
      // The precompiled shader declares the suffixed name as its own field ("Subset_1"), so the
      // resolution appends the instance suffix and addresses the exact reflected field.
      auto* field = fragmentUniforms->findField(std::string(desc.name) + entry.nameSuffix);
      if (field == nullptr) {
        continue;  // absent from this variant; the writer skips it (setDataOptional semantics)
      }
      const size_t elementSize = ParamElementSize(desc.type);
      const bool isArray = desc.type == ParamType::Float4Array || desc.type == ParamType::IntArray;
      if (isArray) {
        if (field->elementStride == 0 || field->elementSize != elementSize) {
          return nullptr;  // the reflection disagrees with the declared element layout
        }
      } else if (field->size != elementSize) {
        return nullptr;  // the reflection disagrees with the declared size
      }
      entry.resolved.offsets[desc.index] = field->offset;
      entry.resolved.strides[desc.index] = isArray ? field->elementStride : 0;
    }
  }
  auto plan = std::make_shared<BindingPlan>();
  plan->_entries = std::move(entries);
  return plan;
}

}  // namespace tgfx
