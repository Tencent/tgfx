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

#include "tgfx/layers/vectors/Polystar.h"
#include <cmath>
#include "VectorContext.h"
#include "core/utils/Log.h"
#include "core/utils/MathExtra.h"

namespace tgfx {

static void AddCurveToPath(Path* path, float centerX, float centerY, float angleDelta, float dx1,
                           float dy1, float roundness1, float dx2, float dy2, float roundness2) {
  auto control1X = dx1 - dy1 * roundness1 * angleDelta + centerX;
  auto control1Y = dy1 + dx1 * roundness1 * angleDelta + centerY;
  auto control2X = dx2 + dy2 * roundness2 * angleDelta + centerX;
  auto control2Y = dy2 - dx2 * roundness2 * angleDelta + centerY;
  path->cubicTo(control1X, control1Y, control2X, control2Y, dx2 + centerX, dy2 + centerY);
}

static void ConvertStarToPath(Path* path, float centerX, float centerY, float points,
                              float rotation, float innerRadius, float outerRadius,
                              float innerRoundness, float outerRoundness, bool reversed) {
  if (points <= 0.0f) {
    return;
  }
  float direction = reversed ? -1.0f : 1.0f;
  auto angleStep = static_cast<float>(M_PI) / points;
  auto currentAngle = DegreesToRadians(rotation - 90.0f);
  auto numPoints = static_cast<int>(ceilf(points)) * 2;
  auto decimalPart = points - floorf(points);
  int decimalIndex = -2;
  if (decimalPart != 0) {
    decimalIndex = direction > 0 ? 1 : numPoints - 3;
    currentAngle -= angleStep * decimalPart * 2.0f;
  }

  auto firstDx = outerRadius * cosf(currentAngle);
  auto firstDy = outerRadius * sinf(currentAngle);
  auto lastDx = firstDx;
  auto lastDy = firstDy;
  path->moveTo(lastDx + centerX, lastDy + centerY);

  auto outerFlag = false;
  for (int i = 0; i < numPoints; i++) {
    auto angleDelta = angleStep * direction;
    float dx = 0.0f;
    float dy = 0.0f;
    if (i == numPoints - 1) {
      dx = firstDx;
      dy = firstDy;
    } else {
      auto radius = outerFlag ? outerRadius : innerRadius;
      if (i == decimalIndex || i == decimalIndex + 1) {
        radius = innerRadius + decimalPart * (radius - innerRadius);
        angleDelta *= decimalPart;
      }
      currentAngle += angleDelta;
      dx = radius * cosf(currentAngle);
      dy = radius * sinf(currentAngle);
    }
    if (innerRoundness != 0 || outerRoundness != 0) {
      float lastRoundness = 0.0f;
      float roundness = 0.0f;
      if (outerFlag) {
        lastRoundness = innerRoundness;
        roundness = outerRoundness;
      } else {
        lastRoundness = outerRoundness;
        roundness = innerRoundness;
      }
      AddCurveToPath(path, centerX, centerY, angleDelta * 0.5f, lastDx, lastDy, lastRoundness, dx,
                     dy, roundness);
      lastDx = dx;
      lastDy = dy;
    } else {
      path->lineTo(dx + centerX, dy + centerY);
    }
    outerFlag = !outerFlag;
  }
  path->close();
}

static void ConvertPolygonToPath(Path* path, float centerX, float centerY, float points,
                                 float rotation, float radius, float roundness, bool reversed) {
  if (points <= 0.0f) {
    return;
  }
  auto numPoints = static_cast<int>(floorf(points));
  float direction = reversed ? -1.0f : 1.0f;
  auto angleStep = static_cast<float>(M_PI) * 2.0f / static_cast<float>(numPoints);
  auto currentAngle = DegreesToRadians(rotation - 90.0f);

  auto firstDx = radius * cosf(currentAngle);
  auto firstDy = radius * sinf(currentAngle);
  auto lastDx = firstDx;
  auto lastDy = firstDy;
  path->moveTo(lastDx + centerX, lastDy + centerY);

  for (int i = 0; i < numPoints; i++) {
    auto angleDelta = angleStep * direction;
    float dx = 0.0f;
    float dy = 0.0f;
    if (i == numPoints - 1) {
      dx = firstDx;
      dy = firstDy;
    } else {
      currentAngle += angleDelta;
      dx = radius * cosf(currentAngle);
      dy = radius * sinf(currentAngle);
    }
    if (roundness != 0) {
      AddCurveToPath(path, centerX, centerY, angleDelta * 0.25f, lastDx, lastDy, roundness, dx, dy,
                     roundness);
      lastDx = dx;
      lastDy = dy;
    } else {
      path->lineTo(dx + centerX, dy + centerY);
    }
  }
  path->close();
}

class PolystarPathProvider final : public PathProvider {
 public:
  PolystarPathProvider(Point center, PolystarType polystarType, float pointCount, float rotation,
                       float outerRadius, float outerRoundness, float innerRadius,
                       float innerRoundness, bool reversed)
      : _center(center), _polystarType(polystarType), _pointCount(pointCount), _rotation(rotation),
        _outerRadius(outerRadius), _outerRoundness(outerRoundness), _innerRadius(innerRadius),
        _innerRoundness(innerRoundness), _reversed(reversed) {
  }

  Path getPath() const override {
    Path path;
    if (_polystarType == PolystarType::Star) {
      ConvertStarToPath(&path, _center.x, _center.y, _pointCount, _rotation, _innerRadius,
                        _outerRadius, _innerRoundness, _outerRoundness, _reversed);
    } else {
      ConvertPolygonToPath(&path, _center.x, _center.y, _pointCount, _rotation, _outerRadius,
                           _outerRoundness, _reversed);
    }
    return path;
  }

  Rect getBounds() const override {
    auto radius = _outerRadius;
    return Rect::MakeXYWH(_center.x - radius, _center.y - radius, radius * 2, radius * 2);
  }

 private:
  Point _center = Point::Zero();
  PolystarType _polystarType = PolystarType::Star;
  float _pointCount = 5.0f;
  float _rotation = 0.0f;
  float _outerRadius = 100.0f;
  float _outerRoundness = 0.0f;
  float _innerRadius = 50.0f;
  float _innerRoundness = 0.0f;
  bool _reversed = false;
};

void Polystar::setCenter(const Point& value) {
  if (_center == value) {
    return;
  }
  _center = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setPolystarType(PolystarType value) {
  if (_polystarType == value) {
    return;
  }
  _polystarType = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setPointCount(float value) {
  if (_pointCount == value) {
    return;
  }
  _pointCount = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setRotation(float value) {
  if (_rotation == value) {
    return;
  }
  _rotation = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setOuterRadius(float value) {
  if (_outerRadius == value) {
    return;
  }
  _outerRadius = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setOuterRoundness(float value) {
  if (_outerRoundness == value) {
    return;
  }
  _outerRoundness = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setInnerRadius(float value) {
  if (_innerRadius == value) {
    return;
  }
  _innerRadius = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setInnerRoundness(float value) {
  if (_innerRoundness == value) {
    return;
  }
  _innerRoundness = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::setReversed(bool value) {
  if (_reversed == value) {
    return;
  }
  _reversed = value;
  _cachedShape = nullptr;
  invalidateContent();
}

void Polystar::apply(VectorContext* context) {
  DEBUG_ASSERT(context != nullptr);
  if (_cachedShape == nullptr) {
    auto pathProvider = std::make_shared<PolystarPathProvider>(
        _center, _polystarType, _pointCount, _rotation, _outerRadius, _outerRoundness, _innerRadius,
        _innerRoundness, _reversed);
    _cachedShape = Shape::MakeFrom(std::move(pathProvider));
  }
  if (_cachedShape) {
    context->addShape(_cachedShape);
  }
}

}  // namespace tgfx
