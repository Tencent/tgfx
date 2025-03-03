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

#include "tgfx/layers/Gradient.h"
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

void Gradient::setColors(std::vector<Color> colors) {
  if (_colors.size() == colors.size() &&
      std::equal(_colors.begin(), _colors.end(), colors.begin())) {
    return;
  }
  _colors = std::move(colors);
  invalidate();
}

void Gradient::setPositions(std::vector<float> positions) {
  if (_positions.size() == positions.size() &&
      std::equal(_positions.begin(), _positions.end(), positions.begin())) {
    return;
  }
  _positions = std::move(positions);
  invalidate();
}

void LinearGradient::setEndPoint(const Point& endPoint) {
  if (_endPoint == endPoint) {
    return;
  }
  _endPoint = endPoint;
  invalidate();
}

void LinearGradient::setStartPoint(const Point& startPoint) {
  if (_startPoint == startPoint) {
    return;
  }
  _startPoint = startPoint;
  invalidate();
}

std::shared_ptr<Shader> LinearGradient::onGetShader() const {
  return Shader::MakeLinearGradient(_startPoint, _endPoint, _colors, _positions);
}

void RadialGradient::setCenter(const Point& center) {
  if (_center == center) {
    return;
  }
  _center = center;
  invalidate();
}

void RadialGradient::setRadius(float radius) {
  if (_radius == radius) {
    return;
  }
  _radius = radius;
  invalidate();
}

std::shared_ptr<Shader> RadialGradient::onGetShader() const {
  return Shader::MakeRadialGradient(_center, _radius, _colors, _positions);
}

void ConicGradient::setStartAngle(float startAngle) {
  if (_startAngle == startAngle) {
    return;
  }
  _startAngle = startAngle;
  invalidate();
}

void ConicGradient::setCenter(const Point& center) {
  if (_center == center) {
    return;
  }
  _center = center;
  invalidate();
}

void ConicGradient::setEndAngle(float endAngle) {
  if (_endAngle == endAngle) {
    return;
  }
  _endAngle = endAngle;
  invalidate();
}

std::shared_ptr<Shader> ConicGradient::onGetShader() const {
  return Shader::MakeConicGradient(_center, _startAngle, _endAngle, _colors, _positions);
}
}  // namespace tgfx
