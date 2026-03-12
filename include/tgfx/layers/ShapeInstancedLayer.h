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

#include "tgfx/core/Color.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shape.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/ShapeStyle.h"
#include "tgfx/layers/StrokeAlign.h"

namespace tgfx {
/**
 * ShapeInstancedLayer is a layer that draws multiple instances of the same shape with different
 * transformations and optional per-instance colors. All instances share the same geometry, enabling
 * efficient batched rendering via GPU instancing.
 */
class ShapeInstancedLayer : public Layer {
 public:
  /**
   * Creates a new shape instanced layer instance.
   */
  static std::shared_ptr<ShapeInstancedLayer> Make();

  LayerType type() const override {
    return LayerType::ShapeInstanced;
  }

  /**
   * Returns the Shape object shared by all instances.
   */
  std::shared_ptr<Shape> shape() const {
    return _shape;
  }

  /**
   * Sets the Shape object shared by all instances.
   */
  void setShape(std::shared_ptr<Shape> shape);

  /**
   * Returns the transformation matrices for all instances.
   */
  const std::vector<Matrix>& matrices() const {
    return _matrices;
  }

  /**
   * Sets the transformation matrices for all instances. Each matrix defines the position, rotation,
   * and scale of one instance.
   */
  void setMatrices(std::vector<Matrix> matrices);

  /**
   * Returns the per-instance colors. An empty vector means no per-instance colors are used.
   */
  const std::vector<Color>& colors() const {
    return _colors;
  }

  /**
   * Sets the per-instance colors. If provided, the vector must have the same number of elements as
   * matrices. Pass an empty vector to use paint color or shader color only.
   */
  void setColors(std::vector<Color> colors);

  /**
   * Returns the list of fill styles used to fill the instances. Each style contains a shader, alpha,
   * and blend mode. Color blending follows the same rules as drawMesh():
   * - If per-instance colors are set and a style has a shader: texture color * instance color.
   * - If per-instance colors are not set and a style has a shader: pure texture color.
   * - If per-instance colors are set and a style has no shader: instance color.
   * - If per-instance colors are not set and a style has no shader: style color.
   * By default, the fill styles list is empty.
   */
  const std::vector<std::shared_ptr<ShapeStyle>>& fillStyles() const {
    return _fillStyles;
  }

  /**
   * Replaces the current list of fill styles with the given list.
   */
  void setFillStyles(std::vector<std::shared_ptr<ShapeStyle>> fills);

  /**
   * Removes all fill styles from the layer.
   */
  void removeFillStyles();

  /**
   * Replaces the current list of fill styles with the given fill style.
   */
  void setFillStyle(std::shared_ptr<ShapeStyle> fillStyle);

  /**
   * Adds a fill style to the end of the existing fill styles.
   */
  void addFillStyle(std::shared_ptr<ShapeStyle> fillStyle);

  /**
   * Returns the list of stroke styles used to stroke the instances. Each style contains a shader,
   * alpha, and blend mode. The stroke styles are drawn in the order they are added, after the fill
   * styles. If the stroke styles list is empty, the instances will not be stroked unless
   * per-instance colors are set and lineWidth is greater than 0. By default, the stroke styles list
   * is empty.
   */
  const std::vector<std::shared_ptr<ShapeStyle>>& strokeStyles() const {
    return _strokeStyles;
  }

  /**
   * Replaces the current list of stroke styles with the given list.
   */
  void setStrokeStyles(std::vector<std::shared_ptr<ShapeStyle>> strokes);

  /**
   * Removes all stroke styles from the layer.
   */
  void removeStrokeStyles();

  /**
   * Replaces the current list of stroke styles with the given stroke style.
   */
  void setStrokeStyle(std::shared_ptr<ShapeStyle> stroke);

  /**
   * Adds a stroke style to the end of the existing stroke styles.
   */
  void addStrokeStyle(std::shared_ptr<ShapeStyle> strokeStyle);

  /**
   * Returns the line cap style for the instances' stroke. The default line cap style is Butt.
   */
  LineCap lineCap() const {
    return stroke.cap;
  }

  /**
   * Sets the line cap style for the instances' stroke.
   */
  void setLineCap(LineCap cap);

  /**
   * Returns the line join style for the instances' stroke. The default line join style is Miter.
   */
  LineJoin lineJoin() const {
    return stroke.join;
  }

  /**
   * Sets the line join style for the instances' stroke.
   */
  void setLineJoin(LineJoin join);

  /**
   * Returns the miter limit used when stroking the instances. If the current line join style is set
   * to LineJoin::Miter, the miter limit determines whether the lines should be joined with a bevel
   * instead of a miter. The length of the miter is divided by the line width. If the result is
   * greater than the miter limit, the path is drawn with a bevel. The default miter limit is 4.0.
   */
  float miterLimit() const {
    return stroke.miterLimit;
  }

  /**
   * Sets the miter limit for the instances' stroke.
   */
  void setMiterLimit(float limit);

  /**
   * Returns the line width of the instances' stroke.
   */
  float lineWidth() const {
    return stroke.width;
  }

  /**
   * Sets the line width of the instances' stroke.
   */
  void setLineWidth(float width);

  /**
   * Returns the dash pattern applied to the instances when stroked. The dash pattern is specified
   * as an array of float numbers that specify the lengths of the painted segments and unpainted
   * segments, respectively, of the dash pattern. The default dash pattern is an empty array, which
   * indicates a solid line.
   */
  const std::vector<float>& lineDashPattern() const {
    return _lineDashPattern;
  }

  /**
   * Sets the dash pattern applied to the instances when stroked.
   */
  void setLineDashPattern(const std::vector<float>& pattern);

  /**
   * Returns the dash phase applied to the instances when stroked. Line dash phase specifies how far
   * into the dash pattern the line starts. The default dash phase is 0.
   */
  float lineDashPhase() const {
    return _lineDashPhase;
  }

  /**
   * Sets the dash phase applied to the instances when stroked.
   */
  void setLineDashPhase(float phase);

  /**
   * Indicates whether to scale the dash intervals so that the dash segments have the same length.
   * The default value is false.
   */
  bool lineDashAdaptive() const {
    return shapeBitFields.lineDashAdaptive;
  }

  /**
   * Sets whether the dash segments are the same length.
   */
  void setLineDashAdaptive(bool adaptive);

  /**
   * Returns the stroke alignment applied to the instances when stroked. The default stroke
   * alignment is Center.
   */
  StrokeAlign strokeAlign() const {
    return static_cast<StrokeAlign>(shapeBitFields.strokeAlign);
  }

  /**
   * Sets the stroke alignment applied to the instances when stroked.
   */
  void setStrokeAlign(StrokeAlign align);

  /**
   * Indicates whether strokes are drawn on top of child layers and layer styles. Normally, strokes
   * are drawn above fills but below child layers. If true, strokes are drawn above all child layers
   * and layer styles. The default value is false.
   */
  bool strokeOnTop() const {
    return shapeBitFields.strokeOnTop;
  }

  /**
   * Sets whether strokes are drawn on top of child layers and layer styles.
   */
  void setStrokeOnTop(bool value);

 protected:
  ShapeInstancedLayer();

  void onUpdateContent(LayerRecorder* recorder) override;

 private:
  std::shared_ptr<Shape> _shape = nullptr;
  std::vector<Matrix> _matrices = {};
  std::vector<Color> _colors = {};
  std::vector<std::shared_ptr<ShapeStyle>> _fillStyles = {};
  std::vector<std::shared_ptr<ShapeStyle>> _strokeStyles = {};
  Stroke stroke = {};
  std::vector<float> _lineDashPattern = {};
  float _lineDashPhase = 0.0f;
  struct {
    bool strokeOnTop : 1;
    bool lineDashAdaptive : 1;
    uint8_t strokeAlign : 2;
  } shapeBitFields = {};
};
}  // namespace tgfx
