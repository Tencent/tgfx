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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "gpu/AAType.h"
#include "gpu/SamplerState.h"
#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/gpu/Attribute.h"

namespace tgfx {

/**
 * The draw's geometry kind as the matchers see it: one value per processor class the precompiled
 * kernels serve, and Other for everything else.
 */
enum class GeometryKind : uint8_t {
  Other = 0,
  Default,
  QuadPerEdgeAA,
  // The rounded-rect pairs (RRectDrawOp): the non-AA forms at AAType::None and the AA forms, each
  // in a plain and a per-corner-radii variant.
  NonAARRect,
  ComplexNonAARRect,
  Ellipse,
  ComplexEllipse,
  // The hairline line/quad pair (HairlineLineOp / HairlineQuadOp) and the triangle mesh
  // (MeshDrawOp).
  HairlineLine,
  HairlineQuad,
  Mesh,
  // The round-capped/joined stroke rect (RectDrawOp's round line join) and the instanced shape
  // batch (ShapeInstancedDrawOp; two families share this processor).
  RoundStrokeRect,
  ShapeInstanced,
  // The glyph atlas geometry (AtlasTextOp).
  AtlasText,
};

class ProgramInfo;
class TextureProxy;

/// The geometry facts a declared family's writers upload: everything the module walk reads off
/// the draw's geometry, snapshotted into one plain value. The writers address this instead of the
/// concrete geometry-processor type, so the geometry stays one data source whatever object
/// produced it. Fields a geometry does not use keep their defaults.
struct AOTGeometryParameters {
  /// Which geometry produced these facts: the matching layer's one classification and the
  /// diagnostic signature read it.
  GeometryKind kind = GeometryKind::Other;
  /// The paint color uniform and its flag (the exact color; per-vertex colors ride the attributes).
  PMColor commonColor = PMColor::Transparent();
  bool hasCommonColor = false;
  /// The view transform of the geometries that project local coordinates (rect, mesh, hairline).
  Matrix viewMatrix = Matrix::I();
  /// The sampling transform of the geometries that carry one inline (quad-per-edge-AA). Module
  /// coordinate matrices stay with their modules (TiledTexture, Texture).
  Matrix uvMatrix = Matrix::I();
  /// Whether the draw carries an inline sampling transform at all (the attribute layout and the
  /// matchers distinguish absent from identity).
  bool hasUVMatrix = false;
  /// Whether the inline transform has a perspective row (part of the program key).
  bool hasUVPerspective = false;
  AAType aaType = AAType::None;
  /// The hairline families' constant coverage factor.
  float coverage = 1.f;
  bool isStroke = false;
  /// The atlas color format (the glyph leaf's AlphaOnly uniform).
  bool isAlphaOnly = false;
  /// The mesh's per-vertex color flag (its HasCommonColor is the complement).
  bool hasColors = false;
  /// The mesh's texture-coordinate flag.
  bool hasTexCoords = false;
  /// The atlas's AA coverage attribute flag (AAType::Coverage).
  bool hasCoverage = false;
  /// The quad geometry's subset attribute flag.
  bool hasSubset = false;
  /// The atlas the glyph attributes index into.
  std::shared_ptr<TextureProxy> atlasProxy = nullptr;
  /// The atlas sampler state (the glyph draws' sampling options).
  SamplerState atlasSampler = {};
};

/// The geometry facts a draw carries: the vertex/instance attribute layout (with the physical
/// slots precompiled shaders number their inputs by) and the uniform values the writers upload.
/// The single value every geometry reader addresses, so a geometry flag has one definition instead
/// of being re-derived at each consumer.
struct GeometryLayout {
  AOTGeometryParameters parameters = {};
  std::vector<Attribute> vertexAttributes = {};
  std::vector<Attribute> instanceAttributes = {};
};

/// The diagnostic name of a geometry kind (the signature's `GP=` field).
const char* GeometryKindName(GeometryKind kind);

/// The geometry layouts, built from the draw's own facts: each factory reproduces the processor's
/// declared slots (filtered by the same conditions, the physical slots keeping the declared
/// numbering) and its uniform values. These are the onMakeGeometry results.
GeometryLayout DefaultGeometryLayout(PMColor color, AAType aaType, const Matrix& viewMatrix,
                                     const Matrix& uvMatrix);
GeometryLayout RoundStrokeRectGeometryLayout(AAType aaType, const std::optional<PMColor>& color,
                                             const std::optional<Matrix>& uvMatrix);
GeometryLayout NonAARRectGeometryLayout(bool stroke, const std::optional<PMColor>& commonColor);
GeometryLayout ComplexNonAARRectGeometryLayout(bool stroke,
                                               const std::optional<PMColor>& commonColor);
GeometryLayout EllipseGeometryLayout(bool stroke, const std::optional<PMColor>& commonColor);
GeometryLayout ComplexEllipseGeometryLayout(bool stroke, const std::optional<PMColor>& commonColor);
GeometryLayout HairlineLineGeometryLayout(PMColor color, const Matrix& viewMatrix,
                                          const std::optional<Matrix>& uvMatrix, float coverage,
                                          AAType aaType);
GeometryLayout HairlineQuadGeometryLayout(PMColor color, const Matrix& viewMatrix,
                                          const std::optional<Matrix>& uvMatrix, float coverage,
                                          AAType aaType);
GeometryLayout MeshGeometryLayout(bool hasTexCoords, bool hasColors, bool hasCoverage,
                                  PMColor color, const Matrix& viewMatrix);
GeometryLayout ShapeInstancedGeometryLayout(AAType aaType, bool hasColors, const Matrix& viewMatrix,
                                            const Matrix& uvMatrix);
GeometryLayout AtlasTextGeometryLayout(std::shared_ptr<TextureProxy> atlasProxy, AAType aaType,
                                       const std::optional<PMColor>& commonColor, bool alphaOnly,
                                       const SamplingOptions& sampling);

/// The quad-per-edge-AA geometry's layout (kept under its own name: the five declared slots
/// filter down to the live ones).
GeometryLayout QuadGeometryLayoutOf(int width, int height, AAType aaType,
                                    const std::optional<PMColor>& commonColor,
                                    const std::optional<Matrix>& uvMatrix, bool hasSubset);

}  // namespace tgfx
