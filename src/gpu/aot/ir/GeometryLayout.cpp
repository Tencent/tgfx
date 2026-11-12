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
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the License.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "gpu/aot/ir/GeometryLayout.h"
#include "gpu/processors/AtlasTextGeometryProcessor.h"
#include "gpu/processors/ComplexEllipseGeometryProcessor.h"
#include "gpu/processors/ComplexNonAARRectGeometryProcessor.h"
#include "gpu/processors/DefaultGeometryProcessor.h"
#include "gpu/processors/EllipseGeometryProcessor.h"
#include "gpu/processors/HairlineLineGeometryProcessor.h"
#include "gpu/processors/HairlineQuadGeometryProcessor.h"
#include "gpu/processors/MeshGeometryProcessor.h"
#include "gpu/processors/NonAARRectGeometryProcessor.h"
#include "gpu/processors/QuadPerEdgeAAGeometryProcessor.h"
#include "gpu/processors/RoundStrokeRectGeometryProcessor.h"
#include "gpu/processors/ShapeInstancedGeometryProcessor.h"

namespace tgfx {

AOTGeometryParameters GeometryParametersOf(const GeometryProcessor* geometry) {
  AOTGeometryParameters params = {};
  if (geometry == nullptr) {
    return params;
  }
  if (IsProcessor<DefaultGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const DefaultGeometryProcessor*>(geometry);
    params.kind = GeometryKind::Default;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = true;
    params.viewMatrix = gp->getViewMatrix();
    params.uvMatrix = gp->getUVMatrix();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry);
    params.kind = GeometryKind::QuadPerEdgeAA;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.uvMatrix = gp->getUVMatrix();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<NonAARRectGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const NonAARRectGeometryProcessor*>(geometry);
    params.kind = GeometryKind::NonAARRect;
    const auto color = gp->getCommonColor();
    params.commonColor = color.value_or(PMColor());
    params.hasCommonColor = color.has_value();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<ComplexNonAARRectGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const ComplexNonAARRectGeometryProcessor*>(geometry);
    params.kind = GeometryKind::ComplexNonAARRect;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<EllipseGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const EllipseGeometryProcessor*>(geometry);
    params.kind = GeometryKind::Ellipse;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<ComplexEllipseGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const ComplexEllipseGeometryProcessor*>(geometry);
    params.kind = GeometryKind::ComplexEllipse;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<HairlineLineGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const HairlineLineGeometryProcessor*>(geometry);
    params.kind = GeometryKind::HairlineLine;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = true;
    params.viewMatrix = gp->getViewMatrix();
    params.coverage = gp->getCoverage();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<HairlineQuadGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const HairlineQuadGeometryProcessor*>(geometry);
    params.kind = GeometryKind::HairlineQuad;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = true;
    params.viewMatrix = gp->getViewMatrix();
    params.coverage = gp->getCoverage();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<MeshGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const MeshGeometryProcessor*>(geometry);
    params.kind = GeometryKind::Mesh;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = !gp->getHasColors();
    params.viewMatrix = gp->getViewMatrix();
    params.hasColors = gp->getHasColors();
  } else if (IsProcessor<RoundStrokeRectGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const RoundStrokeRectGeometryProcessor*>(geometry);
    params.kind = GeometryKind::RoundStrokeRect;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<ShapeInstancedGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const ShapeInstancedGeometryProcessor*>(geometry);
    params.kind = GeometryKind::ShapeInstanced;
    params.viewMatrix = gp->getViewMatrix();
    params.uvMatrix = gp->getUVMatrix();
  } else if (IsProcessor<AtlasTextGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const AtlasTextGeometryProcessor*>(geometry);
    params.kind = GeometryKind::AtlasText;
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isAlphaOnly = gp->isAlphaOnly();
    params.atlasProxy = gp->getTextureProxy();
    params.aaType = gp->getAAType();
  }
  return params;
}

const char* GeometryKindName(GeometryKind kind) {
  switch (kind) {
    case GeometryKind::Other:
      return "Other";
    case GeometryKind::Default:
      return "Default";
    case GeometryKind::QuadPerEdgeAA:
      return "QuadPerEdgeAA";
    case GeometryKind::NonAARRect:
      return "NonAARRect";
    case GeometryKind::ComplexNonAARRect:
      return "ComplexNonAARRect";
    case GeometryKind::Ellipse:
      return "Ellipse";
    case GeometryKind::ComplexEllipse:
      return "ComplexEllipse";
    case GeometryKind::HairlineLine:
      return "HairlineLine";
    case GeometryKind::HairlineQuad:
      return "HairlineQuad";
    case GeometryKind::Mesh:
      return "Mesh";
    case GeometryKind::RoundStrokeRect:
      return "RoundStrokeRect";
    case GeometryKind::ShapeInstanced:
      return "ShapeInstanced";
    case GeometryKind::AtlasText:
      return "AtlasText";
  }
  return "Other";
}

GeometryLayout GeometryLayoutOf(const GeometryProcessor* geometry) {
  GeometryLayout layout = {};
  if (geometry == nullptr) {
    return layout;
  }
  layout.parameters = GeometryParametersOf(geometry);
  layout.vertexAttributes = geometry->vertexAttributes();
  layout.instanceAttributes = geometry->instanceAttributes();
  layout.physicalSlots = geometry->vertexAttributeLocations();
  return layout;
}

GeometryLayout QuadGeometryLayoutOf(int width, int height, AAType aaType,
                                    const std::optional<PMColor>& commonColor,
                                    const std::optional<Matrix>& uvMatrix, bool hasSubset) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::QuadPerEdgeAA;
  // The processor's own default (an opaque white), so the facts match the derived layout
  // byte for byte even when the draw carries a per-vertex color.
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.uvMatrix = uvMatrix.value_or(Matrix::I());
  layout.parameters.aaType = aaType;
  (void)width;
  (void)height;
  // The same five declared slots the processor sets, filtered by the same conditions; the
  // filtered-away attribute keeps its declared slot, exactly as setVertexAttributes does.
  const Attribute declared[5] = {
      {"aPosition", VertexFormat::Float2},
      {"inCoverage", VertexFormat::Float},
      uvMatrix.has_value() ? Attribute{} : Attribute{"uvCoord", VertexFormat::Float2},
      {"inColor", VertexFormat::UByte4Normalized},
      hasSubset ? Attribute{"texSubset", VertexFormat::Float4} : Attribute{}};
  for (uint32_t slot = 0; slot < 5; ++slot) {
    if (declared[slot].empty()) {
      continue;
    }
    layout.vertexAttributes.push_back(declared[slot]);
    layout.physicalSlots.push_back(slot);
  }
  return layout;
}

bool SameGeometryLayout(const GeometryLayout& a, const GeometryLayout& b) {
  if (a.physicalSlots != b.physicalSlots ||
      a.vertexAttributes.size() != b.vertexAttributes.size() ||
      a.instanceAttributes.size() != b.instanceAttributes.size()) {
    return false;
  }
  for (size_t index = 0; index < a.vertexAttributes.size(); ++index) {
    if (a.vertexAttributes[index].name() != b.vertexAttributes[index].name() ||
        a.vertexAttributes[index].format() != b.vertexAttributes[index].format()) {
      return false;
    }
  }
  const auto& pa = a.parameters;
  const auto& pb = b.parameters;
  return pa.kind == pb.kind && pa.hasCommonColor == pb.hasCommonColor &&
         pa.commonColor == pb.commonColor && pa.viewMatrix == pb.viewMatrix &&
         pa.uvMatrix == pb.uvMatrix && pa.aaType == pb.aaType && pa.coverage == pb.coverage &&
         pa.isStroke == pb.isStroke && pa.isAlphaOnly == pb.isAlphaOnly &&
         pa.hasColors == pb.hasColors && pa.atlasProxy == pb.atlasProxy;
}

}  // namespace tgfx
