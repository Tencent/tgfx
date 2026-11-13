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

#include <memory>
#include <vector>
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/core/TextBlob.h"
#include "tgfx/layers/LayerPaint.h"
#include "tgfx/layers/vectors/ColorSource.h"

namespace tgfx {

/**
 * One prepared draw operation produced by a vector layer traversal. Shape ops carry the shape
 * already mapped into the painter's inner space and the matrix that maps that space to the layer
 * space. Glyph-run ops carry the final run matrix and one paint per emitted drawable.
 */
struct VectorDrawOp {
  /**
   * The prepared shape. Mutually exclusive with textBlob for glyph-run ops.
   */
  std::shared_ptr<Shape> shape = nullptr;

  /**
   * The prepared text blob for a glyph-run op that stays in text form.
   */
  std::shared_ptr<TextBlob> textBlob = nullptr;

  /**
   * Paints to apply to the shape or blob, in draw order.
   */
  std::vector<LayerPaint> paints = {};

  /**
   * The transform from the prepared geometry space to the layer space.
   */
  Matrix matrix = Matrix::I();

  /**
   * Opaque identity of the captured geometry that produced this op. It stays stable for the
   * lifetime of the draw call and lets a caller correlate the op with the element it built.
   */
  const void* geometry = nullptr;

  /**
   * Opaque identity of the vector element that produced the captured geometry, or null when the
   * element did not attach one. Exporters use it to recover the authored node behind this draw.
   */
  const void* sourceTag = nullptr;

  /**
   * The authored color source of the painter. Exporters use it to rebuild a seriable paint
   * (gradient or image pattern) because the prepared shader itself is opaque.
   */
  std::shared_ptr<ColorSource> colorSource = nullptr;
};

/**
 * Receives the prepared draw operations of a VectorLayer. Implementations must not retain
 * references to the layer's transient geometry objects beyond the call.
 */
class VectorDrawSink {
 public:
  virtual ~VectorDrawSink() = default;

  /**
   * A shape op with a single paint.
   */
  virtual void drawShape(const VectorDrawOp& op) = 0;

  /**
   * A glyph-run op with one or more paints.
   */
  virtual void drawGlyphRun(const VectorDrawOp& op) = 0;

  /**
   * A geometry that no painter captured. It contributes no paint of its own but still describes
   * the layer's own geometry for consumers such as GlassStyle and contour extraction.
   */
  virtual void drawOwnGeometry(const VectorDrawOp& op) = 0;
};

}  // namespace tgfx
