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

class GeometryProcessor;

/// The geometry facts a draw carries: the vertex/instance attribute layout (with the physical
/// slots precompiled shaders number their inputs by) and the uniform values the writers upload.
/// The single value every geometry reader addresses, so the concrete geometry-processor type stays
/// behind one derivation (GeometryLayoutOf) instead of at each consumer.
struct GeometryLayout {
  AOTGeometryParameters parameters = {};
  std::vector<Attribute> vertexAttributes = {};
  std::vector<Attribute> instanceAttributes = {};
  /// The physical vertex slot of each entry in vertexAttributes (a filtered-away attribute keeps
  /// its declared slot, so an attribute after an unused one does not shift).
  std::vector<uint32_t> physicalSlots = {};
};

/// The diagnostic name of a geometry kind (the signature's `GP=` field).
const char* GeometryKindName(GeometryKind kind);

/// Snapshots the geometry of the given processor. The one place the concrete geometry types are
/// discriminated; returns a default layout for a null processor.
GeometryLayout GeometryLayoutOf(const GeometryProcessor* geometryProcessor);

/// The uniform-value half alone, for callers that only need the writers' facts.
AOTGeometryParameters GeometryParametersOf(const GeometryProcessor* geometryProcessor);

/// The quad-per-edge-AA geometry's layout, built from the draw's own facts: the five declared
/// slots (position, coverage, uv coordinate, colour, subset) filter down to the live ones, and the
/// physical slots keep the declared numbering. The double-track form of onMakeGeometry: the same
/// value GeometryLayoutOf derives from the processor.
GeometryLayout QuadGeometryLayoutOf(int width, int height, AAType aaType,
                                    const std::optional<PMColor>& commonColor,
                                    const std::optional<Matrix>& uvMatrix, bool hasSubset);

/// Field-by-field equality, for the double-track assertion while the geometry processors remain.
bool SameGeometryLayout(const GeometryLayout& a, const GeometryLayout& b);

}  // namespace tgfx
