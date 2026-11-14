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
#include "gpu/aot/chain/AOTChainBuilder.h"
#include "gpu/aot/ir/EffectGraph.h"
#include "gpu/aot/select/DrawFacts.h"
#include "gpu/aot/select/ShaderMatchHelpers.h"
#include "gpu/proxies/TextureProxy.h"
#include "gpu/resources/TextureView.h"
#include "gpu/resources/YUVTextureView.h"

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
    case DimFact::GradientPresent:
      // The color expression is the instance color plus one evaluable clamped gradient: the same
      // predicate the family's rule uses to accept the draw.
      return shape.colorFPCount == 1 && shape.ir != nullptr &&
                     SoleGradientLeaf(shape.ir->color) != nullptr
                 ? 1
                 : 0;
    case DimFact::TiledFull:
      return std::nullopt;
    case DimFact::TiledPresent: {
      if (shape.ir == nullptr) {
        return 0;
      }
      AOTChainBuilder::AOTTextureTailShape tail = {};
      if (!AOTChainBuilder::CollectTextureTail(shape.ir->color, &tail) || tail.leaf == nullptr) {
        return 0;
      }
      return tail.leaf->samplingKind == AOTTextureSamplingKind::Tiled ? 1 : 0;
    }
    case DimFact::UvCoord:
      return shape.geometry != nullptr && shape.geometry->hasUVMatrix ? 0 : 1;
    case DimFact::Subset:
      return shape.geometry != nullptr && shape.geometry->hasSubset ? 1 : 0;
    case DimFact::UvMatrix:
      return shape.geometry != nullptr && shape.geometry->hasUVMatrix ? 1 : 0;
    case DimFact::MeshTexCoords:
      return shape.geometry != nullptr && shape.geometry->hasTexCoords ? 1 : 0;
    case DimFact::VertexColor:
      // The families that declare VERTEX_COLOR keep it at 0 (the instanced batch carries its color
      // per instance), so the declaration annotates that dim Constant0 rather than this fact.
      return std::nullopt;
    case DimFact::YuvFormat: {
      if (shape.ir == nullptr) {
        return std::nullopt;
      }
      AOTChainBuilder::AOTTextureTailShape tail = {};
      if (!AOTChainBuilder::CollectYUVTail(shape.ir->color, &tail) || tail.leaf == nullptr) {
        return std::nullopt;
      }
      if (tail.leaf->yuvFormat >= 0) {
        return static_cast<uint8_t>(tail.leaf->yuvFormat);
      }
      auto view =
          tail.leaf->textureProxy != nullptr ? tail.leaf->textureProxy->getTextureView() : nullptr;
      if (view == nullptr || !view->isYUV()) {
        return std::nullopt;
      }
      const auto* yuvView = static_cast<const YUVTextureView*>(view.get());
      if (yuvView->yuvFormat() == YUVFormat::I420) {
        return 0;
      }
      if (yuvView->yuvFormat() == YUVFormat::NV12) {
        return 1;
      }
      return std::nullopt;
    }
    case DimFact::GlassDispersion:
    case DimFact::GlassShape:
    case DimFact::Unspecified:
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
