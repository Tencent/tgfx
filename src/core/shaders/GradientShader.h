/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 Tencent. All rights reserved.
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

#include "tgfx/core/GradientType.h"
#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

/**
 *  Linear:
 *      points[0] and points[1] represent the start and end points of the gradient.
 *  Radial:
 *      points[0] and radiuses[0] represent the center and radius of the gradient.
 *  Conic:
 *      points[0] represents the center, and radiuses[0] and radiuses[1] represent the start angle
 *      and end angle of the gradient.
 */
struct GradientInfo {
  std::vector<Color> colors;     // The colors in the gradient
  std::vector<float> positions;  // The positions of the colors in the gradient
  std::array<Point, 2> points;
  std::array<float, 2> radiuses;
};

class GradientShader : public Shader {
 public:
  GradientShader(const std::vector<Color>& colors, const std::vector<float>& positions,
                 const Matrix& pointsToUnit);

  bool isOpaque() const override {
    return colorsAreOpaque;
  }

  virtual GradientType asGradient(GradientInfo*) const = 0;

  std::vector<Color> originalColors = {};
  std::vector<float> originalPositions = {};
  const Matrix pointsToUnit;
  bool colorsAreOpaque = false;

 protected:
  Type type() const override {
    return Type::Gradient;
  }

  bool isEqual(const Shader* shader) const override {
    // For performance reasons, we don't compare the GradientInfo struct. So multiple
    // GradientShaders with the same GradientInfo struct will not be considered equal.
    return shader == this;
  }
};

class LinearGradientShader : public GradientShader {
 public:
  LinearGradientShader(const Point& startPoint, const Point& endPoint,
                       const std::vector<Color>& colors, const std::vector<float>& positions);

  GradientType asGradient(GradientInfo*) const override;

 protected:
  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> colorSpace) const override;
};

class RadialGradientShader : public GradientShader {
 public:
  RadialGradientShader(const Point& center, float radius, const std::vector<Color>& colors,
                       const std::vector<float>& positions);

  GradientType asGradient(GradientInfo*) const override;

 protected:
  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> colorSpace) const override;
};

class ConicGradientShader : public GradientShader {
 public:
  ConicGradientShader(const Point& center, float t0, float t1, const std::vector<Color>& colors,
                      const std::vector<float>& positions);

  GradientType asGradient(GradientInfo*) const override;

 protected:
  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> colorSpace) const override;

 private:
  float bias;
  float scale;
};

class DiamondGradientShader : public GradientShader {
 public:
  DiamondGradientShader(const Point& center, float halfDiagonal, const std::vector<Color>& colors,
                        const std::vector<float>& positions);

  GradientType asGradient(GradientInfo* info) const override;

 protected:
  PlacementPtr<FragmentProcessor> asFragmentProcessor(
      const FPArgs& args, const Matrix* uvMatrix,
      std::shared_ptr<ColorSpace> colorSpace) const override;
};
}  // namespace tgfx
