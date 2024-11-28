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

#include "GradientShader.h"
#include "core/utils/MathExtra.h"
#include "gpu/ResourceProvider.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/DualIntervalGradientColorizer.h"
#include "gpu/processors/LinearGradientLayout.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureGradientColorizer.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"

namespace tgfx {
// Intervals smaller than this (that aren't hard stops) on low-precision-only devices force us to
// use the textured gradient
static constexpr float LowPrecisionIntervalLimit = 0.01f;
static constexpr float DegenerateThreshold = 1.0f / (1 << 15);

// Analyze the shader's color stops and positions and chooses an appropriate colorizer to represent
// the gradient.
static std::unique_ptr<FragmentProcessor> MakeColorizer(const Context* context, const Color* colors,
                                                        const float* positions, int count) {
  // If there are hard stops at the beginning or end, the first and/or last color should be
  // ignored by the colorizer since it should only be used in a clamped border color. By detecting
  // and removing these stops at the beginning, it makes optimizing the remaining color stops
  // simpler.
  bool bottomHardStop = FloatNearlyEqual(positions[0], positions[1]);
  bool topHardStop = FloatNearlyEqual(positions[count - 2], positions[count - 1]);
  int offset = 0;
  if (bottomHardStop) {
    offset += 1;
    count--;
  }
  if (topHardStop) {
    count--;
  }

  // Two remaining colors means a single interval from 0 to 1
  // (but it may have originally been a 3 or 4 color gradient with 1-2 hard stops at the ends)
  if (count == 2) {
    return SingleIntervalGradientColorizer::Make(colors[offset], colors[offset + 1]);
  }

  bool tryAnalyticColorizer = count <= UnrolledBinaryGradientColorizer::kMaxColorCount;

  // The remaining analytic colorizes use scale*t+bias, and the scale/bias values can become
  // quite large when thresholds are close (but still outside the hard stop limit). If float isn't
  // 32-bit, output can be incorrect if the thresholds are too close together. However, the
  // analytic shaders are higher quality, so they can be used with lower precision hardware when
  // the thresholds are not ill-conditioned.
  if (!context->caps()->floatIs32Bits && tryAnalyticColorizer) {
    // Could run into problems, check if thresholds are close together (with a limit of .01, so
    // that scales will be less than 100, which leaves 4 decimals of precision on 16-bit).
    for (int i = offset; i < count - 1; i++) {
      auto delta = std::abs(positions[i] - positions[i + 1]);
      if (delta <= LowPrecisionIntervalLimit && delta > FLOAT_NEARLY_ZERO) {
        tryAnalyticColorizer = false;
        break;
      }
    }
  }
  if (tryAnalyticColorizer) {
    if (count == 3) {
      // Must be a dual interval gradient, where the middle point is at offset+1 and the two
      // intervals share the middle color stop.
      return DualIntervalGradientColorizer::Make(colors[offset], colors[offset + 1],
                                                 colors[offset + 1], colors[offset + 2],
                                                 positions[offset + 1]);
    } else if (count == 4 && FloatNearlyEqual(positions[offset + 1], positions[offset + 2])) {
      // Two separate intervals that join at the same threshold position
      return DualIntervalGradientColorizer::Make(colors[offset], colors[offset + 1],
                                                 colors[offset + 2], colors[offset + 3],
                                                 positions[offset + 1]);
    }

    // The single and dual intervals are a specialized case of the unrolled binary search
    // colorizer which can analytically render gradients of up to 8 intervals (up to 9 or 16
    // colors depending on how many hard stops are inserted).
    auto unrolled =
        UnrolledBinaryGradientColorizer::Make(colors + offset, positions + offset, count);
    if (unrolled) {
      return unrolled;
    }
  }

  // Otherwise, fall back to a raster gradient sample by a texture, which can handle
  // arbitrary gradients (the only downside being sampling resolution).
  return TextureGradientColorizer::Make(
      context->resourceProvider()->getGradient(colors + offset, positions + offset, count));
}

GradientShader::GradientShader(const std::vector<Color>& colors,
                               const std::vector<float>& positions, const Matrix& pointsToUnit)
    : pointsToUnit(pointsToUnit) {
  colorsAreOpaque = true;
  for (auto& color : colors) {
    if (!color.isOpaque()) {
      colorsAreOpaque = false;
      break;
    }
  }
  auto dummyFirst = false;
  auto dummyLast = false;
  if (!positions.empty()) {
    dummyFirst = positions[0] != 0;
    dummyLast = positions[positions.size() - 1] != 1.0f;
  }
  // Now copy over the colors, adding the dummies as needed
  if (dummyFirst) {
    originalColors.push_back(colors[0]);
  }
  originalColors.insert(originalColors.end(), colors.begin(), colors.end());
  if (dummyLast) {
    originalColors.push_back(colors[colors.size() - 1]);
  }
  if (positions.empty()) {
    auto posScale = 1.0f / static_cast<float>(colors.size() - 1);
    for (size_t i = 0; i < colors.size(); i++) {
      originalPositions.push_back(static_cast<float>(i) * posScale);
    }
  } else {
    float prev = 0;
    originalPositions.push_back(prev);  // force the first pos to 0
    for (size_t i = dummyFirst ? 0 : 1; i < colors.size() + dummyLast; ++i) {
      // Pin the last value to 1.0, and make sure position is monotonic.
      float curr;
      if (i != colors.size()) {
        curr = std::max(std::min(positions[i], 1.0f), prev);
      } else {
        curr = 1.0f;
      }
      originalPositions.push_back(curr);
      prev = curr;
    }
  }
}

// Combines the colorizer and layout with an appropriately configured primary effect based on the
// gradient's tile mode
static std::unique_ptr<FragmentProcessor> MakeGradient(const Context* context,
                                                       const GradientShader& shader,
                                                       std::unique_ptr<FragmentProcessor> layout) {
  if (layout == nullptr) {
    return nullptr;
  }
  // All gradients are colorized the same way, regardless of layout
  std::unique_ptr<FragmentProcessor> colorizer =
      MakeColorizer(context, shader.originalColors.data(), shader.originalPositions.data(),
                    static_cast<int>(shader.originalColors.size()));
  if (colorizer == nullptr) {
    return nullptr;
  }

  // The primary effect has to export premultiply colors, but under certain conditions it doesn't
  // need to do anything to achieve that: i.e., all the colors have a = 1, in which case
  // premultiply is a no op.
  return ClampedGradientEffect::Make(std::move(colorizer), std::move(layout),
                                     shader.originalColors[0],
                                     shader.originalColors[shader.originalColors.size() - 1]);
}

static Matrix PointsToUnitMatrix(const Point& startPoint, const Point& endPoint) {
  Point vec = {endPoint.x - startPoint.x, endPoint.y - startPoint.y};
  float mag = Point::Length(vec.x, vec.y);
  float inv = mag != 0 ? 1 / mag : 0;
  vec.set(vec.x * inv, vec.y * inv);
  auto matrix = Matrix::I();
  matrix.setSinCos(-vec.y, vec.x, startPoint.x, startPoint.y);
  matrix.postTranslate(-startPoint.x, -startPoint.y);
  matrix.postScale(inv, inv);
  return matrix;
}

static std::array<Point, 2> UnitMatrixToPoints(const Matrix& matrix) {
  Matrix invertMatrix = Matrix::I();
  matrix.invert(&invertMatrix);
  std::array<Point, 2> points{Point::Make(0, 0), Point::Make(1, 0)};
  invertMatrix.mapPoints(points.data(), 2);
  return points;
}

LinearGradientShader::LinearGradientShader(const Point& startPoint, const Point& endPoint,
                                           const std::vector<Color>& colors,
                                           const std::vector<float>& positions)
    : GradientShader(colors, positions, PointsToUnitMatrix(startPoint, endPoint)) {
}

std::unique_ptr<FragmentProcessor> LinearGradientShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  auto totalMatrix = pointsToUnit;
  if (uvMatrix) {
    totalMatrix.preConcat(*uvMatrix);
  }
  return MakeGradient(args.context, *this, LinearGradientLayout::Make(totalMatrix));
}

GradientType LinearGradientShader::asGradient(GradientInfo* info) const {
  if (info) {
    info->colors = originalColors;
    info->positions = originalPositions;
    info->points = UnitMatrixToPoints(pointsToUnit);
  }
  return GradientType::Linear;
}

static Matrix RadialToUnitMatrix(const Point& center, float radius) {
  float inv = 1 / radius;
  auto matrix = Matrix::MakeTrans(-center.x, -center.y);
  matrix.postScale(inv, inv);
  return matrix;
}

static std::tuple<Point, float> UnitMatrixToRadial(const Matrix& matrix) {
  auto invertMatrix = Matrix::I();
  matrix.invert(&invertMatrix);
  std::array<Point, 2> points{Point::Make(0, 0), Point::Make(1, 0)};
  invertMatrix.mapPoints(points.data(), 2);
  return {points[0], Point::Distance(points[0], points[1])};
}

RadialGradientShader::RadialGradientShader(const Point& center, float radius,
                                           const std::vector<Color>& colors,
                                           const std::vector<float>& positions)
    : GradientShader(colors, positions, RadialToUnitMatrix(center, radius)) {
}

std::unique_ptr<FragmentProcessor> RadialGradientShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  auto totalMatrix = pointsToUnit;
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }
  return MakeGradient(args.context, *this, RadialGradientLayout::Make(totalMatrix));
}

GradientType RadialGradientShader::asGradient(GradientInfo* info) const {
  if (info) {
    info->colors = originalColors;
    info->positions = originalPositions;
    std::tie(info->points[0], info->radiuses[0]) = UnitMatrixToRadial(pointsToUnit);
  }
  return GradientType::Radial;
}

ConicGradientShader::ConicGradientShader(const Point& center, float t0, float t1,
                                         const std::vector<Color>& colors,
                                         const std::vector<float>& positions)
    : GradientShader(colors, positions, Matrix::MakeTrans(-center.x, -center.y)), bias(-t0),
      scale(1.f / (t1 - t0)) {
}

std::unique_ptr<FragmentProcessor> ConicGradientShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix) const {
  auto totalMatrix = pointsToUnit;
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }
  return MakeGradient(args.context, *this, ConicGradientLayout::Make(totalMatrix, bias, scale));
}

GradientType ConicGradientShader::asGradient(GradientInfo* info) const {
  if (info) {
    info->colors = originalColors;
    info->positions = originalPositions;
    auto center = Point::Zero();
    pointsToUnit.mapPoints(&center, 1);
    info->points[0] = center * -1.f;
    info->radiuses[0] = -bias * 360.f;
    info->radiuses[1] = (1.f / scale - bias) * 360.f;
  }
  return GradientType::Conic;
}

std::shared_ptr<Shader> Shader::MakeLinearGradient(const Point& startPoint, const Point& endPoint,
                                                   const std::vector<Color>& colors,
                                                   const std::vector<float>& positions) {
  if (!std::isfinite(Point::Distance(endPoint, startPoint)) || colors.empty()) {
    return nullptr;
  }
  if (1 == colors.size()) {
    return Shader::MakeColorShader(colors[0]);
  }
  if (FloatNearlyZero((endPoint - startPoint).length(), DegenerateThreshold)) {
    // Degenerate gradient, the only tricky complication is when in clamp mode, the limit of
    // the gradient approaches two half planes of solid color (first and last). However, they
    // are divided by the line perpendicular to the start and end point, which becomes undefined
    // once start and end are exactly the same, so just use the end color for a stable solution.
    return Shader::MakeColorShader(colors[0]);
  }
  auto shader = std::make_shared<LinearGradientShader>(startPoint, endPoint, colors, positions);
  shader->weakThis = shader;
  return shader;
}

std::shared_ptr<Shader> Shader::MakeRadialGradient(const Point& center, float radius,
                                                   const std::vector<Color>& colors,
                                                   const std::vector<float>& positions) {
  if (radius < 0 || colors.empty()) {
    return nullptr;
  }
  if (1 == colors.size()) {
    return Shader::MakeColorShader(colors[0]);
  }

  if (FloatNearlyZero(radius, DegenerateThreshold)) {
    // Degenerate gradient optimization, and no special logic needed for clamped radial gradient
    return Shader::MakeColorShader(colors[colors.size() - 1]);
  }
  auto shader = std::make_shared<RadialGradientShader>(center, radius, colors, positions);
  shader->weakThis = shader;
  return shader;
}

std::shared_ptr<Shader> Shader::MakeConicGradient(const Point& center, float startAngle,
                                                  float endAngle, const std::vector<Color>& colors,
                                                  const std::vector<float>& positions) {
  if (colors.empty()) {
    return nullptr;
  }
  if (1 == colors.size() || FloatNearlyEqual(startAngle, endAngle, DegenerateThreshold)) {
    return Shader::MakeColorShader(colors[0]);
  }
  auto shader = std::make_shared<ConicGradientShader>(center, startAngle / 360.f, endAngle / 360.f,
                                                      colors, positions);
  shader->weakThis = shader;
  return shader;
}
}  // namespace tgfx