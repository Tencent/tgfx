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
#include <vector>
#include "gpu/aot/ir/GeometryParameters.h"
#include "tgfx/gpu/Attribute.h"

namespace tgfx {

/// The geometry facts a draw carries: the vertex/instance attribute layout (with the physical
/// slots precompiled shaders number their inputs by) and the uniform values the writers upload.
/// The single value every geometry reader addresses, so the concrete geometry-processor type stays
/// behind one derivation (GeometryLayoutOf) instead of at each consumer.
struct GeometryLayout {
  AOTGeometryParameters parameters = {};
  std::vector<Attribute> vertexAttributes = {};
  std::vector<Attribute> instanceAttributes = {};
};

/// The diagnostic name of a geometry kind (the signature's `GP=` field).
const char* GeometryKindName(GeometryKind kind);

/// The geometry layouts, built from the draw's own facts: each factory reproduces the processor's
/// declared slots (filtered by the same conditions, the physical slots keeping the declared
/// numbering) and its uniform values. These are the onMakeGeometry results; the double-track form
/// asserts them against GeometryLayoutOf while the processors remain.
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
