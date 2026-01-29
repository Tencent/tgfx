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

#include "tgfx/layers/vectors/Gradient.h"
#include "tgfx/layers/Layer.h"

namespace tgfx {
std::shared_ptr<LinearGradient> Gradient::MakeLinear(const Point& startPoint, const Point& endPoint,
                                                     const std::vector<Color>& colors,
                                                     const std::vector<float>& positions) {
  return std::shared_ptr<LinearGradient>(
      new LinearGradient(startPoint, endPoint, colors, positions));
}

std::shared_ptr<RadialGradient> Gradient::MakeRadial(const Point& center, float radius,
                                                     const std::vector<Color>& colors,
                                                     const std::vector<float>& positions) {
  return std::shared_ptr<RadialGradient>(new RadialGradient(center, radius, colors, positions));
}

std::shared_ptr<ConicGradient> Gradient::MakeConic(const Point& center, float startAngle,
                                                   float endAngle, const std::vector<Color>& colors,
                                                   const std::vector<float>& positions) {
  return std::shared_ptr<ConicGradient>(
      new ConicGradient(center, startAngle, endAngle, colors, positions));
}

std::shared_ptr<DiamondGradient> Gradient::MakeDiamond(const Point& center, float radius,
                                                       const std::vector<Color>& colors,
                                                       const std::vector<float>& positions) {
  return std::shared_ptr<DiamondGradient>(new DiamondGradient(center, radius, colors, positions));
}

void Gradient::setColors(std::vector<Color> colors) {
  if (_colors.size() == colors.size() &&
      std::equal(_colors.begin(), _colors.end(), colors.begin())) {
    return;
  }
  _colors = std::move(colors);
  invalidateContent();
}

void Gradient::setPositions(std::vector<float> positions) {
  if (_positions.size() == positions.size() &&
      std::equal(_positions.begin(), _positions.end(), positions.begin())) {
    return;
  }
  _positions = std::move(positions);
  invalidateContent();
}

void Gradient::setMatrix(const Matrix& matrix) {
  if (_matrix == matrix) {
    return;
  }
  _matrix = matrix;
  invalidateContent();
}

std::shared_ptr<Shader> Gradient::getShader() const {
  auto shader = onCreateShader();
  if (shader == nullptr || _matrix.isIdentity()) {
    return shader;
  }
  return shader->makeWithMatrix(_matrix);
}

void LinearGradient::setEndPoint(const Point& endPoint) {
  if (_endPoint == endPoint) {
    return;
  }
  _endPoint = endPoint;
  invalidateContent();
}

void LinearGradient::setStartPoint(const Point& startPoint) {
  if (_startPoint == startPoint) {
    return;
  }
  _startPoint = startPoint;
  invalidateContent();
}

std::shared_ptr<Shader> LinearGradient::onCreateShader() const {
  return Shader::MakeLinearGradient(_startPoint, _endPoint, _colors, _positions);
}

void RadialGradient::setCenter(const Point& center) {
  if (_center == center) {
    return;
  }
  _center = center;
  invalidateContent();
}

void RadialGradient::setRadius(float radius) {
  if (_radius == radius) {
    return;
  }
  _radius = radius;
  invalidateContent();
}

std::shared_ptr<Shader> RadialGradient::onCreateShader() const {
  return Shader::MakeRadialGradient(_center, _radius, _colors, _positions);
}

void ConicGradient::setStartAngle(float startAngle) {
  if (_startAngle == startAngle) {
    return;
  }
  _startAngle = startAngle;
  invalidateContent();
}

void ConicGradient::setCenter(const Point& center) {
  if (_center == center) {
    return;
  }
  _center = center;
  invalidateContent();
}

void ConicGradient::setEndAngle(float endAngle) {
  if (_endAngle == endAngle) {
    return;
  }
  _endAngle = endAngle;
  invalidateContent();
}

std::shared_ptr<Shader> ConicGradient::onCreateShader() const {
  return Shader::MakeConicGradient(_center, _startAngle, _endAngle, _colors, _positions);
}

void DiamondGradient::setCenter(const Point& center) {
  if (_center == center) {
    return;
  }
  _center = center;
  invalidateContent();
}

void DiamondGradient::setRadius(float radius) {
  if (_radius == radius) {
    return;
  }
  _radius = radius;
  invalidateContent();
}

std::shared_ptr<Shader> DiamondGradient::onCreateShader() const {
  return Shader::MakeDiamondGradient(_center, _radius, _colors, _positions);
}

}  // namespace tgfx
