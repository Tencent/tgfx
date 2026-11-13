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

namespace tgfx {

namespace {

/// Appends one declared attribute; an empty attribute contributes no entry (the
/// setVertexAttributes rule the processors follow). The list is the geometry's attribute order —
/// the program's shader locations come from the declaration, so no slot numbering is kept here.
void AppendSlot(GeometryLayout* layout, const Attribute& attribute) {
  if (attribute.empty()) {
    return;
  }
  layout->vertexAttributes.push_back(attribute);
}

void AppendInstanceSlot(GeometryLayout* layout, const Attribute& attribute) {
  if (!attribute.empty()) {
    layout->instanceAttributes.push_back(attribute);
  }
}

}  // namespace

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

GeometryLayout DefaultGeometryLayout(PMColor color, AAType aaType, const Matrix& viewMatrix,
                                     const Matrix& uvMatrix) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::Default;
  layout.parameters.commonColor = color;
  layout.parameters.hasCommonColor = true;
  layout.parameters.viewMatrix = viewMatrix;
  layout.parameters.uvMatrix = uvMatrix;
  layout.parameters.hasUVMatrix = uvMatrix != Matrix::I();
  layout.parameters.hasUVPerspective = uvMatrix.hasPerspective();
  layout.parameters.aaType = aaType;
  AppendSlot(&layout, Attribute{"aPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inCoverage", VertexFormat::Float});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  return layout;
}

GeometryLayout QuadGeometryLayoutOf(int width, int height, AAType aaType,
                                    const std::optional<PMColor>& commonColor,
                                    const std::optional<Matrix>& uvMatrix, bool hasSubset) {
  (void)width;
  (void)height;
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::QuadPerEdgeAA;
  // The processor's own default (an opaque white), so the facts match the derived layout
  // byte for byte even when the draw carries a per-vertex color.
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.uvMatrix = uvMatrix.value_or(Matrix::I());
  layout.parameters.hasUVMatrix = uvMatrix.has_value();
  layout.parameters.hasUVPerspective =
      uvMatrix.has_value() && (uvMatrix->getType() & Matrix::PerspectiveMask) != 0;
  layout.parameters.aaType = aaType;
  layout.parameters.hasSubset = hasSubset;
  AppendSlot(&layout, Attribute{"aPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inCoverage", VertexFormat::Float});
  AppendSlot(&layout,
             uvMatrix.has_value() ? Attribute{} : Attribute{"uvCoord", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  AppendSlot(&layout, hasSubset ? Attribute{"texSubset", VertexFormat::Float4} : Attribute{});
  return layout;
}

GeometryLayout RoundStrokeRectGeometryLayout(AAType aaType,
                                             const std::optional<PMColor>& commonColor,
                                             const std::optional<Matrix>& uvMatrix) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::RoundStrokeRect;
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.uvMatrix = uvMatrix.value_or(Matrix::I());
  layout.parameters.hasUVMatrix = uvMatrix.has_value();
  layout.parameters.hasUVPerspective =
      uvMatrix.has_value() && (uvMatrix->getType() & Matrix::PerspectiveMask) != 0;
  layout.parameters.aaType = aaType;
  AppendSlot(&layout, Attribute{"inPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inCoverage", VertexFormat::Float});
  AppendSlot(&layout, Attribute{"inEllipseOffset", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inEllipseRadii", VertexFormat::Float2});
  AppendSlot(&layout,
             uvMatrix.has_value() ? Attribute{} : Attribute{"inUVCoord", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  return layout;
}

GeometryLayout NonAARRectGeometryLayout(bool stroke, const std::optional<PMColor>& commonColor) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::NonAARRect;
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.isStroke = stroke;
  AppendSlot(&layout, Attribute{"inPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inLocalCoord", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inRadii", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inRectBounds", VertexFormat::Float4});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  // The stroke width slot is unconditional (a fill batch writes zeros), matching the module's
  // always-declared attribute and the provider's constant stride.
  AppendSlot(&layout, Attribute{"inStrokeWidth", VertexFormat::Float2});
  return layout;
}

GeometryLayout ComplexNonAARRectGeometryLayout(bool stroke,
                                               const std::optional<PMColor>& commonColor) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::ComplexNonAARRect;
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.isStroke = stroke;
  AppendSlot(&layout, Attribute{"inPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inLocalCoord", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inXRadii", VertexFormat::Float4});
  AppendSlot(&layout, Attribute{"inYRadii", VertexFormat::Float4});
  AppendSlot(&layout, Attribute{"inRectBounds", VertexFormat::Float4});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  AppendSlot(&layout, Attribute{"inStrokeWidth", VertexFormat::Float2});
  return layout;
}

GeometryLayout EllipseGeometryLayout(bool stroke, const std::optional<PMColor>& commonColor) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::Ellipse;
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.isStroke = stroke;
  AppendSlot(&layout, Attribute{"inPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  AppendSlot(&layout, Attribute{"inEllipseOffset", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inEllipseRadii", VertexFormat::Float4});
  return layout;
}

GeometryLayout ComplexEllipseGeometryLayout(bool stroke,
                                            const std::optional<PMColor>& commonColor) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::ComplexEllipse;
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.isStroke = stroke;
  AppendSlot(&layout, Attribute{"inPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  AppendSlot(&layout, Attribute{"inEllipseOffset", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inEllipseRadii", VertexFormat::Float4});
  AppendSlot(&layout, Attribute{"inEdgeDist", VertexFormat::Float2});
  // The stroke width slot is unconditional (a fill batch writes zeros), matching the module's
  // always-declared attribute and the provider's constant stride.
  AppendSlot(&layout, Attribute{"inStrokeWidth", VertexFormat::Float2});
  return layout;
}

GeometryLayout HairlineLineGeometryLayout(PMColor color, const Matrix& viewMatrix,
                                          const std::optional<Matrix>& uvMatrix, float coverage,
                                          AAType aaType) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::HairlineLine;
  layout.parameters.commonColor = color;
  layout.parameters.hasCommonColor = true;
  layout.parameters.viewMatrix = viewMatrix;
  layout.parameters.uvMatrix = uvMatrix.value_or(Matrix::I());
  layout.parameters.hasUVMatrix = uvMatrix.has_value();
  layout.parameters.hasUVPerspective =
      uvMatrix.has_value() && (uvMatrix->getType() & Matrix::PerspectiveMask) != 0;
  layout.parameters.coverage = coverage;
  layout.parameters.aaType = aaType;
  AppendSlot(&layout, Attribute{"aPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"aEdgeDistance", VertexFormat::Float});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  return layout;
}

GeometryLayout HairlineQuadGeometryLayout(PMColor color, const Matrix& viewMatrix,
                                          const std::optional<Matrix>& uvMatrix, float coverage,
                                          AAType aaType) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::HairlineQuad;
  layout.parameters.commonColor = color;
  layout.parameters.hasCommonColor = true;
  layout.parameters.viewMatrix = viewMatrix;
  layout.parameters.uvMatrix = uvMatrix.value_or(Matrix::I());
  layout.parameters.hasUVMatrix = uvMatrix.has_value();
  layout.parameters.hasUVPerspective =
      uvMatrix.has_value() && (uvMatrix->getType() & Matrix::PerspectiveMask) != 0;
  layout.parameters.coverage = coverage;
  layout.parameters.aaType = aaType;
  AppendSlot(&layout, Attribute{"aPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"hairQuadEdge", VertexFormat::Float4});
  return layout;
}

GeometryLayout MeshGeometryLayout(bool hasTexCoords, bool hasColors, bool hasCoverage,
                                  PMColor color, const Matrix& viewMatrix) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::Mesh;
  layout.parameters.commonColor = color;
  layout.parameters.hasCommonColor = !hasColors;
  layout.parameters.hasColors = hasColors;
  layout.parameters.viewMatrix = viewMatrix;
  layout.parameters.hasTexCoords = hasTexCoords;
  layout.parameters.hasCoverage = hasCoverage || hasTexCoords || hasColors;
  AppendSlot(&layout, Attribute{"aPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"aCoverage", VertexFormat::Float});
  AppendSlot(&layout, hasTexCoords ? Attribute{"aTexCoord", VertexFormat::Float2} : Attribute{});
  AppendSlot(&layout, Attribute{"aColor", VertexFormat::UByte4Normalized});
  return layout;
}

GeometryLayout ShapeInstancedGeometryLayout(AAType aaType, bool hasColors, const Matrix& viewMatrix,
                                            const Matrix& uvMatrix) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::ShapeInstanced;
  layout.parameters.viewMatrix = viewMatrix;
  layout.parameters.uvMatrix = uvMatrix;
  layout.parameters.hasUVMatrix = uvMatrix != Matrix::I();
  layout.parameters.hasUVPerspective = uvMatrix.hasPerspective();
  layout.parameters.aaType = aaType;
  layout.parameters.hasColors = hasColors;
  AppendSlot(&layout, Attribute{"aPosition", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inCoverage", VertexFormat::Float});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  AppendInstanceSlot(&layout, Attribute{"aOffset", VertexFormat::Float2});
  AppendInstanceSlot(&layout, Attribute{"aColor", VertexFormat::UByte4Normalized});
  return layout;
}

GeometryLayout AtlasTextGeometryLayout(std::shared_ptr<TextureProxy> atlasProxy, AAType aaType,
                                       const std::optional<PMColor>& commonColor, bool alphaOnly,
                                       const SamplingOptions& sampling) {
  GeometryLayout layout = {};
  layout.parameters.kind = GeometryKind::AtlasText;
  layout.parameters.commonColor = commonColor.value_or(PMColor());
  layout.parameters.hasCommonColor = commonColor.has_value();
  layout.parameters.atlasProxy = std::move(atlasProxy);
  layout.parameters.isAlphaOnly = alphaOnly;
  layout.parameters.aaType = aaType;
  layout.parameters.hasCoverage = aaType == AAType::Coverage;
  layout.parameters.atlasSampler = SamplerState(sampling);
  AppendSlot(&layout, Attribute{"aPosition", VertexFormat::Float2});
  AppendSlot(&layout, aaType == AAType::Coverage ? Attribute{"inCoverage", VertexFormat::Float}
                                                 : Attribute{});
  AppendSlot(&layout, Attribute{"maskCoord", VertexFormat::Float2});
  AppendSlot(&layout, Attribute{"inColor", VertexFormat::UByte4Normalized});
  return layout;
}

}  // namespace tgfx
