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
//  either express or implied. See the License for the specific language governing permissions and
//  limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/shaders/FamilyDomains.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "gpu/shaders/modules/Xfer.h"

namespace tgfx {

namespace {

void AppendDomainDims(const Module* module, const int8_t* pinned, std::vector<std::string>* owned) {
  if (module == nullptr) {
    return;
  }
  for (uint8_t dim = 0; dim < module->dimCount; ++dim) {
    if (pinned != nullptr && pinned[dim] >= 0) {
      continue;
    }
    // Frame and Xfer are implicit singletons: their dim macros carry no module prefix, so the
    // shared HAS_XP lookup the profile filters use keeps working.
    owned->emplace_back(ModuleIsImplicit(module)
                            ? module->dims[dim].name
                            : std::string(module->name) + "_" + module->dims[dim].name);
  }
}

// Builds the domain entries from the finished name list. The names must be complete before any
// c_str() is taken: growing the vector could reallocate and dangle every earlier pointer the
// stored dimensions keep.
void AppendDomainEntries(const Module* module, const int8_t* pinned,
                         const std::vector<std::string>& owned, size_t* nameIndex,
                         std::vector<PermutationDimension>* dims) {
  if (module == nullptr) {
    return;
  }
  for (uint8_t dim = 0; dim < module->dimCount; ++dim) {
    if (pinned != nullptr && pinned[dim] >= 0) {
      continue;
    }
    dims->emplace_back(PermutationInt(owned[*nameIndex].c_str(), module->dims[dim].count));
    ++*nameIndex;
  }
}

}  // namespace

PrecompiledShaderInfo AssembledFamilyInfo(const Family& family) {
  // The domain names must outlive the returned info: the registry keeps it for the process
  // lifetime and the matchers compare names against it. A prefixed dim name exists only as a
  // concatenation, so the strings are built once per family and kept for the process lifetime
  // (both callers build them from a single-threaded initialization path).
  static std::map<const Family*, std::vector<std::string>> ownedNames;
  std::vector<std::string>& owned = ownedNames[&family];
  if (owned.empty()) {
    for (uint8_t geometry = 0; geometry < family.geometryCount; ++geometry) {
      AppendDomainDims(family.geometries[geometry], nullptr, &owned);
    }
    for (uint8_t index = 0; index < family.moduleCount; ++index) {
      AppendDomainDims(family.modules[index].module, family.modules[index].pinned, &owned);
    }
    AppendDomainDims(&XferModule(), nullptr, &owned);
  }
  std::vector<PermutationDimension> dims;
  size_t nameIndex = 0;
  // The variant ordinal encodes the geometry index as its lowest radix (EnumerateVariants starts
  // the stride at the geometry count). A family with more than one geometry must model that radix
  // here too, or valueOf would decode an ordinal against a domain that lacks the geometry term
  // and report the wrong HAS_XP for the profile filters.
  if (family.geometryCount > 1) {
    dims.emplace_back(PermutationInt("GEOMETRY", family.geometryCount));
  }
  for (uint8_t geometry = 0; geometry < family.geometryCount; ++geometry) {
    AppendDomainEntries(family.geometries[geometry], nullptr, owned, &nameIndex, &dims);
  }
  for (uint8_t index = 0; index < family.moduleCount; ++index) {
    AppendDomainEntries(family.modules[index].module, family.modules[index].pinned, owned,
                        &nameIndex, &dims);
  }
  AppendDomainEntries(&XferModule(), nullptr, owned, &nameIndex, &dims);
  PermutationDomain domain(dims);
  return PrecompiledShaderInfo{family.name, "", "", domain, domain, 0};
}

}  // namespace tgfx
