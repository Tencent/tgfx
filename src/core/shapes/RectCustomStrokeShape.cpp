/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making tgfx available.
//
//  Copyright (C) 2025 Tencent. All rights reserved.
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

#include "RectCustomStrokeShape.h"
#include "core/utils/MathExtra.h"
#include "core/utils/UniqueID.h"
#include "tgfx/core/BytesKey.h"

namespace tgfx {
static constexpr size_t CornerCount = 4;

static void RectInset(float dLeft, float dTop, float dRight, float dBottom, Rect* rect) {
  rect->left += dLeft;
  rect->top += dTop;
  rect->right -= dRight;
  rect->bottom -= dBottom;
}

static void RectOutset(float dLeft, float dTop, float dRight, float dBottom, Rect* rect) {
  RectInset(-dLeft, -dTop, -dRight, -dBottom, rect);
}

static void GetTrianglePoints(const Rect& rect, size_t excludeIndex, Point outPoints[3]) {
  const Point corners[CornerCount] = {{rect.left, rect.top},
                                      {rect.right, rect.top},
                                      {rect.right, rect.bottom},
                                      {rect.left, rect.bottom}};
  excludeIndex %= CornerCount;
  size_t outIndex = 0;
  for (size_t i = 0; i < CornerCount; i++) {
    if (excludeIndex == i) {
      continue;
    }
    outPoints[outIndex++] = corners[i];
  }
}

static void AddSolidCornerJoin(const Rect& outerRect, const Rect& innerRect, LineJoin lineJoin,
                               Path* path) {
  if (lineJoin == LineJoin::Miter) {
    return;
  }
  const Rect rects[4] = {
      Rect::MakeLTRB(outerRect.left, outerRect.top, innerRect.left, innerRect.top),
      Rect::MakeLTRB(innerRect.right, outerRect.top, outerRect.right, innerRect.top),
      Rect::MakeLTRB(innerRect.right, innerRect.bottom, outerRect.right, outerRect.bottom),
      Rect::MakeLTRB(outerRect.left, innerRect.bottom, innerRect.left, outerRect.bottom)};

  if (lineJoin == LineJoin::Round) {
    Point radii[CornerCount];
    for (size_t i = 0; i < CornerCount; ++i) {
      auto min = std::min(rects[i].width(), rects[i].height());
      radii[i] = {min, min};
    }
    Path roundPath;
    roundPath.addRoundRect(outerRect, radii);
    path->addPath(roundPath, PathOp::Intersect);

  } else if (lineJoin == LineJoin::Bevel) {
    Point trianglePoints[3] = {};
    Path triangles;
    for (size_t i = 0; i < CornerCount; ++i) {
      GetTrianglePoints(rects[i], (i + 2) % CornerCount, trianglePoints);
      triangles.moveTo(trianglePoints[0]);
      triangles.lineTo(trianglePoints[1]);
      triangles.lineTo(trianglePoints[2]);
      triangles.close();
    }
    path->addPath(triangles, PathOp::Difference);
  }
}

static void AddDashCornerJoin(const Rect& outerRect, const Rect& innerRect, LineJoin lineJoin,
                              Path* path) {
  Rect rects[4] = {
      Rect::MakeLTRB(outerRect.left, outerRect.top, innerRect.left, innerRect.top),
      Rect::MakeLTRB(innerRect.right, outerRect.top, outerRect.right, innerRect.top),
      Rect::MakeLTRB(innerRect.right, innerRect.bottom, outerRect.right, outerRect.bottom),
      Rect::MakeLTRB(outerRect.left, innerRect.bottom, innerRect.left, outerRect.bottom)};

  switch (lineJoin) {
    case LineJoin::Miter:
      for (const auto& rect : rects) {
        if (FloatNearlyZero(rect.area())) {
          continue;
        }
        path->addRect(rect);
      }
      break;
    case LineJoin::Bevel: {
      Point trianglePoints[3] = {};
      for (size_t i = 0; i < CornerCount; ++i) {
        GetTrianglePoints(rects[i], i, trianglePoints);
        path->moveTo(trianglePoints[0]);
        path->lineTo(trianglePoints[1]);
        path->lineTo(trianglePoints[2]);
        path->close();
      }
      break;
    }
    case LineJoin::Round:
      for (size_t i = 0; i < CornerCount; ++i) {
        Point radii[CornerCount] = {};
        auto& rect = rects[i];
        if (FloatNearlyZero(rect.area())) {
          continue;
        }
        auto min = std::min(rect.width(), rect.height());
        radii[i] = {min, min};
        Path cornerPath;
        cornerPath.addRoundRect(rect, radii);
        path->addPath(cornerPath, PathOp::Append);
      }
      break;
    default:
      break;
  }
}

RectCustomStrokeShape::RectCustomStrokeShape(std::shared_ptr<Shape> rectShape, const Stroke& stroke)
    : shape(std::move(rectShape)), stroke(stroke) {
}

Rect RectCustomStrokeShape::getBounds() const {
  auto bounds = shape->getBounds();
  if (_strokeAlign == StrokeAlign::Inside) {
    return bounds;
  }
  auto ratio = _strokeAlign == StrokeAlign::Outside ? 1.f : 0.5f;
  bounds.left -= _borderWeights[0] * ratio;
  bounds.top -= _borderWeights[1] * ratio;
  bounds.right += _borderWeights[2] * ratio;
  bounds.bottom += _borderWeights[3] * ratio;
  return bounds;
}

Path RectCustomStrokeShape::getPath() const {
  auto path = shape->getPath();
  Rect rect = {};
  if (!path.isRect(&rect)) {
    return path;
  }
  auto hasRadius = std::any_of(std::begin(_radii), std::end(_radii),
                               [](float radius) { return radius != 0.0f; });
  if (hasRadius) {
    ApplyRRectStroke(rect, &path);
  } else {
    ApplyRectStroke(rect, &path);
  }
  return path;
}

UniqueKey RectCustomStrokeShape::getUniqueKey() const {
  static const auto WidthStrokeShapeType = UniqueID::Next();
  static const auto CapJoinStrokeShapeType = UniqueID::Next();

  auto hasRadius = std::any_of(std::begin(_radii), std::end(_radii),
                               [](float radius) { return radius != 0.0f; });
  auto hasCapJoin = stroke.cap != LineCap::Butt || (!hasRadius && stroke.join != LineJoin::Miter);
  auto type = hasCapJoin ? CapJoinStrokeShapeType : WidthStrokeShapeType;
  size_t count = 9 + (hasCapJoin ? 1 : 0);
  BytesKey bytesKey;
  bytesKey.reserve(count);
  bytesKey.write(type);
  std::for_each(std::begin(_radii), std::end(_radii),
                [&bytesKey](float radius) { bytesKey.write(radius); });
  std::for_each(std::begin(_borderWeights), std::end(_borderWeights),
                [&bytesKey](float weight) { bytesKey.write(weight); });
  if (hasCapJoin) {
    bytesKey.write(static_cast<uint32_t>(stroke.join) << 16 | static_cast<uint32_t>(stroke.cap));
  }
  return UniqueKey::Append(shape->getUniqueKey(), bytesKey.data(), bytesKey.size());
}

void RectCustomStrokeShape::setCornerRadii(const std::array<float, 4>& radii) {
  _radii = radii;
  for (auto& radius : _radii) {
    radius = std::max(0.f, radius);
  }
}

void RectCustomStrokeShape::setBorderWeights(const std::array<float, 4>& borderWeights) {
  _borderWeights = borderWeights;
  for (auto& weight : _borderWeights) {
    weight = std::max(0.f, weight);
  }
}

void RectCustomStrokeShape::ApplyRectStroke(const Rect& rect, Path* path) const {
  switch (_strokeAlign) {
    case StrokeAlign::Inside:
      ApplyRectInsideStroke(rect, path);
      break;
    case StrokeAlign::Outside:
      ApplyRectOutsideStroke(rect, path);
      break;
    case StrokeAlign::Center:
      ApplyRectCenterStroke(rect, path);
      break;
    default:
      break;
  }
}

void RectCustomStrokeShape::ApplyRRectStroke(const Rect& rect, Path* path) const {
  switch (_strokeAlign) {
    case StrokeAlign::Inside:
      ApplyRRectInsideStroke(rect, path);
      break;
    case StrokeAlign::Outside:
      ApplyRRectOutsideStroke(rect, path);
      break;
    case StrokeAlign::Center:
      ApplyRRectCenterStroke(rect, path);
      break;
    default:
      break;
  }
}

void RectCustomStrokeShape::ApplyRectInsideStroke(const Rect& rect, Path* path) const {
  if (_lineDashPattern.empty()) {
    auto [left, top, right, bottom] = _borderWeights;
    if (left + right >= rect.width() || top + bottom >= rect.height()) {
      path->addRect(rect);
      return;
    }
    auto innerRect = rect;
    RectInset(left, top, right, bottom, &innerRect);
    Path innerPath;
    innerPath.addRect(innerRect);
    path->addRect(rect);
    path->addPath(innerPath, PathOp::Difference);
  } else {
    const Point cornerPoints[CornerCount] = {
        {rect.left, rect.bottom},
        {rect.left, rect.top},
        {rect.right, rect.top},
        {rect.right, rect.bottom},
    };
    *path = {};
    // side path
    for (size_t i = 0; i < CornerCount; ++i) {
      auto strokeWidth = _borderWeights[i];
      if (FloatNearlyZero(strokeWidth)) {
        continue;
      }
      strokeWidth = std::min(strokeWidth, i % 2 == 0 ? rect.width() : rect.height());
      auto startPoint = cornerPoints[i];
      auto endPoint = cornerPoints[(i + 1) % CornerCount];
      auto direction = endPoint - startPoint;
      auto normal = Point::Make(-direction.y, direction.x);
      auto length = normal.length();
      if (FloatNearlyZero(length)) {
        continue;
      }
      normal *= (1.f / length);
      startPoint += normal * strokeWidth * 0.5f;
      endPoint += normal * strokeWidth * 0.5f;
      Path dashPath;
      dashPath.moveTo(startPoint.x, startPoint.y);
      dashPath.lineTo(endPoint.x, endPoint.y);
      auto dashEffect =
          PathEffect::MakeDash(_lineDashPattern.data(), static_cast<int>(_lineDashPattern.size()),
                               _lineDashPattern[0] * 0.5f, true);
      dashEffect->filterPath(&dashPath);
      auto tempStroke = stroke;
      tempStroke.width = strokeWidth;
      tempStroke.applyToPath(&dashPath);
      path->addPath(dashPath, PathOp::Append);
    }
  }
}

void RectCustomStrokeShape::ApplyRectOutsideStroke(const Rect& rect, Path* path) const {
  auto [left, top, right, bottom] = _borderWeights;
  auto outerRect = rect;
  RectOutset(left, top, right, bottom, &outerRect);
  if (_lineDashPattern.empty()) {
    Path basePath, outerPath;
    basePath.addRect(rect);
    outerPath.addRect(outerRect);
    *path = outerPath;
    path->addPath(basePath, PathOp::Difference);
    AddSolidCornerJoin(outerRect, rect, stroke.join, path);
  } else {
    const Point cornerPoints[4] = {
        {rect.left, rect.bottom},
        {rect.left, rect.top},
        {rect.right, rect.top},
        {rect.right, rect.bottom},
    };
    *path = {};
    // side path
    for (size_t i = 0; i < CornerCount; ++i) {
      if (FloatNearlyZero(_borderWeights[i])) {
        continue;
      }
      auto startPoint = cornerPoints[i];
      auto endPoint = cornerPoints[(i + 1) % CornerCount];
      auto direction = endPoint - startPoint;
      auto normal = Point::Make(-direction.y, direction.x);
      auto length = normal.length();
      if (FloatNearlyZero(length)) {
        continue;
      }
      normal *= (1.f / length);
      startPoint -= normal * _borderWeights[i] * 0.5f;
      endPoint -= normal * _borderWeights[i] * 0.5f;
      Path dashPath;
      dashPath.moveTo(startPoint.x, startPoint.y);
      dashPath.lineTo(endPoint.x, endPoint.y);
      auto dashEffect =
          PathEffect::MakeDash(_lineDashPattern.data(), static_cast<int>(_lineDashPattern.size()),
                               _lineDashPattern[0] * 0.5f, true);
      dashEffect->filterPath(&dashPath);
      auto tempStroke = stroke;
      tempStroke.width = _borderWeights[i];
      tempStroke.applyToPath(&dashPath);
      path->addPath(dashPath, PathOp::Append);
    }
    AddDashCornerJoin(outerRect, rect, stroke.join, path);
  }
}

void RectCustomStrokeShape::ApplyRectCenterStroke(const Rect& rect, Path* path) const {
  auto left = _borderWeights[0] * 0.5f;
  auto top = _borderWeights[1] * 0.5f;
  auto right = _borderWeights[2] * 0.5f;
  auto bottom = _borderWeights[3] * 0.5f;
  auto outerRect = rect;
  RectOutset(left, top, right, bottom, &outerRect);
  if (_lineDashPattern.empty()) {
    Path innerPath, outerPath;
    outerPath.addRect(outerRect);
    if (left + right >= rect.width() || top + bottom >= rect.height()) {
      *path = outerPath;
      AddSolidCornerJoin(outerRect, rect, stroke.join, path);
      return;
    }
    auto innerRect = rect;
    RectInset(left, top, right, bottom, &innerRect);
    innerPath.addRect(innerRect);
    *path = outerPath;
    path->addPath(innerPath, PathOp::Difference);
    AddSolidCornerJoin(outerRect, rect, stroke.join, path);
  } else {
    const Point cornerPoints[CornerCount] = {
        {rect.left, rect.bottom},
        {rect.left, rect.top},
        {rect.right, rect.top},
        {rect.right, rect.bottom},
    };
    *path = {};
    // side path
    for (size_t i = 0; i < CornerCount; ++i) {
      if (FloatNearlyZero(_borderWeights[i])) {
        continue;
      }
      auto startPoint = cornerPoints[i];
      auto endPoint = cornerPoints[(i + 1) % CornerCount];
      Path dashPath;
      dashPath.moveTo(startPoint.x, startPoint.y);
      dashPath.lineTo(endPoint.x, endPoint.y);
      auto dashEffect =
          PathEffect::MakeDash(_lineDashPattern.data(), static_cast<int>(_lineDashPattern.size()),
                               _lineDashPattern[0] * 0.5f, true);
      dashEffect->filterPath(&dashPath);
      auto tempStroke = stroke;
      tempStroke.width = _borderWeights[i];
      tempStroke.applyToPath(&dashPath);
      path->addPath(dashPath, PathOp::Append);
    }
    AddDashCornerJoin(outerRect, rect, stroke.join, path);
  }
}

void RectCustomStrokeShape::ApplyRRectInsideStroke(const Rect& rect, Path* path) const {
  auto [left, top, right, bottom] = _borderWeights;
  Point innerRadii[CornerCount], baseRadii[CornerCount];
  const float radiiOffset[] = {left, top, right, top, right, bottom, left, bottom};
  for (size_t i = 0; i < CornerCount; ++i) {
    auto offsetX = radiiOffset[2 * i];
    auto offsetY = radiiOffset[2 * i + 1];
    innerRadii[i].x = std::max(0.f, _radii[i] - offsetX);
    innerRadii[i].y = std::max(0.f, _radii[i] - offsetY);
    baseRadii[i] = {_radii[i], _radii[i]};
  }
  auto innerRect = rect;
  RectInset(left, top, right, bottom, &innerRect);
  Path basePath, innerPath;
  innerPath.addRoundRect(innerRect, innerRadii);
  basePath.addRoundRect(rect, baseRadii);
  *path = basePath;
  path->addPath(innerPath, PathOp::Difference);
  if (_lineDashPattern.empty()) {
    return;
  }
  auto dashEffect =
      PathEffect::MakeDash(_lineDashPattern.data(), static_cast<int>(_lineDashPattern.size()),
                           _lineDashPattern[0] * 0.5f, true);
  dashEffect->filterPath(&basePath);
  auto maxWidth = std::max({left, top, right, bottom});
  auto tempStroke = stroke;
  tempStroke.width = maxWidth * 2.f;
  tempStroke.applyToPath(&basePath);
  path->addPath(basePath, PathOp::Intersect);
}

void RectCustomStrokeShape::ApplyRRectOutsideStroke(const Rect& rect, Path* path) const {
  auto [left, top, right, bottom] = _borderWeights;
  Point outerRadii[CornerCount], baseRadii[CornerCount];
  const float radiiOffset[] = {left, top, right, top, right, bottom, left, bottom};
  for (size_t i = 0; i < CornerCount; ++i) {
    float min = std::min(radiiOffset[2 * i], radiiOffset[2 * i + 1]);
    float max = std::max(radiiOffset[2 * i], radiiOffset[2 * i + 1]);
    auto radiusOffset = FloatNearlyZero(_radii[i]) ? _radii[i] : (FloatNearlyZero(min) ? max : min);
    outerRadii[i] = {_radii[i] + radiusOffset, _radii[i] + radiusOffset};
    baseRadii[i] = {_radii[i], _radii[i]};
  }
  auto outerRect = rect;
  RectOutset(left, top, right, bottom, &outerRect);
  Path basePath, outerPath;
  basePath.addRoundRect(rect, baseRadii);
  outerPath.addRoundRect(outerRect, outerRadii);
  *path = outerPath;
  path->addPath(basePath, PathOp::Difference);
  if (_lineDashPattern.empty()) {
    return;
  }
  auto dashEffect =
      PathEffect::MakeDash(_lineDashPattern.data(), static_cast<int>(_lineDashPattern.size()),
                           _lineDashPattern[0] * 0.5f, true);
  dashEffect->filterPath(&basePath);
  auto maxWidth = std::max({left, top, right, bottom});
  auto tempStroke = stroke;
  tempStroke.width = maxWidth * 2.f;
  tempStroke.applyToPath(&basePath);
  path->addPath(basePath, PathOp::Intersect);
}

void RectCustomStrokeShape::ApplyRRectCenterStroke(const Rect& rect, Path* path) const {
  auto left = _borderWeights[0] * 0.5f;
  auto top = _borderWeights[1] * 0.5f;
  auto right = _borderWeights[2] * 0.5f;
  auto bottom = _borderWeights[3] * 0.5f;
  const float radiiOffset[] = {left, top, right, top, right, bottom, left, bottom};
  Point outerRadii[CornerCount], innerRadii[CornerCount];
  for (size_t i = 0; i < _radii.size(); ++i) {
    auto offsetX = radiiOffset[2 * i];
    auto offsetY = radiiOffset[2 * i + 1];
    auto min = std::min(offsetX, offsetY);
    auto max = std::max(offsetX, offsetY);
    auto radiusOffset = (FloatNearlyZero(min) ? max : min);
    outerRadii[i] = {_radii[i] + radiusOffset, _radii[i] + radiusOffset};
    innerRadii[i].x = std::max(0.f, _radii[i] - offsetX);
    innerRadii[i].y = std::max(0.f, _radii[i] - offsetY);
  }
  auto outerRect = rect;
  RectOutset(left, top, right, bottom, &outerRect);

  Path outerPath;
  outerPath.addRoundRect(outerRect, outerRadii);
  auto innerRect = rect;
  RectInset(left, top, right, bottom, &innerRect);
  *path = outerPath;
  if (!innerRect.isEmpty()) {
    Path innerPath;
    innerPath.addRoundRect(innerRect, innerRadii);
    path->addPath(innerPath, PathOp::Difference);
  }
  if (_lineDashPattern.empty()) {
    return;
  }

  Point baseRadii[CornerCount];
  for (size_t i = 0; i < CornerCount; ++i) {
    baseRadii[i] = {_radii[i], _radii[i]};
  }
  Path basePath;
  basePath.addRoundRect(rect, baseRadii);
  auto dashEffect =
      PathEffect::MakeDash(_lineDashPattern.data(), static_cast<int>(_lineDashPattern.size()),
                           _lineDashPattern[0] * 0.5f, true);
  dashEffect->filterPath(&basePath);
  auto maxWidth = std::max({left, top, right, bottom});
  auto tempStroke = stroke;
  tempStroke.width = maxWidth * 2.f;
  tempStroke.applyToPath(&basePath);
  path->addPath(basePath, PathOp::Intersect);
}
}  // namespace tgfx
