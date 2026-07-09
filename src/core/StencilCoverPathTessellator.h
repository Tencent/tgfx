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
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

// Algorithm reference for this file and its sibling stencil-and-cover components:
//
//   Charles Loop, Jim Blinn. "Resolution Independent Curve Rendering using Programmable
//   Graphics Hardware." ACM SIGGRAPH 2005.
//   https://www.microsoft.com/en-us/research/wp-content/uploads/2005/01/p1000-loop.pdf
//
// The render path is "stencil-and-cover" (NV_path_rendering vocabulary): a stencil pass marks
// the path's interior in the stencil buffer, then a cover pass shades every marked pixel.
// What Loop-Blinn contributes is *how* the stencil pass decides interior-vs-exterior for
// pixels under bezier curve segments — instead of subdividing the curve into many small
// triangles, every curve fragment carries three barycentric "KLM" coefficients whose values
// are linearly interpolated across the triangle by the GPU. The fragment shader then runs a
// single O(1) implicit-curve test on the interpolated (k, l, m) to decide if the fragment is
// inside the filled region. For quadratic beziers (this implementation) the test simplifies
// to `k*k - l > 0 ⇒ outside`. The third coefficient m is reserved for the cubic extension
// from the same paper and is unused here.

#pragma once

#include "DataSource.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * StencilCoverVertexData holds the CPU-side geometry stream consumed by the stencil-and-cover
 * render path. Each vertex carries the screen-space position together with the three KLM
 * coefficients defined by the Loop-Blinn quadratic implicit-curve test (see file header).
 * The vertex layout is owned by the geometry processors used in the stencil and cover passes.
 */
struct StencilCoverVertexData {
  StencilCoverVertexData(std::shared_ptr<Data> vertices, size_t vertexCount)
      : vertices(std::move(vertices)), vertexCount(vertexCount) {
  }

  std::shared_ptr<Data> vertices = nullptr;
  size_t vertexCount = 0;
};

/**
 * StencilCoverPathTessellator converts a shape into the geometry stream consumed by the stencil
 * pass of the stencil-and-cover render path. It is the CPU half of a Loop-Blinn quadratic
 * fill renderer: rather than triangulating curves on the CPU, it emits per-curve-segment
 * triangles with the canonical Loop-Blinn KLM coordinates so the fragment shader can run the
 * implicit-curve test `k*k - l > 0 ⇒ discard` to obtain pixel-accurate coverage on the GPU.
 *
 * Unlike ShapeRasterizer this tessellator never falls back to a triangulated mesh or an alpha
 * mask — the output is always a Loop-Blinn vertex buffer suitable for implicit curve coverage
 * on the GPU.
 *
 * The algorithm decomposes the shape's path into line and quadratic bezier segments (cubics
 * are lowered to quads via PathUtils::ConvertCubicToQuads, conics via NoConicsPathIterator),
 * then emits a vertex stream carrying per-vertex position and Loop-Blinn KLM coefficients.
 * The fan origin is the bounding-box centre. Degenerate quads (collinear control points) are
 * demoted to straight lines to use the cheaper vertex layout.
 */
class StencilCoverPathTessellator : public DataSource<StencilCoverVertexData> {
 public:
  /**
   * Creates a StencilCoverPathTessellator from a shape. The shape's transform must already be baked
   * in by the caller — the rasterizer only inspects the path geometry, not its placement on
   * the render target.
   */
  explicit StencilCoverPathTessellator(std::shared_ptr<Shape> shape);

  /**
   * Returns true if getData() may be called from an arbitrary thread. The stencil-and-cover
   * pipeline is purely CPU-side numerics, so it follows the same async-availability rule used
   * by ShapeRasterizer (only disabled on the Web build without freetype).
   */
  bool asyncSupport() const;

  /**
   * Builds the bezier vertex stream for the shape. Returns nullptr when the shape produces no
   * drawable geometry (empty path, zero-area bounds) or when the input shape is null.
   */
  std::shared_ptr<StencilCoverVertexData> getData() const override;

 private:
  std::shared_ptr<Shape> shape = nullptr;
};
}  // namespace tgfx
