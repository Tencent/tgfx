/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2024 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "SolidColor.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/Stroke.h"
#include "tgfx/layers/Layer.h"
#include "tgfx/layers/PathProvider.h"
#include "tgfx/layers/ShapeStyle.h"

namespace tgfx {
/**
 * ShapeLayer represents a layer that draws a shape.
 */
class ShapeLayer : public Layer {
 public:
  /**
   * Creates a new shape layer instance.
   */
  static std::shared_ptr<ShapeLayer> Make();

  LayerType type() const override {
    return LayerType::Shape;
  }

  /**
   * Returns the path defining the shape to be rendered.
   */
  Path path() const {
    return _path;
  }

  /**
   * Sets the path defining the shape to be rendered. If the path provider is set, the path will be
   * set to an empty Path.
   */
  void setPath(Path path);

  /**
   * Returns the path provider that generates the shape's path.
   */
  std::shared_ptr<PathProvider> pathProvider() const {
    return _pathProvider;
  }

  /**
   * Sets the path provider that generates the shape's path. If the path is set directly using
   * setPath(), the path provider will be set to nullptr.
   */
  void setPathProvider(std::shared_ptr<PathProvider> provider);

  /**
   * Returns the style used to fill the shape's path, which can be a solid color, gradient, or image
   * pattern. If the fill style is set to nullptr, the shape will not be filled. The default fill
   * style is a SolidColor with an opaque black color.
   */
  std::shared_ptr<ShapeStyle> fillStyle() const {
    return _fillStyle;
  }

  /**
   * Sets the style used to fill the shape's path.
   */
  void setFillStyle(std::shared_ptr<ShapeStyle> style);

  /**
   * Returns the style used to stroke the shape’s path, which can be a solid color, gradient, or
   * image pattern. If the stroke style is set to nullptr, the shape will not be stroked. The
   * default stroke style is nullptr.
   */
  std::shared_ptr<ShapeStyle> strokeStyle() const {
    return _strokeStyle;
  }

  /**
   * Sets the style used to fill the shape's path.
   */
  void setStrokeStyle(std::shared_ptr<ShapeStyle> style);

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
   * Returns the line join style for the shape’s path. The default line join style is Miter.
   */
  LineJoin lineJoin() const {
    return stroke.join;
  }

  /**
   * Sets the line join style for the shape’s path.
   */
  void setLineJoin(LineJoin join);

  /**
   * Returns miter limit used when stroking the shape’s path. If the current line join style is set
   * to LineJoin::Miter, the miter limit determines whether the lines should be joined with a bevel
   * instead of a miter. The length of the miter is divided by the line width. If the result is
   * greater than the miter limit, the path is drawn with a bevel. The default miter limit is 4.0.
   */
  float miterLimit() const {
    return stroke.miterLimit;
  }

  /**
   * Sets the miter limit for the shape’s path.
   */
  void setMiterLimit(float limit);

  /**
   * Returns the line width of the shape’s path.
   */
  float lineWidth() const {
    return stroke.width;
  }

  /**
   * Sets the line width of the shape’s path.
   */
  void setLineWidth(float width);

  /**
   * Returns the dash pattern applied to the shape’s path when stroked. The dash pattern is
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
   * Sets the dash pattern applied to the shape’s path when stroked.
   */
  void setLineDashPattern(const std::vector<float>& pattern);

  /**
   * Returns the dash phase applied to the shape’s path when stroked. Line dash phase specifies how
   * far into the dash pattern the line starts. The default dash phase is 0.
   */
  float lineDashPhase() const {
    return _lineDashPhase;
  }

  /**
   * Sets the dash phase applied to the shape’s path when stroked.
   */
  void setLineDashPhase(float phase);

  /**
   * Returns the relative location at which to begin stroking the path. The value of this property
   * must be in the range 0.0 to 1.0. The default value of this property is 0.0. Combined with the
   * strokeEnd property, this property defines the subregion of the path to stroke. The value in
   * this property indicates the relative point along the path at which to begin stroking while the
   * strokeEnd property defines the end point. A value of 0.0 represents the beginning of the path
   * while a value of 1.0 represents the end of the path. Values in between are interpreted linearly
   * along the path length.
   */
  float strokeStart() const {
    return _strokeStart;
  }

  /**
   * Sets the relative location at which to begin stroking the path.
   */
  void setStrokeStart(float start);

  /**
   * Returns the relative location at which to stop stroking the path. The value of this property
   * must be in the range 0.0 to 1.0. The default value of this property is 1.0. Combined with the
   * strokeStart property, this property defines the subregion of the path to stroke. The value in
   * this property indicates the relative point along the path at which to stop stroking while the
   * strokeStart property defines the starting point. A value of 0.0 represents the beginning of the
   * path while a value of 1.0 represents the end of the path. Values in between are interpreted
   * linearly along the path length.
   */
  float strokeEnd() const {
    return _strokeEnd;
  }

  /**
   * Sets the relative location at which to stop stroking the path.
   */
  void setStrokeEnd(float end);

 protected:
  ShapeLayer() = default;

  std::unique_ptr<LayerContent> onUpdateContent() override;
··
  bool hitTestByPixel(float x, float y) override;

 private:
  Path _path = {};
  std::shared_ptr<PathProvider> _pathProvider = nullptr;
  std::shared_ptr<ShapeStyle> _fillStyle = SolidColor::Make();
  std::shared_ptr<ShapeStyle> _strokeStyle = nullptr;
  Stroke stroke = {};
  std::vector<float> _lineDashPattern = {};
  float _lineDashPhase = 0.0f;
  float _strokeStart = 0.0f;
  float _strokeEnd = 1.0f;
};
}  // namespace tgfx
