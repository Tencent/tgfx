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

#include <vector>
#include "Geometry.h"
#include "tgfx/core/BlendMode.h"
#include "tgfx/core/Shader.h"
#include "tgfx/layers/LayerPaint.h"
#include "tgfx/layers/vectors/ColorSource.h"

namespace tgfx {

class LayerRecorder;
class VectorContext;

/**
 * Painter is the base class for objects that perform draw operations on a list of Geometries.
 * It owns the shared traversal flow: walking each captured geometry, splitting the matrix into the
 * inner part (the transform from the shape's defining space into the painter's enclosing group
 * space, captured at apply() time) and the outer part (any further transforms accumulated from
 * outer groups during merging). The outer part is forwarded to the recorder as a CTM so it
 * uniformly affects both the shape and the shader. Subclasses customize how each Geometry is
 * turned into final shapes/text blobs by overriding the prepare hooks.
 */
class Painter {
 public:
  virtual ~Painter() = default;

  /**
   * Applies an alpha multiplier to this painter.
   */
  void applyAlpha(float groupAlpha) {
    alpha *= groupAlpha;
  }

  /**
   * Captures geometry pointers and snapshots their current matrices as the inner-space transforms.
   */
  void captureGeometries(VectorContext* context);

  /**
   * Performs the shared traversal: for each captured Geometry, walk shape and glyph runs and emit
   * them through the recorder under the proper CTM.
   */
  void draw(LayerRecorder* recorder);

  /**
   * Creates a copy of this painter.
   */
  virtual std::unique_ptr<Painter> clone() const = 0;

  std::shared_ptr<Shader> shader = nullptr;
  std::shared_ptr<ColorSource> colorSource = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  float alpha = 1.0f;
  LayerPlacement placement = LayerPlacement::Background;
  std::vector<Geometry*> geometries = {};
  std::vector<Matrix> innerMatrices = {};

 protected:
  /**
   * Subclass hook for shape geometries. The supplied innerShape has already been mapped into the
   * enclosing group space (i.e. Shape::ApplyMatrix(originalShape, innerMatrices[index])).
   * Subclasses may further modify the shape and must populate paint.shader; style and stroke may
   * be set when applicable. Return nullptr to skip emission.
   */
  virtual std::shared_ptr<Shape> prepareShape(std::shared_ptr<Shape> innerShape, size_t index,
                                              LayerPaint* paint) = 0;

  /**
   * Subclass hook for glyph runs. Subclasses produce one or more emit items per run (e.g. base +
   * colored overlay). Each item carries either a text blob or a stroke-expanded shape; the base
   * installs runLocalMatrix * outerMatrix as the recorder CTM around all items.
   */
  struct GlyphEmit {
    std::shared_ptr<TextBlob> textBlob = nullptr;
    std::shared_ptr<Shape> shape = nullptr;
    LayerPaint paint = {};
  };
  virtual std::vector<GlyphEmit> prepareGlyphRun(const StyledGlyphRun& run, size_t index) = 0;

  /**
   * Wraps the painter's shader with a fit matrix derived from the supplied inner-space bounds.
   * Returns the original shader unchanged when the color source does not opt into per-geometry
   * fit.
   */
  std::shared_ptr<Shader> wrapShaderWithFit(const Rect& innerBounds) const;

  /**
   * Builds a base LayerPaint pre-populated with the painter's blendMode/alpha/placement.
   */
  LayerPaint makeBasePaint() const;

 private:
  /**
   * Computes outerMatrix = inv(innerMatrix) * geometry->matrix for the geometry at the given
   * index. Returns the identity matrix when the inner matrix is not invertible.
   */
  Matrix computeOuterMatrix(size_t index) const;
};

}  // namespace tgfx
