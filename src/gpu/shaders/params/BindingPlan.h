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

// The per-program parameter binding a migrated kernel declares. Part of the params/ layer; see
// ParamHandle.h and ParamWriter.h for the field and writer contracts.
#pragma once
#include <algorithm>
#include <memory>
#include <vector>
#include "gpu/shaders/params/ParamWriter.h"

namespace tgfx {

class FragmentProcessor;
class ProgramInfo;
class UniformData;

// Binds one module instance's parameters from the processor it reads. Implemented by the module;
// the plan owns the resolution, the module only moves values into the writer.
using ModuleBindFn = void (*)(const FragmentProcessor*, ParamWriter*);

// One declared instance of a module within a kernel. The flattened position is the processor's
// index in the kernel's DFS-flattened fragment-processor order (color roots first, then
// coverage roots, children in Iter order) - the order ProgramInfo's uniform walk uses, so the
// plan and the walk address the same processor for the same draw shape.
struct BindingPlanEntry {
  // The flattened index of an instance bound to no processor; never collides with a real one.
  static constexpr uint32_t ABSENT_PROCESSOR = 0xFFFFFFFFu;

  ModuleBindFn bind = nullptr;
  uint32_t flattenedIndex = 0;
  // Appended to every field name at resolution (the structural "_1" of a second same-class
  // instance), so the plan addresses the exact reflected field ("Subset" / "Subset_1").
  const char* nameSuffix = "";
  ResolvedInstance resolved;
};

// The per-program parameter binding: which processor feeds which module instance, and every
// field resolved to a byte offset in the program's fragment uniform block. Built once at
// program creation (same program key, same processor structure) and walked once per draw -
// no string lookups on the draw path.
class BindingPlan {
 public:
  // Whether the processor at the flattened position is bound by this plan; its onSetData is
  // skipped on the precompiled path when it is.
  bool bindsFlattened(uint32_t flattenedIndex) const;

  // Runs every entry's Bind against the draw's processors. The flattened list must be in the
  // order FlattenProcessors produces; entries whose processor is absent are skipped.
  void bind(const std::vector<const FragmentProcessor*>& flattened, uint8_t* fragmentBuffer) const;

  const std::vector<BindingPlanEntry>& entries() const {
    return _entries;
  }

 private:
  std::vector<BindingPlanEntry> _entries;
  friend class BindingPlanBuilder;
};

// Collects the module instances a kernel declares for one draw, then resolves them against the
// program's fragment uniform block. Structural mistakes (a processor outside the flattened
// list, a field index out of range, two instances on one processor, a declared size the
// reflection disagrees with) fail finish() and the program creation reports
// BindingPlanMismatch. A field this program's variant does not declare resolves to NO_FIELD
// and the writer skips it - the same semantics as setDataOptional; the build-time ownership
// check keeps the names honest.
class BindingPlanBuilder {
 public:
  BindingPlanBuilder(std::vector<const FragmentProcessor*> flattened,
                     const UniformData* fragmentUniforms);

  // Declares one module instance reading from processor. The module contract: a static
  // constexpr FieldDecls container of ParamDesc (ParamDescOf of its Param handles, dense
  // indices from zero) and a static Bind(const FragmentProcessor*, ParamWriter*). A null
  // processor is skipped - an optional instance that is absent this draw.
  template <typename Module>
  void add(const FragmentProcessor* processor, FieldMask mask, const char* nameSuffix = "") {
    if (processor == nullptr || failed) {
      return;
    }
    auto position = std::find(flattened.begin(), flattened.end(), processor);
    if (position == flattened.end()) {
      failed = true;
      return;
    }
    BindingPlanEntry entry = {};
    entry.bind = &Module::Bind;
    entry.flattenedIndex = static_cast<uint32_t>(position - flattened.begin());
    entry.nameSuffix = nameSuffix;
    entry.resolved.mask = mask;
    entry.resolved.descs.assign(Module::FieldDecls.begin(), Module::FieldDecls.end());
    entry.resolved.offsets.assign(entry.resolved.descs.size(), ResolvedInstance::NO_FIELD);
    entry.resolved.strides.assign(entry.resolved.descs.size(), 0);
    appendEntry(std::move(entry));
  }

  // Declares a processor-less instance whose Bind receives nullptr: the runtime default of a
  // field no draw-scoped processor owns (e.g. the device-mask switch when the program's
  // structure carries no mask FP).
  template <typename Module>
  void addAbsent(FieldMask mask) {
    if (failed) {
      return;
    }
    BindingPlanEntry entry = {};
    entry.bind = &Module::Bind;
    entry.flattenedIndex = BindingPlanEntry::ABSENT_PROCESSOR;
    entry.resolved.mask = mask;
    entry.resolved.descs.assign(Module::FieldDecls.begin(), Module::FieldDecls.end());
    entry.resolved.offsets.assign(entry.resolved.descs.size(), ResolvedInstance::NO_FIELD);
    entry.resolved.strides.assign(entry.resolved.descs.size(), 0);
    appendEntry(std::move(entry));
  }

  // Resolves every entry's masked fields against the fragment block. Returns null on the
  // structural failures documented above.
  std::shared_ptr<const BindingPlan> finish();

 private:
  void appendEntry(BindingPlanEntry entry) {
    for (size_t i = 0; i < entry.resolved.descs.size(); ++i) {
      if (entry.resolved.descs[i].index != i) {
        failed = true;  // indices must be dense from zero, in FieldDecls order
        return;
      }
    }
    if (entry.flattenedIndex != BindingPlanEntry::ABSENT_PROCESSOR) {
      for (const auto& existing : entries) {
        if (existing.flattenedIndex == entry.flattenedIndex) {
          failed = true;  // one processor feeds one instance; two is a family bug
          return;
        }
      }
    }
    entries.push_back(std::move(entry));
  }

  std::vector<const FragmentProcessor*> flattened;
  const UniformData* fragmentUniforms = nullptr;
  std::vector<BindingPlanEntry> entries;
  bool failed = false;
};

// The DFS-flattened fragment-processor order the uniform walk and the binding plan share.
void FlattenProcessors(const ProgramInfo& programInfo, std::vector<const FragmentProcessor*>* out);

}  // namespace tgfx
