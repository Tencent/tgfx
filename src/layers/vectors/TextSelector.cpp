/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2026 Tencent. All rights reserved.
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

#include "tgfx/layers/vectors/TextSelector.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>

namespace tgfx {

// ==================== Helper Functions ====================

static float OverlayFactor(float oldFactor, float factor, SelectorMode mode) {
  switch (mode) {
    case SelectorMode::Add:
      return oldFactor + factor;
    case SelectorMode::Subtract:
      return factor >= 0.0f ? oldFactor * (1.0f - factor) : oldFactor * (-1.0f - factor);
    case SelectorMode::Intersect:
      return oldFactor * factor;
    case SelectorMode::Min:
      return std::min(oldFactor, factor);
    case SelectorMode::Max:
      return std::max(oldFactor, factor);
    case SelectorMode::Difference:
      return std::abs(oldFactor - factor);
    default:
      return factor;
  }
}

// Square shape factor calculation
static float CalculateSquareFactor(float textStart, float textEnd, float rangeStart,
                                   float rangeEnd) {
  if (textStart >= rangeEnd || textEnd <= rangeStart) {
    return 0.0f;
  }
  auto textRange = textEnd - textStart;
  if (textStart < rangeStart) {
    textStart = rangeStart;
  }
  if (textEnd > rangeEnd) {
    textEnd = rangeEnd;
  }
  return (textEnd - textStart) / textRange;
}

// Ramp up shape factor calculation
static float CalculateRampUpFactor(float textStart, float textEnd, float rangeStart,
                                   float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter <= rangeStart) {
    return 0.0f;
  }
  if (textCenter >= rangeEnd) {
    return 1.0f;
  }
  return (textCenter - rangeStart) / (rangeEnd - rangeStart);
}

// Ramp down shape factor calculation
static float CalculateRampDownFactor(float textStart, float textEnd, float rangeStart,
                                     float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter <= rangeStart) {
    return 1.0f;
  }
  if (textCenter >= rangeEnd) {
    return 0.0f;
  }
  return (rangeEnd - textCenter) / (rangeEnd - rangeStart);
}

struct CubicSolutions {
  std::array<double, 3> values = {};
  size_t count = 0;
};

// Solve cubic equation a*x^3 + b*x^2 + c*x + d = 0 using Shengjin's formula.
static CubicSolutions SolveCubicEquation(double a, double b, double c, double d) {
  CubicSolutions solutions = {};
  if (a == 0) {
    return solutions;
  }

  auto A = b * b - 3 * a * c;
  auto B = b * c - 9 * a * d;
  auto C = c * c - 3 * b * d;
  auto delta = B * B - 4 * A * C;
  if (A == 0 && B == 0) {
    solutions.values[solutions.count++] = -b / (3 * a);
  } else if (delta == 0 && A != 0) {
    auto k = B / A;
    solutions.values[solutions.count++] = -b / a + k;
    solutions.values[solutions.count++] = -0.5 * k;
  } else if (delta > 0) {
    auto y1 = A * b + 1.5 * a * (-B + std::sqrt(delta));
    auto y2 = A * b + 1.5 * a * (-B - std::sqrt(delta));
    solutions.values[solutions.count++] = (-b - std::cbrt(y1) - std::cbrt(y2)) / (3 * a);
  } else if (delta < 0 && A > 0) {
    auto t = (A * b - 1.5 * a * B) / (A * std::sqrt(A));
    if (-1 < t && t < 1) {
      auto theta = std::acos(t);
      auto sqrtA = std::sqrt(A);
      auto cosA = std::cos(theta / 3);
      auto sinA = std::sin(theta / 3);
      solutions.values[solutions.count++] = (-b - 2 * sqrtA * cosA) / (3 * a);
      solutions.values[solutions.count++] = (-b + sqrtA * (cosA + std::sqrt(3.0) * sinA)) / (3 * a);
      solutions.values[solutions.count++] = (-b + sqrtA * (cosA - std::sqrt(3.0) * sinA)) / (3 * a);
    }
  }
  return solutions;
}

static bool DoubleNearlyEqual(double a, double b, double tolerance) {
  return std::abs(a - b) <= tolerance;
}

// Triangle shape factor calculation with easeOut/easeIn bezier curve support.
static float CalculateTriangleFactor(float textStart, float textEnd, float rangeStart,
                                     float rangeEnd, float easeOut, float easeIn) {
  if (rangeStart >= rangeEnd) {
    return 0.0f;
  }
  auto textCenter = (textStart + textEnd) * 0.5f;
  auto rangeCenter = (rangeStart + rangeEnd) * 0.5f;
  double x = textCenter;

  if (x < rangeStart || x > rangeEnd) {
    return 0.0f;
  }

  // Cubic bezier curve control points
  double x1 = x <= rangeCenter ? rangeStart : rangeEnd;
  double y1 = 0;
  double step = rangeCenter - x1;
  double x2 = easeIn >= 0 ? x1 + easeIn * step : x1;
  double y2 = easeIn >= 0 ? 0 : -easeIn;
  double x3 = easeOut >= 0 ? rangeCenter - easeOut * step : rangeCenter;
  double y3 = easeOut >= 0 ? 1 : 1 + easeOut;
  double x4 = rangeCenter;
  double y4 = 1;

  // Solve cubic equation to find parameter t from x.
  // From P = (1-t)^3*P1 + 3*(1-t)^2*t*P2 + 3*(1-t)*t^2*P3 + t^3*P4,
  // we get: a*t^3 + b*t^2 + c*t + d = 0
  auto a = -x1 + 3 * x2 - 3 * x3 + x4;
  auto b = 3 * (x1 - 2 * x2 + x3);
  auto c = 3 * (-x1 + x2);
  auto d = x1 - x;

  double t = 0;
  auto solutions = SolveCubicEquation(a, b, c, d);
  for (size_t i = 0; i < solutions.count; i++) {
    auto solution = solutions.values[i];
    if ((solution >= 0 && solution <= 1) || DoubleNearlyEqual(solution, 1, 1e-6)) {
      t = solution;
      break;
    }
  }

  // Calculate y from t using cubic bezier formula
  auto oneMinusT = 1 - t;
  return static_cast<float>(oneMinusT * oneMinusT * oneMinusT * y1 +
                            3 * oneMinusT * oneMinusT * t * y2 + 3 * oneMinusT * t * t * y3 +
                            t * t * t * y4);
}

// Round shape factor calculation (circular falloff)
static float CalculateRoundFactor(float textStart, float textEnd, float rangeStart,
                                  float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter >= rangeEnd || textCenter <= rangeStart) {
    return 0.0f;
  }
  auto rangeCenter = (rangeStart + rangeEnd) * 0.5f;
  auto radius = (rangeEnd - rangeStart) * 0.5f;
  auto xWidth = rangeCenter - textCenter;
  auto yHeight = std::sqrt(radius * radius - xWidth * xWidth);
  return yHeight / radius;
}

// Cubic bezier easing: given x, find y on the curve from (0,0) to (1,1)
// with control points P1(cx1, cy1) and P2(cx2, cy2).
// Uses Newton-Raphson iteration to find t from x, then calculates y.
static float CubicBezierEasing(float x, float cx1, float cy1, float cx2, float cy2) {
  if (x <= 0.0f) {
    return 0.0f;
  }
  if (x >= 1.0f) {
    return 1.0f;
  }

  // Solve for t given x using Newton-Raphson iteration.
  // x(t) = 3*(1-t)^2*t*cx1 + 3*(1-t)*t^2*cx2 + t^3
  float t = x;
  for (int i = 0; i < 8; i++) {
    float t2 = t * t;
    float t3 = t2 * t;
    float mt = 1.0f - t;
    float mt2 = mt * mt;

    // x(t) = 3*mt2*t*cx1 + 3*mt*t2*cx2 + t3
    float xt = 3.0f * mt2 * t * cx1 + 3.0f * mt * t2 * cx2 + t3;
    float dx = xt - x;
    if (std::abs(dx) < 1e-6f) {
      break;
    }

    // x'(t) = 3*mt2*cx1 + 6*mt*t*(cx2-cx1) + 3*t2*(1-cx2)
    float dxt = 3.0f * mt2 * cx1 + 6.0f * mt * t * (cx2 - cx1) + 3.0f * t2 * (1.0f - cx2);
    if (std::abs(dxt) < 1e-6f) {
      break;
    }
    t -= dx / dxt;
    t = std::clamp(t, 0.0f, 1.0f);
  }

  // Calculate y(t)
  float t2 = t * t;
  float t3 = t2 * t;
  float mt = 1.0f - t;
  float mt2 = mt * mt;
  return 3.0f * mt2 * t * cy1 + 3.0f * mt * t2 * cy2 + t3;
}

// Smooth shape factor calculation using cubic bezier curve.
// Uses control points (0.5, 0.0) and (0.5, 1.0) to match AE behavior.
static float CalculateSmoothFactor(float textStart, float textEnd, float rangeStart,
                                   float rangeEnd) {
  auto textCenter = (textStart + textEnd) * 0.5f;
  if (textCenter >= rangeEnd || textCenter <= rangeStart) {
    return 0.0f;
  }
  auto rangeCenter = (rangeStart + rangeEnd) * 0.5f;
  float x = 0.0f;
  if (textCenter < rangeCenter) {
    x = (textCenter - rangeStart) / (rangeCenter - rangeStart);
  } else {
    x = (rangeEnd - textCenter) / (rangeEnd - rangeCenter);
  }

  // Cubic bezier easing with control points P1(0.5, 0.0) and P2(0.5, 1.0).
  return CubicBezierEasing(x, 0.5f, 0.0f, 0.5f, 1.0f);
}

// Lookup table for the first character's random index when seed == 0.
// The specific values are obtained from AE testing. The random value in AE only depends on
// the character count, so this table maps character count ranges to the first character's index.
// Format: {end, index} means when textCount is in range [previous_end+1, end], first char index is
// 'index'.
static const std::pair<int, int> RANDOM_RANGES[] = {
    {2, 0}, {4, 2}, {5, 4}, {20, 5}, {69, 20}, {127, 69}, {206, 127}, {10000, 205},
};

static size_t GetFirstCharRandomIndex(size_t textCount) {
  for (const auto& range : RANDOM_RANGES) {
    if (static_cast<int>(textCount) <= range.first) {
      return static_cast<size_t>(range.second);
    }
  }
  return 0;
}

// Generate a deterministic pseudo-random index mapping using sort-based shuffle.
static std::vector<size_t> BuildRandomIndices(size_t totalCount, uint16_t seed) {
  std::mt19937 rng(seed);
  std::vector<std::pair<uint32_t, size_t>> randList = {};
  randList.reserve(totalCount);
  for (size_t i = 0; i < totalCount; i++) {
    randList.emplace_back(rng(), i);
  }

  std::sort(randList.begin(), randList.end(),
            [](const std::pair<uint32_t, size_t>& a, const std::pair<uint32_t, size_t>& b) {
              return a.first < b.first;
            });

  std::vector<size_t> randomIndices = {};
  randomIndices.reserve(totalCount);
  for (const auto& item : randList) {
    randomIndices.push_back(item.second);
  }

  // Special handling for seed == 0: use lookup table for the first character.
  if (seed == 0 && totalCount > 1) {
    auto m = GetFirstCharRandomIndex(totalCount);
    size_t k = 0;
    while (k < totalCount && randomIndices[k] != m) {
      k++;
    }
    if (k < totalCount) {
      std::swap(randomIndices[0], randomIndices[k]);
    }
  }

  return randomIndices;
}

// ==================== TextSelector ====================

float TextSelector::CalculateCombinedFactor(
    const std::vector<std::shared_ptr<TextSelector>>& selectors, size_t index, size_t totalCount) {
  if (selectors.empty()) {
    return 1.0f;
  }

  float totalFactor = 1.0f;
  bool isFirst = true;

  for (const auto& selector : selectors) {
    if (selector == nullptr) {
      continue;
    }
    float factor = selector->calculateFactor(index, totalCount);

    if (isFirst && selector->mode() != SelectorMode::Subtract) {
      totalFactor = factor;
    } else {
      totalFactor = OverlayFactor(totalFactor, factor, selector->mode());
    }
    isFirst = false;
  }

  return std::clamp(totalFactor, -1.0f, 1.0f);
}

void TextSelector::setMode(SelectorMode value) {
  if (_mode == value) {
    return;
  }
  _mode = value;
  invalidateContent();
}

void TextSelector::setWeight(float value) {
  if (_weight == value) {
    return;
  }
  _weight = value;
  invalidateContent();
}

// ==================== RangeSelector ====================

void RangeSelector::setStart(float value) {
  if (_start == value) {
    return;
  }
  _start = value;
  invalidateContent();
}

void RangeSelector::setEnd(float value) {
  if (_end == value) {
    return;
  }
  _end = value;
  invalidateContent();
}

void RangeSelector::setOffset(float value) {
  if (_offset == value) {
    return;
  }
  _offset = value;
  invalidateContent();
}

void RangeSelector::setUnit(SelectorUnit value) {
  if (_unit == value) {
    return;
  }
  _unit = value;
  invalidateContent();
}

void RangeSelector::setShape(SelectorShape value) {
  if (_shape == value) {
    return;
  }
  _shape = value;
  invalidateContent();
}

void RangeSelector::setEaseOut(float value) {
  if (_easeOut == value) {
    return;
  }
  _easeOut = value;
  invalidateContent();
}

void RangeSelector::setEaseIn(float value) {
  if (_easeIn == value) {
    return;
  }
  _easeIn = value;
  invalidateContent();
}

void RangeSelector::setRandomizeOrder(bool value) {
  if (_randomizeOrder == value) {
    return;
  }
  _randomizeOrder = value;
  invalidateContent();
}

void RangeSelector::setRandomSeed(uint16_t value) {
  if (_randomSeed == value) {
    return;
  }
  _randomSeed = value;
  invalidateContent();
}

float RangeSelector::calculateFactor(size_t index, size_t totalCount) const {
  if (totalCount == 0) {
    return 0.0f;
  }

  size_t effectiveIndex = index;
  if (_randomizeOrder) {
    auto randomIndices = BuildRandomIndices(totalCount, _randomSeed);
    effectiveIndex = randomIndices[index];
  }

  auto textStart = static_cast<float>(effectiveIndex) / static_cast<float>(totalCount);
  auto textEnd = static_cast<float>(effectiveIndex + 1) / static_cast<float>(totalCount);

  float rangeStart = _start;
  float rangeEnd = _end;
  if (_unit == SelectorUnit::Index) {
    rangeStart = _start / static_cast<float>(totalCount);
    rangeEnd = _end / static_cast<float>(totalCount);
  }

  rangeStart += _offset;
  rangeEnd += _offset;

  if (rangeStart > rangeEnd) {
    std::swap(rangeStart, rangeEnd);
  }

  float factor = 0.0f;
  switch (_shape) {
    case SelectorShape::RampUp:
      factor = CalculateRampUpFactor(textStart, textEnd, rangeStart, rangeEnd);
      break;
    case SelectorShape::RampDown:
      factor = CalculateRampDownFactor(textStart, textEnd, rangeStart, rangeEnd);
      break;
    case SelectorShape::Triangle:
      factor = CalculateTriangleFactor(textStart, textEnd, rangeStart, rangeEnd, _easeOut, _easeIn);
      break;
    case SelectorShape::Round:
      factor = CalculateRoundFactor(textStart, textEnd, rangeStart, rangeEnd);
      break;
    case SelectorShape::Smooth:
      factor = CalculateSmoothFactor(textStart, textEnd, rangeStart, rangeEnd);
      break;
    default:
      factor = CalculateSquareFactor(textStart, textEnd, rangeStart, rangeEnd);
      break;
  }

  factor = std::clamp(factor, 0.0f, 1.0f);
  factor *= weight();
  return factor;
}

}  // namespace tgfx
