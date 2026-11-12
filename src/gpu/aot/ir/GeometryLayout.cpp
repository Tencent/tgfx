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
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = true;
    params.viewMatrix = gp->getViewMatrix();
    params.uvMatrix = gp->getUVMatrix();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<QuadPerEdgeAAGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const QuadPerEdgeAAGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.uvMatrix = gp->getUVMatrix();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<NonAARRectGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const NonAARRectGeometryProcessor*>(geometry);
    const auto color = gp->getCommonColor();
    params.commonColor = color.value_or(PMColor());
    params.hasCommonColor = color.has_value();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<ComplexNonAARRectGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const ComplexNonAARRectGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<EllipseGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const EllipseGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<ComplexEllipseGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const ComplexEllipseGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isStroke = gp->isStroke();
  } else if (IsProcessor<HairlineLineGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const HairlineLineGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = true;
    params.viewMatrix = gp->getViewMatrix();
    params.coverage = gp->getCoverage();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<HairlineQuadGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const HairlineQuadGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = true;
    params.viewMatrix = gp->getViewMatrix();
    params.coverage = gp->getCoverage();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<MeshGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const MeshGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = !gp->getHasColors();
    params.viewMatrix = gp->getViewMatrix();
    params.hasColors = gp->getHasColors();
  } else if (IsProcessor<RoundStrokeRectGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const RoundStrokeRectGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.aaType = gp->getAAType();
  } else if (IsProcessor<ShapeInstancedGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const ShapeInstancedGeometryProcessor*>(geometry);
    params.viewMatrix = gp->getViewMatrix();
    params.uvMatrix = gp->getUVMatrix();
  } else if (IsProcessor<AtlasTextGeometryProcessor>(geometry)) {
    const auto* gp = static_cast<const AtlasTextGeometryProcessor*>(geometry);
    params.commonColor = gp->getCommonColor();
    params.hasCommonColor = gp->hasCommonColor();
    params.isAlphaOnly = gp->isAlphaOnly();
    params.atlasProxy = gp->getTextureProxy();
    params.aaType = gp->getAAType();
  }
  return params;
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

}  // namespace tgfx
