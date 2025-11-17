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

#include "GradientShader.h"
#include "core/utils/MathExtra.h"
#include "core/utils/Types.h"
#include "gpu/GlobalCache.h"
#include "gpu/ShaderCaps.h"
#include "gpu/processors/ClampedGradientEffect.h"
#include "gpu/processors/ConicGradientLayout.h"
#include "gpu/processors/DiamondGradientLayout.h"
#include "gpu/processors/DualIntervalGradientColorizer.h"
#include "gpu/processors/LinearGradientLayout.h"
#include "gpu/processors/RadialGradientLayout.h"
#include "gpu/processors/SingleIntervalGradientColorizer.h"
#include "gpu/processors/TextureGradientColorizer.h"
#include "gpu/processors/UnrolledBinaryGradientColorizer.h"

namespace tgfx {
static constexpr float DegenerateThreshold = 1.0f / (1 << 15);

// Analyze the shader's color stops and positions and chooses an appropriate colorizer to represent
// the gradient.
static PlacementPtr<FragmentProcessor> MakeColorizer(const Context* context, const Color* colors,
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
  auto drawingBuffer = context->drawingAllocator();
  // Two remaining colors means a single interval from 0 to 1
  // (but it may have originally been a 3 or 4 color gradient with 1-2 hard stops at the ends)
  if (count == 2) {
    return SingleIntervalGradientColorizer::Make(drawingBuffer, colors[offset], colors[offset + 1]);
  }

  if (count <= UnrolledBinaryGradientColorizer::MaxColorCount) {
    if (count == 3) {
      // Must be a dual interval gradient, where the middle point is at offset+1 and the two
      // intervals share the middle color stop.
      return DualIntervalGradientColorizer::Make(drawingBuffer, colors[offset], colors[offset + 1],
                                                 colors[offset + 1], colors[offset + 2],
                                                 positions[offset + 1]);
    } else if (count == 4 && FloatNearlyEqual(positions[offset + 1], positions[offset + 2])) {
      // Two separate intervals that join at the same threshold position
      return DualIntervalGradientColorizer::Make(drawingBuffer, colors[offset], colors[offset + 1],
                                                 colors[offset + 2], colors[offset + 3],
                                                 positions[offset + 1]);
    }

    // The single and dual intervals are a specialized case of the unrolled binary search
    // colorizer which can analytically render gradients of up to 8 intervals (up to 9 or 16
    // colors depending on how many hard stops are inserted).
    auto unrolled = UnrolledBinaryGradientColorizer::Make(drawingBuffer, colors + offset,
                                                          positions + offset, count);
    if (unrolled) {
      return unrolled;
    }
  }
  // Otherwise, fall back to a raster gradient sample by a texture, which can handle
  // arbitrary gradients (the only downside being sampling resolution).
  auto gradient = context->globalCache()->getGradient(colors + offset, positions + offset, count);
  return TextureGradientColorizer::Make(drawingBuffer, std::move(gradient));
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
static PlacementPtr<FragmentProcessor> MakeGradient(const Context* context,
                                                    const GradientShader& shader,
                                                    PlacementPtr<FragmentProcessor> layout,
                                                    std::shared_ptr<ColorSpace> dstColorSpace) {
  if (layout == nullptr) {
    return nullptr;
  }
  auto dstColors = shader.originalColors;

  if (dstColorSpace != nullptr) {
    for (auto& color : dstColors) {
      color.applyColorSpace(std::move(dstColorSpace), false);
    }
  }

  // All gradients are colorized the same way, regardless of layout
  PlacementPtr<FragmentProcessor> colorizer =
      MakeColorizer(context, dstColors.data(), shader.originalPositions.data(),
                    static_cast<int>(dstColors.size()));
  if (colorizer == nullptr) {
    return nullptr;
  }

  // The primary effect has to export premultiply colors, but under certain conditions it doesn't
  // need to do anything to achieve that: i.e., all the colors have a = 1, in which case
  // premultiply is a no op.
  return ClampedGradientEffect::Make(context->drawingAllocator(), std::move(colorizer),
                                     std::move(layout), dstColors[0],
                                     dstColors[dstColors.size() - 1]);
}

static Matrix PointsToUnitMatrix(const Point& startPoint, const Point& endPoint) {
  Point vec = {endPoint.x - startPoint.x, endPoint.y - startPoint.y};
  float mag = Point::Length(vec.x, vec.y);
  float inv = mag != 0 ? 1 / mag : 0;
  vec.set(vec.x * inv, vec.y * inv);
  Matrix matrix = {};
  matrix.setSinCos(-vec.y, vec.x, startPoint.x, startPoint.y);
  matrix.postTranslate(-startPoint.x, -startPoint.y);
  matrix.postScale(inv, inv);
  return matrix;
}

static std::array<Point, 2> UnitMatrixToPoints(const Matrix& matrix) {
  Matrix invertMatrix = {};
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

PlacementPtr<FragmentProcessor> LinearGradientShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto totalMatrix = pointsToUnit;
  if (uvMatrix) {
    totalMatrix.preConcat(*uvMatrix);
  }
  return MakeGradient(args.context, *this,
                      LinearGradientLayout::Make(args.context->drawingAllocator(), totalMatrix),
                      std::move(dstColorSpace));
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
  Matrix invertMatrix = {};
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

PlacementPtr<FragmentProcessor> RadialGradientShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto totalMatrix = pointsToUnit;
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }
  return MakeGradient(args.context, *this,
                      RadialGradientLayout::Make(args.context->drawingAllocator(), totalMatrix),
                      std::move(dstColorSpace));
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

PlacementPtr<FragmentProcessor> ConicGradientShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto totalMatrix = pointsToUnit;
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }
  return MakeGradient(
      args.context, *this,
      ConicGradientLayout::Make(args.context->drawingAllocator(), totalMatrix, bias, scale),
      std::move(dstColorSpace));
}

GradientType ConicGradientShader::asGradient(GradientInfo* info) const {
  if (info) {
    info->colors = originalColors;
    info->positions = originalPositions;
    Point center = {};
    pointsToUnit.mapPoints(&center, 1);
    info->points[0] = center * -1.f;
    info->radiuses[0] = -bias * 360.f;
    info->radiuses[1] = (1.f / scale - bias) * 360.f;
  }
  return GradientType::Conic;
}

static Matrix DiamondHalfDiagonalToUnitMatrix(const Point& center, float halfDiagonal) {
  // sqrt(2) / half-diagonal to calculate the side length of the diamond
  float inv = 1.4142135624f / halfDiagonal;
  auto matrix = Matrix::MakeTrans(-center.x, -center.y);
  matrix.postScale(inv, inv);
  matrix.postRotate(45);
  return matrix;
}

static std::tuple<Point, float> UnitMatrixToDiamondHalfDiagonal(const Matrix& matrix) {
  auto invertMatrix = matrix;
  invertMatrix.postRotate(-45);
  invertMatrix.invert(&invertMatrix);
  std::array<Point, 2> points{Point::Make(0, 0), Point::Make(2, 0)};
  invertMatrix.mapPoints(points.data(), 2);
  // half diagonal =  side length / sqrt(2)
  return {points[0], Point::Distance(points[0], points[1]) / 1.4142135624f};
}

DiamondGradientShader::DiamondGradientShader(const Point& center, float halfDiagonal,
                                             const std::vector<Color>& colors,
                                             const std::vector<float>& positions)
    : GradientShader(colors, positions, DiamondHalfDiagonalToUnitMatrix(center, halfDiagonal)) {
}

PlacementPtr<FragmentProcessor> DiamondGradientShader::asFragmentProcessor(
    const FPArgs& args, const Matrix* uvMatrix, std::shared_ptr<ColorSpace> dstColorSpace) const {
  auto totalMatrix = pointsToUnit;
  if (uvMatrix != nullptr) {
    totalMatrix.preConcat(*uvMatrix);
  }
  auto layout = DiamondGradientLayout::Make(args.context->drawingAllocator(), totalMatrix);
  return MakeGradient(args.context, *this, std::move(layout), std::move(dstColorSpace));
}

GradientType DiamondGradientShader::asGradient(GradientInfo* info) const {
  if (info) {
    info->colors = originalColors;
    info->positions = originalPositions;
    std::tie(info->points[0], info->radiuses[0]) = UnitMatrixToDiamondHalfDiagonal(pointsToUnit);
  }
  return GradientType::Diamond;
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

std::shared_ptr<Shader> Shader::MakeDiamondGradient(const Point& center, float halfDiagonal,
                                                    const std::vector<Color>& colors,
                                                    const std::vector<float>& positions) {
  if (halfDiagonal < 0 || colors.empty()) {
    return nullptr;
  }
  if (1 == colors.size()) {
    return Shader::MakeColorShader(colors[0]);
  }

  if (FloatNearlyZero(halfDiagonal, DegenerateThreshold)) {
    // Degenerate gradient optimization, and no special logic needed for clamped diamond gradient
    return Shader::MakeColorShader(colors[colors.size() - 1]);
  }
  auto shader = std::make_shared<DiamondGradientShader>(center, halfDiagonal, colors, positions);
  shader->weakThis = shader;
  return shader;
}

}  // namespace tgfx