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

#include "tgfx/core/CurveConverter.h"
#include <cmath>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include "pathkit.h"
#pragma clang diagnostic pop

namespace tgfx {
using namespace pk;

namespace {
// Weight for a 90-degree circular arc: cos(45°) = √2/2
constexpr float Conic90DegreeWeight = 0.707106781186548f;
// Tolerance for comparing weights
constexpr float WeightTolerance = 1e-5f;
// Kappa value for 90-degree arc cubic approximation: 4/3 * tan(π/8) = 4/3 * (√2 - 1)
constexpr float Kappa90Degree = 0.552284749830794f;

bool IsNear90DegreeArc(const Point& p0, const Point& p1, const Point& p2, float weight) {
  if (std::abs(weight - Conic90DegreeWeight) > WeightTolerance) {
    return false;
  }
  // Check if tangents at endpoints are perpendicular (90-degree arc property)
  // Tangent at p0: p1 - p0, Tangent at p2: p1 - p2
  Point tangent0 = {p1.x - p0.x, p1.y - p0.y};
  Point tangent1 = {p1.x - p2.x, p1.y - p2.y};
  float dot = (tangent0.x * tangent1.x) + (tangent0.y * tangent1.y);
  float len0Sq = (tangent0.x * tangent0.x) + (tangent0.y * tangent0.y);
  float len1Sq = (tangent1.x * tangent1.x) + (tangent1.y * tangent1.y);
  if (len0Sq < 1e-10f || len1Sq < 1e-10f) {
    return false;
  }
  // cos(angle) = dot / (len0 * len1), for perpendicular: cos ≈ 0
  float cosAngle = dot / std::sqrt(len0Sq * len1Sq);
  return std::abs(cosAngle) < 0.01f;
}

void Conic90DegreeToCubic(const Point& p0, const Point& p1, const Point& p2, Point cubic[4]) {
  // For 90-degree arc, use optimal kappa approximation
  // Tangent directions from control point
  Point tangent0 = {p1.x - p0.x, p1.y - p0.y};
  Point tangent1 = {p1.x - p2.x, p1.y - p2.y};

  cubic[0] = p0;
  cubic[1].x = p0.x + Kappa90Degree * tangent0.x;
  cubic[1].y = p0.y + Kappa90Degree * tangent0.y;
  cubic[2].x = p2.x + Kappa90Degree * tangent1.x;
  cubic[2].y = p2.y + Kappa90Degree * tangent1.y;
  cubic[3] = p2;
}

void QuadToCubic(const Point& p0, const Point& p1, const Point& p2, Point cubic[4]) {
  // Quad to cubic is exact: Q0=P0, Q1=P0+2/3*(P1-P0), Q2=P2+2/3*(P1-P2), Q3=P2
  cubic[0] = p0;
  cubic[1].x = p0.x + (2.0f / 3.0f) * (p1.x - p0.x);
  cubic[1].y = p0.y + (2.0f / 3.0f) * (p1.y - p0.y);
  cubic[2].x = p2.x + (2.0f / 3.0f) * (p1.x - p2.x);
  cubic[2].y = p2.y + (2.0f / 3.0f) * (p1.y - p2.y);
  cubic[3] = p2;
}
}  // namespace

std::vector<Point> CurveConverter::ConicToQuads(const Point& p0, const Point& p1, const Point& p2,
                                                float weight, int pow2) {
  size_t maxQuads = static_cast<size_t>(1) << pow2;
  std::vector<Point> quads(1 + 2 * maxQuads);
  int numQuads = SkPath::ConvertConicToQuads(*reinterpret_cast<const SkPoint*>(&p0),
                                             *reinterpret_cast<const SkPoint*>(&p1),
                                             *reinterpret_cast<const SkPoint*>(&p2), weight,
                                             reinterpret_cast<SkPoint*>(quads.data()), pow2);
  // Resize to actual number of quads
  quads.resize(1 + 2 * static_cast<size_t>(numQuads));
  return quads;
}

std::vector<Point> CurveConverter::ConicToCubics(const Point& p0, const Point& p1, const Point& p2,
                                                 float weight, int pow2) {
  if (IsNear90DegreeArc(p0, p1, p2, weight)) {
    std::vector<Point> cubics(4);
    Conic90DegreeToCubic(p0, p1, p2, cubics.data());
    return cubics;
  }

  // Convert to quads first
  auto quads = ConicToQuads(p0, p1, p2, weight, pow2);
  size_t numQuads = (quads.size() - 1) / 2;

  // Allocate cubics and convert each quad
  std::vector<Point> cubics(1 + 3 * numQuads);
  cubics[0] = quads[0];
  for (size_t i = 0; i < numQuads; ++i) {
    Point cubic[4] = {};
    QuadToCubic(quads[i * 2], quads[i * 2 + 1], quads[i * 2 + 2], cubic);
    cubics[i * 3 + 1] = cubic[1];
    cubics[i * 3 + 2] = cubic[2];
    cubics[i * 3 + 3] = cubic[3];
  }
  return cubics;
}

}  // namespace tgfx
