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
//  Unless required by applicable law or agreed to in writing, software distributed under the
//  License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
//  either express or implied. see the License for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/select/DimFacts.h"
#include "gpu/aot/select/DrawFacts.h"

namespace tgfx {

std::optional<uint8_t> DimValueOf(DimFact fact, const DrawShape& shape) {
  switch (fact) {
    case DimFact::Constant0:
      return 0;
    case DimFact::Constant1:
      return 1;
    case DimFact::XpType: {
      const int xpType = shape.xpType();
      if (xpType < 0) {
        return std::nullopt;
      }
      return static_cast<uint8_t>(xpType);
    }
    case DimFact::DeviceMaskPresent:
      return shape.coverage.has_value() && *shape.coverage == Coverage::DeviceMask ? 1 : 0;
    case DimFact::LocalMaskPresent:
      return shape.coverage.has_value() && *shape.coverage == Coverage::LocalMask ? 1 : 0;
    case DimFact::AtlasCoverage:
      return shape.geometry != nullptr && shape.geometry->hasCoverage ? 1 : 0;
    case DimFact::Unspecified:
    case DimFact::GradientPresent:
    case DimFact::TiledFull:
    case DimFact::TiledPresent:
    case DimFact::UvCoord:
    case DimFact::Subset:
    case DimFact::UvMatrix:
    case DimFact::MeshTexCoords:
    case DimFact::VertexColor:
    case DimFact::YuvFormat:
    case DimFact::GlassDispersion:
    case DimFact::GlassShape:
      // Not wired yet: the family's rule still supplies these values itself.
      return std::nullopt;
  }
  return std::nullopt;
}

std::optional<Variant> ComposeVariantFromFacts(uint16_t familyIndex, uint8_t geometry,
                                               const DrawShape& shape) {
  if (familyIndex >= FamilyCount()) {
    return std::nullopt;
  }
  if (geometry >= Families()[familyIndex].geometryCount) {
    return std::nullopt;
  }
  const auto dims = FreeDimsOf(familyIndex, geometry);
  std::vector<uint8_t> values = {};
  values.reserve(dims.size());
  for (const auto& dim : dims) {
    const auto value = DimValueOf(dim.source, shape);
    if (!value.has_value() || *value >= dim.iterations) {
      return std::nullopt;
    }
    values.push_back(*value);
  }
  return EncodeVariant(familyIndex, geometry, values);
}

}  // namespace tgfx
