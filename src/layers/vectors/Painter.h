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
#include "tgfx/layers/StrokeAlign.h"
#include "tgfx/layers/VectorDraw.h"
#include "tgfx/layers/vectors/ColorSource.h"

namespace tgfx {

class VectorContext;

/**
 * Describes the fill/stroke style of a drawing operation.
 */
struct PainterStyle {
  /**
   * Whether the geometry is filled or stroked.
   */
  PaintStyle style = PaintStyle::Fill;
  /**
   * The width of the stroke. Only meaningful when style is Stroke.
   */
  float strokeWidth = 0.0f;
  /**
   * The alignment of the stroke relative to the shape boundary.
   */
  StrokeAlign strokeAlign = StrokeAlign::Center;
};

/** Prepares captured geometry with a shared traversal, preserving inner-space fit and outer CTM.
 * Subclasses supply shape and glyph paints alongside their owned snapshots.
 */
class Painter {
 public:
  virtual ~Painter() = default;

  /** Applies groupAlpha and records kind's author sourceTag and current-frame runtimeEndpoint. */
  void applyAlpha(float groupAlpha, const void* sourceTag, const void* runtimeEndpoint,
                  VectorAlphaFactor::Kind kind);

  /**
   * Captures geometry pointers and snapshots their current matrices as the inner-space transforms.
   */
  void captureGeometries(VectorContext* context);

  /** Walks captured shapes and glyph runs and delivers prepared operations to sink. */
  void draw(VectorDrawSink* sink);

  /**
   * Creates a copy of this painter.
   */
  virtual std::unique_ptr<Painter> clone() const = 0;

  /**
   * Returns the fill/stroke style of this painter.
   */
  PainterStyle getStyle() const;

  std::shared_ptr<Shader> shader = nullptr;
  std::shared_ptr<ColorSource> colorSource = nullptr;
  VectorColorSnapshot colorSnapshot = {};
  /**
   * Opaque identity of the style element that created this painter. tgfx never dereferences it.
   */
  const void* sourceTag = nullptr;
  const void* runtimeEndpoint = nullptr;
  BlendMode blendMode = BlendMode::SrcOver;
  float alpha = 1.0f;
  std::vector<VectorAlphaFactor> alphaFactors = {};
  class VectorPrepareBudget* budget = nullptr;
  LayerPlacement placement = LayerPlacement::Background;
  std::vector<Geometry*> geometries = {};
  std::vector<Matrix> innerMatrices = {};

 protected:
  /** Prepares non-null innerShape for geometry index; fills paint and snapshot, or null skips emission.
   * Shape and fit bounds are in the captured painter's inner space, before the outer CTM.
   */
  virtual std::shared_ptr<Shape> prepareShape(std::shared_ptr<Shape> innerShape, size_t index,
                                              LayerPaint* paint, VectorPaintSnapshot* snapshot) = 0;

  /** One run-local drawable and its paints with one matching snapshot per paint. */
  struct GlyphRunEmit {
    std::shared_ptr<TextBlob> textBlob = nullptr;
    std::shared_ptr<Shape> shape = nullptr;
    std::vector<LayerPaint> paints = {};
    std::vector<VectorPaintSnapshot> paintSnapshots = {};
  };
  /** Prepares run for geometry index in run-local space; empty paints skip emission. */
  virtual GlyphRunEmit prepareGlyphRun(const StyledGlyphRun& run, size_t index) = 0;

  /** Applies snapshot's exact fit matrix to the painter shader, or returns it without fitting. */
  std::shared_ptr<Shader> wrapShaderWithFit(const VectorPaintSnapshot& snapshot) const;

  /** Checks every copied painter field against the shared budget before clone allocation. */
  bool canClone() const;

  /** Builds a base LayerPaint with the painter's blend mode, alpha, and placement. */
  LayerPaint makeBasePaint() const;

  /** Captures a base paint's exact fit bounds and supplied glyph alpha multiplier. */
  VectorPaintSnapshot makeBaseSnapshot(const Rect& bounds, float glyphAlpha = 1) const;

  /** Captures an overlay color, its exact measurement bounds, and glyph alpha multiplier. */
  VectorPaintSnapshot makeOverlaySnapshot(const Rect& bounds, const Color& color,
                                          float glyphAlpha) const;

  /**
   * Returns the fill/stroke style of this painter.
   */
  virtual PainterStyle onGetStyle() const = 0;

 private:
  VectorDrawOp makeDrawOp(size_t index) const;
  Matrix computeOuterMatrix(size_t index) const;
};

}  // namespace tgfx
