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

#pragma once

#include "DataSource.h"
#include "tgfx/core/Data.h"
#include "tgfx/core/Shape.h"

namespace tgfx {
/**
 * ShapeBezierBuffer holds the CPU-side geometry stream consumed by the bezier rasterization
 * render path. The vertex layout (position + per-vertex implicit-curve coefficients) is owned
 * by the geometry processors used in the stencil and cover passes.
 */
struct ShapeBezierBuffer {
  ShapeBezierBuffer(std::shared_ptr<Data> vertices, size_t vertexCount)
      : vertices(std::move(vertices)), vertexCount(vertexCount) {
  }

  std::shared_ptr<Data> vertices = nullptr;
  size_t vertexCount = 0;
};

/**
 * ShapeBezierRasterizer converts a shape into the geometry stream consumed by the stencil
 * pass of the bezier rasterization render path. Unlike ShapeRasterizer it never falls back to
 * a triangulated mesh or an alpha mask — the output is always a vertex buffer suitable for
 * implicit bezier coverage on the GPU.
 *
 * The algorithm itself is filled in by a later stage; this skeleton only nails down the
 * interface so downstream modules (ProxyProvider, draw op, geometry processors) can be wired
 * up first.
 */
class ShapeBezierRasterizer : public DataSource<ShapeBezierBuffer> {
 public:
  /**
   * Creates a ShapeBezierRasterizer from a shape. The shape's transform must already be baked
   * in by the caller — the rasterizer only inspects the path geometry, not its placement on
   * the render target.
   */
  explicit ShapeBezierRasterizer(std::shared_ptr<Shape> shape);

  /**
   * Returns true if getData() may be called from an arbitrary thread. The bezier rasterization
   * pipeline is purely CPU-side numerics, so it follows the same async-availability rule used
   * by ShapeRasterizer (only disabled on the Web build without freetype).
   */
  bool asyncSupport() const;

  /**
   * Builds the bezier vertex stream for the shape. Returns nullptr when the shape produces no
   * drawable geometry, when the input is unsupported, or while the algorithm has not yet been
   * implemented.
   */
  std::shared_ptr<ShapeBezierBuffer> getData() const override;

 private:
  std::shared_ptr<Shape> shape = nullptr;
};
}  // namespace tgfx
