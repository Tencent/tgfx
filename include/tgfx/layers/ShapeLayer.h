/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 Tencent. All rights reserved.
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

#include "tgfx/core/Shape.h"
#include "tgfx/layers/ColorSource.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
/**
 * The alignment of the stroke relative to the boundaries of the shape.
 */
enum class StrokeAlign {
  /**
  * Draw a stroke centered along the shape boundary.
  */
  Center,
  /**
  * Draw a stroke inside the shape boundary.
  */
  Inside,
  /**
  * Draw a stroke outside the shape boundary.
  */
  Outside
};

/**
 * ShapeLayer is a layer that draws a shape. Each shape layer has a path that defines the shape's
 * geometry and a list of fill and stroke styles that determine how the shape is rendered. The fill
 * styles are drawn first, followed by the stroke styles on top of the filled shape. If the fill or
 * stroke lists are empty, the shape will not be filled or stroked.
 */
class ShapeLayer : public Layer {
 public:
  /**
   * Creates a new shape layer instance.
   */
  static std::shared_ptr<ShapeLayer> Make();

  ~ShapeLayer() override;

  LayerType type() const override {
    return LayerType::Shape;
  }

  /**
   * Returns the Path object defining the shape to be rendered.
   */
  Path path() const;

  /**
   * Sets the Path object defining the shape to be rendered.
   */
  void setPath(Path path);

  /**
   * Returns the Shape object defining the shape to be rendered.
   */
  std::shared_ptr<Shape> shape() const {
    return _shape;
  }

  /**
   * Sets the Shape object defining the shape to be rendered.
   */
  void setShape(std::shared_ptr<Shape> shape);

  /**
   * Returns the list of fill styles used to fill the shape's path. Each style can be a solid color,
   * gradient, or image pattern. The fill styles are drawn in the order they are added, followed by
   * the stroke styles on top of the filled shape. If the fill styles list is empty, the shape will
   * not be filled. By default, the fill styles list is empty.
   */
  const std::vector<std::shared_ptr<ColorSource>>& fillStyles() const {
    return _fillStyles;
  }

  /**
   * Replaces the current list of fill styles with the given list.
   */
  void setFillStyles(std::vector<std::shared_ptr<ColorSource>> fills);

  /**
   * Removes all fill styles from the shape layer.
   */
  void removeFillStyles();

  /**
   * Replace the current list of fill styles with the given fill style.
   */
  void setFillStyle(std::shared_ptr<ColorSource> fillStyle);

  /**
   * Adds a fill style to the end of the existing fill styles.
   */
  void addFillStyle(std::shared_ptr<ColorSource> fillStyle);

  /**
   * Returns the list of stroke styles used to stroke the shape's path. Each style can be a solid
   * color, gradient, or image pattern. The stroke styles are drawn in the order they are added,
   * after the fill styles. If the stroke styles list is empty, the shape will not be stroked. By
   * default, the stroke styles list is empty.
   */
  const std::vector<std::shared_ptr<ColorSource>>& strokeStyles() const {
    return _strokeStyles;
  }

  /**
   * Replaces the current list of stroke styles with the given list.
   */
  void setStrokeStyles(std::vector<std::shared_ptr<ColorSource>> strokes);

  /**
   * Removes all stroke styles from the shape layer.
   */
  void removeStrokeStyles();

  /**
   * Replace the current list of stroke styles with the given stroke style.
   */
  void setStrokeStyle(std::shared_ptr<ColorSource> stroke);

  /**
   * Adds a stroke style to the end of the existing stroke styles.
   */
  void addStrokeStyle(std::shared_ptr<ColorSource> strokeStyle);

  /**
   * Returns the line cap style for the shape's path. The default line cap style is Butt.
   */
  LineCap lineCap() const {
    return stroke.cap;
  }

  /**
   * Sets the line cap style for the shape's path.
   */
  void setLineCap(LineCap cap);

  /**
   * Returns the line join style for the shape's path. The default line join style is Miter.
   */
  LineJoin lineJoin() const {
    return stroke.join;
  }

  /**
   * Sets the line join style for the shape's path.
   */
  void setLineJoin(LineJoin join);

  /**
   * Returns miter limit used when stroking the shape's path. If the current line join style is set
   * to LineJoin::Miter, the miter limit determines whether the lines should be joined with a bevel
   * instead of a miter. The length of the miter is divided by the line width. If the result is
   * greater than the miter limit, the path is drawn with a bevel. The default miter limit is 4.0.
   */
  float miterLimit() const {
    return stroke.miterLimit;
  }

  /**
   * Sets the miter limit for the shape's path.
   */
  void setMiterLimit(float limit);

  /**
   * Returns the line width of the shape's path.
   */
  float lineWidth() const {
    return stroke.width;
  }

  /**
   * Sets the line width of the shape's path.
   */
  void setLineWidth(float width);

  /**
   * Returns the dash pattern applied to the shape's path when stroked. The dash pattern is
   * specified as an array of float numbers that specify the lengths of the painted segments and
   * unpainted segments, respectively, of the dash pattern.
   * For example, passing an array with the values [2,3] sets a dash pattern that alternates between
   * a 2-user-space-unit-long painted segment and a 3-user-space-unit-long unpainted segment.
   * Passing the values [10,5,5,5] sets the pattern to a 10-unit painted segment, a 5-unit unpainted
   * segment, a 5-unit painted segment, and a 5-unit unpainted segment.
   * The default dash pattern is an empty array, which indicates a solid line.
   */
  const std::vector<float>& lineDashPattern() const {
    return _lineDashPattern;
  }

  /**
   * Sets the dash pattern applied to the shape's path when stroked.
   */
  void setLineDashPattern(const std::vector<float>& pattern);

  /**
   * Returns the dash phase applied to the shape's path when stroked. Line dash phase specifies how
   * far into the dash pattern the line starts. The default dash phase is 0.
   */
  float lineDashPhase() const {
    return _lineDashPhase;
  }

  /**
   * Sets the dash phase applied to the shape's path when stroked.
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
   * Returns the stroke alignment applied to the shape's path when stroked. The default stroke alignment is Center.
   */
  StrokeAlign strokeAlign() const {
    return static_cast<StrokeAlign>(shapeBitFields.strokeAlign);
  }

  /**
   * Sets the stroke alignment applied to the shape's path when stroked.
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

  void setStrokeOnTop(bool value);

 protected:
  ShapeLayer();

  void onUpdateContent(LayerRecorder* recorder) override;

 private:
  std::shared_ptr<Shape> _shape = nullptr;
  std::vector<std::shared_ptr<ColorSource>> _fillStyles = {};
  std::vector<std::shared_ptr<ColorSource>> _strokeStyles = {};
  Stroke stroke = {};
  std::vector<float> _lineDashPattern = {};
  float _lineDashPhase = 0.0f;
  struct {
    bool strokeOnTop : 1;
    bool lineDashAdaptive : 1;
    uint8_t strokeAlign : 2;
  } shapeBitFields = {};

  std::shared_ptr<Shape> createStrokeShape() const;
};
}  // namespace tgfx
