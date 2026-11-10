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
#include <cassert>
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
    // A bare-names module (Frame, Xfer, the declared Chain) carries no dim prefix, so the shared
    // HAS_XP lookup the profile filters use keeps working and the chain's hooks read the legacy
    // dim macros verbatim.
    owned->emplace_back(module->bareNames
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
    AppendDomainDims(family.geometries[0], nullptr, &owned);
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
  // The ordinal composes the CHOSEN geometry's dims on the stride after the geometry, so every
  // geometry must declare the dims positionally identical and the domain carries one copy. A
  // family whose geometries disagree cannot be flat-decoded; the assert keeps that from slipping
  // through as a silently wrong valueOf.
  AppendDomainEntries(family.geometries[0], nullptr, owned, &nameIndex, &dims);
  for (uint8_t geometry = 1; geometry < family.geometryCount; ++geometry) {
    const Module& other = *family.geometries[geometry];
    const Module& first = *family.geometries[0];
    assert(other.dimCount == first.dimCount);
    for (uint8_t dim = 0; dim < other.dimCount; ++dim) {
      assert(other.dims[dim].count == first.dims[dim].count);
    }
  }
  for (uint8_t index = 0; index < family.moduleCount; ++index) {
    AppendDomainEntries(family.modules[index].module, family.modules[index].pinned, owned,
                        &nameIndex, &dims);
  }
  AppendDomainEntries(&XferModule(), nullptr, owned, &nameIndex, &dims);
  PermutationDomain domain(dims);
  return PrecompiledShaderInfo{family.name, "", "", domain, domain, family.textureReads};
}

uint8_t ChainGeometryOfCell(const ChainLegacyCell& cell) {
  if (cell.gpLayout == 0) {
    if (cell.coverage != 0) {
      return cell.uvCoord != 0 ? 1 : 0;
    }
    return 2;
  }
  if (cell.gpLayout == 1) {
    return 3;
  }
  if (cell.gpLayout == 2) {
    return 4;
  }
  return 5;
}

uint8_t ChainProfileOfCell(const ChainLegacyCell& cell) {
  if (cell.textureCount == 0 && cell.mask == 0 && cell.resource == 0) {
    return 0;
  }
  if (cell.textureCount == 0 && cell.mask == 1 && cell.resource == 0) {
    return 1;
  }
  if (cell.textureCount == 1 && cell.mask == 0 && cell.resource == 0) {
    return 2;
  }
  if (cell.textureCount == 0 && cell.mask == 0 && cell.resource == 1) {
    return 3;
  }
  if (cell.textureCount == 0 && cell.mask == 1 && cell.resource == 1) {
    return 4;
  }
  if (cell.textureCount == 1 && cell.mask == 0 && cell.resource == 1) {
    return 5;
  }
  return 6;
}

bool DecodeChainCell(const Family& family, const Variant& variant, ChainLegacyCell* cell) {
  const uint8_t subset = DimValueOf(family, variant, 1, 0);
  const uint8_t stroke = DimValueOf(family, variant, 1, 1);
  const uint8_t profile = DimValueOf(family, variant, 2, 0);
  cell->subset = subset;
  cell->xp = DimValueOf(family, variant, 3, 0);
  cell->coverage = 1;
  cell->uvCoord = 0;
  switch (variant.geometry) {
    case 0:
      break;
    case 1:
      cell->uvCoord = 1;
      break;
    case 2:
      cell->coverage = 0;
      cell->uvCoord = 1;
      break;
    case 3:
      cell->gpLayout = 1;
      cell->coverage = 0;
      break;
    case 4:
      cell->gpLayout = 2;
      cell->coverage = 0;
      break;
    default:
      cell->gpLayout = 3 + stroke;
      cell->coverage = 0;
      break;
  }
  // PROFILE -> (texture count, mask, resource kind), the legacy cell terms.
  switch (profile) {
    case 0:
      break;
    case 1:
      cell->mask = 1;
      break;
    case 2:
      cell->textureCount = 1;
      break;
    case 3:
      cell->resource = 1;
      break;
    case 4:
      cell->mask = 1;
      cell->resource = 1;
      break;
    case 5:
      cell->textureCount = 1;
      cell->resource = 1;
      break;
    case 6:
      cell->textureCount = 1;
      cell->resource = 2;
      break;
    default:
      return false;
  }
  return true;
}

}  // namespace tgfx
