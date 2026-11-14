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

PrecompiledShaderInfo AssembledFamilyInfo(const Family& family) {
  // The declaration is the only variant description left: the info names the family and carries
  // the capability bits the matcher gates on. There is no domain to assemble, because the variant
  // space is what EnumerateVariants walks.
  return PrecompiledShaderInfo{family.name, "", "", family.textureReads};
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
  if (cell.gpLayout == 3) {
    return 5;
  }
  if (cell.gpLayout == 5) {
    return 6;
  }
  if (cell.gpLayout == 7) {
    return 7;
  }
  return 8;
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

uint32_t VariantIdOf(uint16_t familyIndex, uint32_t ordinal) {
  // The runtime's variant identity (see Variant::id): the family index in the high bits and the
  // family-local ordinal in the low 20. The ordinal is the mixed-radix encoding, not the
  // enumeration index, so it is unique within its family but sparse.
  if (familyIndex >= FamilyCount() || ordinal >= (1u << 20)) {
    return UINT32_MAX;
  }
  return (static_cast<uint32_t>(familyIndex) << 20) | ordinal;
}

}  // namespace tgfx
