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

namespace tgfx {
std::shared_ptr<LinearGradient> Gradient::MakeLinear(const Point& startPoint,
                                                     const Point& endPoint) {
  return std::shared_ptr<LinearGradient>(new LinearGradient(startPoint, endPoint));
}

std::shared_ptr<RadialGradient> Gradient::MakeRadial(const Point& center, float radius) {
  return std::shared_ptr<RadialGradient>(new RadialGradient(center, radius));
}

std::shared_ptr<ConicGradient> Gradient::MakeConic(const Point& center, float startAngle,
                                                   float endAngle) {
  return std::shared_ptr<ConicGradient>(new ConicGradient(center, startAngle, endAngle));
}

void Gradient::setColors(std::vector<Color> colors) {
  _colors = std::move(colors);
}

void Gradient::setPositions(std::vector<float> positions) {
  _positions = std::move(positions);
}

std::shared_ptr<Shader> LinearGradient::getShader() const {
  return Shader::MakeLinearGradient(_startPoint, _endPoint, _colors, _positions);
}

std::shared_ptr<Shader> RadialGradient::getShader() const {
  return Shader::MakeRadialGradient(_center, _radius, _colors, _positions);
}

std::shared_ptr<Shader> ConicGradient::getShader() const {
  return Shader::MakeSweepGradient(_center, _startAngle, _endAngle, _colors, _positions);
}
}  // namespace tgfx
