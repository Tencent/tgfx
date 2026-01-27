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

#include <vector>
#include "tgfx/core/Color.h"
#include "tgfx/core/GradientType.h"
#include "tgfx/core/Point.h"
#include "tgfx/layers/vectors/ColorSource.h"

namespace tgfx {
class LinearGradient;
class RadialGradient;
class ConicGradient;
class DiamondGradient;

/**
 * The base class for all gradient types that can be drawn on a shape layer.
 */
class Gradient : public ColorSource {
 public:
  /**
   * Creates a color source that generates a linear gradient between the two specified points. The
   * color gradient is aligned with the line connecting the two points.
   * @param startPoint The start point for the gradient.
   * @param endPoint The end point for the gradient.
   * @param colors The array of colors, to be distributed between the two points.
   * @param positions Maybe empty. The relative position of each corresponding color in the color
   * array. If this is empty, the colors are distributed evenly between the start and end point.
   * If this is not empty, the values must begin with 0, end with 1.0, and intermediate values must
   * be strictly increasing.
   */
  static std::shared_ptr<LinearGradient> MakeLinear(const Point& startPoint, const Point& endPoint,
                                                    const std::vector<Color>& colors,
                                                    const std::vector<float>& positions = {});

  /**
   * Returns a color source that generates a radial gradient given the center and radius. The color
   * gradient is drawn from the center point to the edge of the radius.
   * @param center The center of the circle for this gradient
   * @param radius Must be positive. The radius of the circle for this gradient.
   * @param colors The array of colors, to be distributed between the center and edge of the circle.
   * @param positions Maybe empty. The relative position of each corresponding color in the color
   * array. If this is empty, the colors are distributed evenly between the start and end point.
   * If this is not empty, the values must begin with 0, end with 1.0, and intermediate values must
   * be strictly increasing.
   */
  static std::shared_ptr<RadialGradient> MakeRadial(const Point& center, float radius,
                                                    const std::vector<Color>& colors,
                                                    const std::vector<float>& positions = {});

  /**
   * Returns a color source that generates a conic gradient given a center point and an angular
   * range. The color gradient is drawn from the start angle to the end angle, wrapping around the
   * center point.
   * @param center The center of the circle for this gradient
   * @param startAngle Start of the angular range, corresponding to pos == 0.
   * @param endAngle End of the angular range, corresponding to pos == 1.
   * @param colors The array of colors, to be distributed around the center, within the gradient
   * angle range.
   * @param positions Maybe empty. The relative position of each corresponding color in the color
   * array. If this is empty, the colors are distributed evenly between the start and end point.
   * If this is not empty, the values must begin with 0, end with 1.0, and intermediate values must
   * be strictly increasing.
   */
  static std::shared_ptr<ConicGradient> MakeConic(const Point& center, float startAngle,
                                                  float endAngle, const std::vector<Color>& colors,
                                                  const std::vector<float>& positions = {});

  /**
   * Returns a color source that generates a diamond gradient given a center point and radius. The
   * color gradient is drawn from the center point to the vertices of the diamond.
   * @param center The center of the diamond
   * @param radius Must be positive. The distance from the center to a vertex of the diamond.
   * @param colors The array of colors, to be distributed around the center, within the gradient
   * angle range.
   * @param positions Maybe empty. The relative position of each corresponding color in the color
   * array. If this is empty, the colors are distributed evenly between the start and end point.
   */
  static std::shared_ptr<DiamondGradient> MakeDiamond(const Point& center, float radius,
                                                      const std::vector<Color>& colors,
                                                      const std::vector<float>& positions = {});

  /**
   * Returns the gradient type.
   */
  virtual GradientType type() const = 0;

  /**
   * Returns the array of colors to be distributed between the start and end points of the gradient.
   */
  const std::vector<Color>& colors() const {
    return _colors;
  }

  /**
   * Sets the array of colors to be distributed between the start and end points of the gradient.
   */
  void setColors(std::vector<Color> colors);

  /**
   * Returns the relative position of each corresponding color in the color array. If this is empty,
   * the colors are distributed evenly between the start and end point. If this is not empty, the
   * values must begin with 0, end with 1.0, and intermediate values must be strictly increasing.
   */
  const std::vector<float>& positions() const {
    return _positions;
  }

  /**
   * Sets the relative position of each corresponding color in the color array.
   */
  void setPositions(std::vector<float> positions);

  /**
   * Returns the transformation matrix applied to the gradient.
   */
  const Matrix& matrix() const {
    return _matrix;
  }

  /**
   * Sets the transformation matrix applied to the gradient.
   */
  void setMatrix(const Matrix& matrix);

  std::shared_ptr<Shader> getShader() const override;

 protected:
  Type getType() const override {
    return Type::Gradient;
  }

  virtual std::shared_ptr<Shader> onCreateShader() const = 0;

  std::vector<Color> _colors = {};
  std::vector<float> _positions = {};

  Gradient(const std::vector<Color>& colors, const std::vector<float>& positions)
      : _colors(colors), _positions(positions) {
  }

 private:
  Matrix _matrix = {};
};

/**
 * Represents a linear gradient that can be drawn on a shape layer.
 */
class LinearGradient : public Gradient {
 public:
  GradientType type() const override {
    return GradientType::Linear;
  }

  /**
   * Returns the start point of the gradient when drawn in the layer's coordinate space. The start
   * point corresponds to the first stop of the gradient.
   */
  const Point& startPoint() const {
    return _startPoint;
  }

  /**
   * Sets the start point of the gradient when drawn in the layer's coordinate space.
   */
  void setStartPoint(const Point& startPoint);

  /**
   * Returns the end point of the gradient when drawn in the layer's coordinate space. The end point
   * corresponds to the last stop of the gradient.
   */
  const Point& endPoint() const {
    return _endPoint;
  }

  /**
   * Sets the end point of the gradient when drawn in the layer's coordinate space.
   */
  void setEndPoint(const Point& endPoint);

 protected:
  std::shared_ptr<Shader> onCreateShader() const override;

 private:
  Point _startPoint = {};
  Point _endPoint = {};

  LinearGradient(const Point& startPoint, const Point& endPoint, const std::vector<Color>& colors,
                 const std::vector<float>& positions)
      : Gradient(colors, positions), _startPoint(startPoint), _endPoint(endPoint) {
  }

  friend class Gradient;
};

/**
 * Represents a radial gradient that can be drawn on a shape layer.
 */
class RadialGradient : public Gradient {
 public:
  GradientType type() const override {
    return GradientType::Radial;
  }

  /**
   * Returns the center of the circle for this gradient. The center point corresponds to the first
   * stop of the gradient.
   */
  const Point& center() const {
    return _center;
  }

  /**
   * Sets the center of the circle for this gradient.
   */
  void setCenter(const Point& center);

  /**
   * Returns the radius of the circle for this gradient. The radius corresponds to the last stop of
   * the gradient.
   */
  float radius() const {
    return _radius;
  }

  /**
   * Sets the radius of the circle for this gradient. The radius must be positive.
   */
  void setRadius(float radius);

 protected:
  std::shared_ptr<Shader> onCreateShader() const override;

 private:
  Point _center = {};
  float _radius = 0;

  RadialGradient(const Point& center, float radius, const std::vector<Color>& colors,
                 const std::vector<float>& positions)
      : Gradient(colors, positions), _center(center), _radius(radius) {
  }

  friend class Gradient;
};

/**
 * Represents a conic gradient that can be drawn on a shape layer.
 */
class ConicGradient : public Gradient {
 public:
  GradientType type() const override {
    return GradientType::Conic;
  }

  /**
   * Returns the center of the circle for this gradient.
   */
  const Point& center() const {
    return _center;
  }

  /**
   * Sets the center of the circle for this gradient.
   */
  void setCenter(const Point& center);

  /**
   * Returns the start angle for this gradient. The start angle corresponds to the first stop of the
   * gradient.
   */
  float startAngle() const {
    return _startAngle;
  }

  /**
   * Sets the start angle for this gradient.
   */
  void setStartAngle(float startAngle);

  /**
   * Returns the end angle for this gradient. The end angle corresponds to the last stop of the
   * gradient.
   */
  float endAngle() const {
    return _endAngle;
  }

  /**
   * Sets the end angle for this gradient.
   */
  void setEndAngle(float endAngle);

 protected:
  std::shared_ptr<Shader> onCreateShader() const override;

 private:
  Point _center = {};
  float _startAngle = 0;
  float _endAngle = 0;

  ConicGradient(const Point& center, float startAngle, float endAngle,
                const std::vector<Color>& colors, const std::vector<float>& positions)
      : Gradient(colors, positions), _center(center), _startAngle(startAngle), _endAngle(endAngle) {
  }

  friend class Gradient;
};

/**
 * Represents a diamond gradient that can be drawn on a shape layer.
 */
class DiamondGradient : public Gradient {
 public:
  GradientType type() const override {
    return GradientType::Diamond;
  }

  /**
   * Returns the center of the diamond for this gradient. The center point corresponds to the first
   * stop of the gradient.
   */
  const Point& center() const {
    return _center;
  }

  /**
   * Sets the center of the diamond for this gradient.
   */
  void setCenter(const Point& center);

  /**
   * Returns the radius of the diamond for this gradient. The distance from the center to a vertex
   * of the diamond.
   */
  float radius() const {
    return _radius;
  }

  /**
   * Sets the radius of the diamond for this gradient. The value must be positive.
   */
  void setRadius(float radius);

 protected:
  std::shared_ptr<Shader> onCreateShader() const override;

 private:
  Point _center = {};
  float _radius = 0;

  DiamondGradient(const Point& center, float radius, const std::vector<Color>& colors,
                  const std::vector<float>& positions)
      : Gradient(colors, positions), _center(center), _radius(radius) {
  }

  friend class Gradient;
};

}  // namespace tgfx
