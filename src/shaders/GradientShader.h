/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2023 THL A29 Limited, a Tencent company. All rights reserved.
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

#include "tgfx/core/Matrix.h"
#include "tgfx/core/Shader.h"

namespace tgfx {

class GradientShaderBase : public Shader {
 public:
  GradientShaderBase(const std::vector<Color>& colors, const std::vector<float>& positions,
                     const Matrix& pointsToUnit);

  bool isOpaque() const override {
    return colorsAreOpaque;
  }

  std::vector<Color> originalColors = {};
  std::vector<float> originalPositions = {};

 protected:
  const Matrix pointsToUnit;
  bool colorsAreOpaque = false;
};

class LinearGradient : public GradientShaderBase {
 public:
  LinearGradient(const Point& startPoint, const Point& endPoint, const std::vector<Color>& colors,
                 const std::vector<float>& positions);

 protected:
  std::unique_ptr<FragmentProcessor> onMakeFragmentProcessor(
      const DrawArgs& args, const Matrix* localMatrix) const override;
};

class RadialGradient : public GradientShaderBase {
 public:
  RadialGradient(const Point& center, float radius, const std::vector<Color>& colors,
                 const std::vector<float>& positions);

 protected:
  std::unique_ptr<FragmentProcessor> onMakeFragmentProcessor(
      const DrawArgs& args, const Matrix* localMatrix) const override;
};

class SweepGradient : public GradientShaderBase {
 public:
  SweepGradient(const Point& center, float t0, float t1, const std::vector<Color>& colors,
                const std::vector<float>& positions);

 protected:
  std::unique_ptr<FragmentProcessor> onMakeFragmentProcessor(
      const DrawArgs& args, const Matrix* localMatrix) const override;

 private:
  float bias;
  float scale;
};
}  // namespace tgfx
