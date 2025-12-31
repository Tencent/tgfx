/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
//
//  Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
//  in compliance with the License. You may obtain a copy of the License at
//
//      https://opensource.org/licenses/BSD-3-Clause
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "QuadsVertexProvider.h"
#include <cmath>
#include "core/utils/ColorHelper.h"

namespace tgfx {

// CW to Z-order mapping for GPU triangle strip.
// CW order: 0=top-left, 1=top-right, 2=bottom-right, 3=bottom-left
// Z-order: 0=left-top, 1=left-bottom, 2=right-top, 3=right-bottom
// Mapping: Z[0]=CW[0], Z[1]=CW[3], Z[2]=CW[1], Z[3]=CW[2]
static constexpr size_t CW_TO_Z_ORDER[] = {0, 3, 1, 2};

// Maps edge index (0-3) to corresponding AA flag.
static constexpr unsigned EDGE_AA_FLAGS[] = {
    QUAD_AA_FLAG_EDGE_01,  // Edge 0: v0 -> v1
    QUAD_AA_FLAG_EDGE_12,  // Edge 1: v1 -> v2
    QUAD_AA_FLAG_EDGE_23,  // Edge 2: v2 -> v3
    QUAD_AA_FLAG_EDGE_30   // Edge 3: v3 -> v0
};

/**
 * Writes a quad's vertices to the output buffer in Z-order.
 * @param totalVerts Output buffer for all vertex data.
 * @param globalIndex Current write position in totalVerts, updated after writing.
 * @param quadVerts The 4 vertices of the quad in CW order.
 * @param coverage Coverage value for AA (1.0 for inner, 0.0 for outer).
 * @param hasColor Whether to write color data.
 * @param compressedColor Compressed color value to write if hasColor is true.
 */
static void WriteQuadVertices(float* totalVerts, size_t& globalIndex, const Point* quadVerts,
                              float coverage, bool hasColor, float compressedColor) {
  for (int i = 0; i < 4; ++i) {
    const Point& p = quadVerts[CW_TO_Z_ORDER[i]];
    totalVerts[globalIndex++] = p.x;
    totalVerts[globalIndex++] = p.y;
    totalVerts[globalIndex++] = coverage;
    if (hasColor) {
      totalVerts[globalIndex++] = compressedColor;
    }
  }
}

/**
 * NonAAQuadsVertexProvider generates vertices for non-AA quad rendering.
 */
class NonAAQuadsVertexProvider : public QuadsVertexProvider {
 public:
  NonAAQuadsVertexProvider(PlacementArray<QuadRecord>&& quads, AAType aaType, bool hasColor,
                           std::shared_ptr<BlockAllocator> reference)
      : QuadsVertexProvider(std::move(quads), aaType, hasColor, std::move(reference)) {
  }

  size_t vertexCount() const override {
    // Each quad = 4 vertices, vertex data: position(2) + [color(1)]
    const size_t perVertexCount = _hasColor ? 3 : 2;
    return quads.size() * 4 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    for (size_t i = 0; i < quads.size(); ++i) {
      auto& record = quads[i];
      float compressedColor = 0.f;
      if (_hasColor) {
        const uint32_t uintColor = ToUintPMColor(record->color, nullptr);
        compressedColor = *reinterpret_cast<const float*>(&uintColor);
      }
      // Write 4 vertices in Z-order to match index buffer layout.
      for (size_t j = 0; j < 4; ++j) {
        const Point& p = record->quad.point(CW_TO_Z_ORDER[j]);
        vertices[index++] = p.x;
        vertices[index++] = p.y;
        if (_hasColor) {
          vertices[index++] = compressedColor;
        }
      }
    }
  }
};

/**
 * AAQuadsVertexProvider generates vertices for per-edge AA quad rendering.
 */
class AAQuadsVertexProvider : public QuadsVertexProvider {
 public:
  AAQuadsVertexProvider(PlacementArray<QuadRecord>&& quads, AAType aaType, bool hasColor,
                        std::shared_ptr<BlockAllocator> reference)
      : QuadsVertexProvider(std::move(quads), aaType, hasColor, std::move(reference)) {
  }

  size_t vertexCount() const override {
    // Each AA quad = 8 vertices (4 inner + 4 outer)
    // Vertex data: position(2) + coverage(1) + [color(1)]
    const size_t perVertexCount = _hasColor ? 4 : 3;
    return quads.size() * 8 * perVertexCount;
  }

  void getVertices(float* vertices) const override {
    size_t index = 0;
    for (size_t i = 0; i < quads.size(); ++i) {
      auto& record = quads[i];
      float compressedColor = 0.f;
      if (_hasColor) {
        const uint32_t uintColor = ToUintPMColor(record->color, nullptr);
        compressedColor = *reinterpret_cast<const float*>(&uintColor);
      }
      writeAAQuadVertices(vertices, index, *record, compressedColor);
    }
  }

 private:
  void writeAAQuadVertices(float* vertices, size_t& index, const QuadRecord& record,
                           float compressedColor) const {
    const QuadCW& quad = record.quad;
    // Compute inward normals for each edge (perpendicular to edge, pointing inward for CW winding)
    Point normals[4];
    for (size_t i = 0; i < 4; ++i) {
      const size_t next = (i + 1) % 4;
      const Point edge = quad.point(next) - quad.point(i);
      const float len = std::sqrt(edge.x * edge.x + edge.y * edge.y);
      if (len > 0) {
        normals[i] = Point::Make(-edge.y / len, edge.x / len);
      } else {
        normals[i] = Point::Make(0, 0);
      }
    }

    // AA offset distance for vertex expansion/contraction
    constexpr float kAAOffset = 0.5f;
    Point insetVertices[4];
    Point outsetVertices[4];
    for (size_t i = 0; i < 4; ++i) {
      // Each vertex is affected by two edges (the one ending at it and the one starting from it)
      const size_t prevEdge = (i + 3) % 4;  // Edge ending at this vertex
      const size_t nextEdge = i;            // Edge starting at this vertex
      const bool prevNeedsAA = (record.aaFlags & EDGE_AA_FLAGS[prevEdge]) != 0;
      const bool nextNeedsAA = (record.aaFlags & EDGE_AA_FLAGS[nextEdge]) != 0;
      // Compute offset direction based on which edges need AA:
      // - Both edges AA: use bisector (sum of two normals)
      // - Only one edge AA: use that edge's normal only
      // - No edges AA: no offset
      Point offsetDir = {};
      if (prevNeedsAA && nextNeedsAA) {
        offsetDir = normals[prevEdge] + normals[nextEdge];
      } else if (prevNeedsAA) {
        offsetDir = normals[prevEdge];
      } else if (nextNeedsAA) {
        offsetDir = normals[nextEdge];
      }
      const float offsetLen = std::sqrt(offsetDir.x * offsetDir.x + offsetDir.y * offsetDir.y);
      if (offsetLen > 0) {
        offsetDir.x /= offsetLen;
        offsetDir.y /= offsetLen;
      }

      // Note: For acute angles, the geometrically correct miter point would be farther than
      // kAAOffset from the corner (distance = kAAOffset / cos(theta/2)). However, we intentionally
      // use a fixed offset distance because:
      // 1. Coverage is interpolated between vertices, so exact miter geometry isn't required for AA.
      // 2. True miter points can extend infinitely at very acute angles, causing numerical issues.
      // 3. Simpler calculation with better performance.
      const Point corner = quad.point(i);
      insetVertices[i] = corner + offsetDir * kAAOffset;
      outsetVertices[i] = corner - offsetDir * kAAOffset;
    }

    // Write inner quad (coverage = 1.0) and outer quad (coverage = 0.0) in Z-order
    WriteQuadVertices(vertices, index, insetVertices, 1.0f, _hasColor, compressedColor);
    WriteQuadVertices(vertices, index, outsetVertices, 0.0f, _hasColor, compressedColor);
  }
};

PlacementPtr<QuadsVertexProvider> QuadsVertexProvider::MakeFrom(BlockAllocator* allocator,
                                                                const Rect& rect, AAType aaType,
                                                                const Color& color) {
  QuadCW quad(Point::Make(rect.left, rect.top), Point::Make(rect.right, rect.top),
              Point::Make(rect.right, rect.bottom), Point::Make(rect.left, rect.bottom));
  auto record = allocator->make<QuadRecord>(quad, QUAD_AA_FLAG_ALL, color);
  std::vector<PlacementPtr<QuadRecord>> quads;
  quads.push_back(std::move(record));
  return MakeFrom(allocator, std::move(quads), aaType);
}

PlacementPtr<QuadsVertexProvider> QuadsVertexProvider::MakeFrom(
    BlockAllocator* allocator, std::vector<PlacementPtr<QuadRecord>>&& quads, AAType aaType) {
  if (quads.empty()) {
    return nullptr;
  }
  bool hasColor = false;
  if (quads.size() > 1) {
    auto& firstColor = quads.front()->color;
    for (auto& record : quads) {
      if (record->color != firstColor) {
        hasColor = true;
        break;
      }
    }
  }
  auto quadArray = allocator->makeArray(std::move(quads));
  if (aaType == AAType::Coverage) {
    return allocator->make<AAQuadsVertexProvider>(std::move(quadArray), aaType, hasColor,
                                                  allocator->addReference());
  }
  return allocator->make<NonAAQuadsVertexProvider>(std::move(quadArray), aaType, hasColor,
                                                   allocator->addReference());
}

QuadsVertexProvider::QuadsVertexProvider(PlacementArray<QuadRecord>&& quads, AAType aaType,
                                         bool hasColor, std::shared_ptr<BlockAllocator> reference)
    : VertexProvider(std::move(reference)),
      quads(std::move(quads)),
      _aaType(aaType),
      _hasColor(hasColor) {
}

}  // namespace tgfx
