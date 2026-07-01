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

#include "GeometryShape.h"
#include "RRectUtils.h"

namespace tgfx {

GeometryShape::GeometryShape() : _rect(Rect::MakeEmpty()) {
}

GeometryShape::GeometryShape(const Rect& rect) : _rect(rect), _type(Type::Rect) {
}

GeometryShape::GeometryShape(const RRect& rRect) : _rRect(rRect), _type(Type::RRect) {
}

GeometryShape::GeometryShape(const Path& path) : _rect(Rect::MakeEmpty()) {
  setPath(path);
}

GeometryShape::GeometryShape(const GeometryShape& other) : _rect(Rect::MakeEmpty()) {
  switch (other._type) {
    case Type::Empty:
      break;
    case Type::Rect:
      setRect(other._rect);
      break;
    case Type::RRect:
      setRRect(other._rRect);
      break;
    case Type::Path:
      setPath(other._path);
      break;
  }
}

GeometryShape& GeometryShape::operator=(const GeometryShape& other) {
  if (this == &other) {
    return *this;
  }
  switch (other._type) {
    case Type::Empty:
      setEmpty();
      break;
    case Type::Rect:
      setRect(other._rect);
      break;
    case Type::RRect:
      setRRect(other._rRect);
      break;
    case Type::Path:
      setPath(other._path);
      break;
  }
  return *this;
}

GeometryShape::~GeometryShape() {
  setEmpty();
}

Rect GeometryShape::bounds() const {
  switch (_type) {
    case Type::Empty:
      return Rect::MakeEmpty();
    case Type::Rect:
      return _rect;
    case Type::RRect:
      return _rRect.rect();
    case Type::Path:
      return _path.getBounds();
  }
}

Path GeometryShape::asPath() const {
  Path path = {};
  switch (_type) {
    case Type::Empty:
      break;
    case Type::Rect:
      path.addRect(_rect);
      break;
    case Type::RRect:
      path.addRRect(_rRect);
      break;
    case Type::Path:
      return _path;
  }
  return path;
}

bool GeometryShape::convex() const {
  switch (_type) {
    case Type::Empty:
    case Type::Rect:
    case Type::RRect:
      return true;
    case Type::Path:
      // A convex shape needs a single closed contour; an open contour is not a valid filled region.
      return _path.isLastContourClosed() && _path.isConvex();
  }
}

bool GeometryShape::conservativeContains(const Point& point) const {
  switch (_type) {
    case Type::Empty:
      return false;
    case Type::Rect:
      return _rect.contains(point.x, point.y);
    case Type::RRect:
      return RRectUtils::ContainsPoint(_rRect, point);
    case Type::Path:
      return _path.contains(point.x, point.y);
  }
}

bool GeometryShape::conservativeContains(const Rect& rect) const {
  switch (_type) {
    case Type::Empty:
      return false;
    case Type::Rect:
      return _rect.contains(rect);
    case Type::RRect:
      return RRectUtils::ContainsRect(_rRect, rect);
    case Type::Path:
      return _path.contains(rect);
  }
}

void GeometryShape::simplify() {
  // Reduce a Path to a more specific type when possible. Inverse-fill paths keep the region outside
  // their geometry, which a plain rect/rrect cannot represent, so they are left as Path.
  if (_type == Type::Path && !_path.isInverseFillType()) {
    if (_path.isEmpty()) {
      setEmpty();
    } else if (Rect rect = {}; _path.isRect(&rect)) {
      setRect(rect);
    } else if (Rect ovalBounds = {}; _path.isOval(&ovalBounds)) {
      // isRRect rejects oval-typed rrects, so detect ovals separately.
      setRRect(RRect::MakeOval(ovalBounds));
    } else if (RRect rRect = {}; _path.isRRect(&rRect)) {
      setRRect(rRect);
    }
  }
  // An rrect whose corners are all zero is really a rect.
  if (_type == Type::RRect && _rRect.isRect()) {
    setRect(_rRect.rect());
  }
}

void GeometryShape::setType(Type type) {
  if (_type == Type::Path && type != Type::Path) {
    _path.~Path();
  }
  _type = type;
}

void GeometryShape::setEmpty() {
  setType(Type::Empty);
}

void GeometryShape::setRect(const Rect& rect) {
  setType(Type::Rect);
  _rect = rect;
}

void GeometryShape::setRRect(const RRect& rRect) {
  setType(Type::RRect);
  _rRect = rRect;
}

void GeometryShape::setPath(const Path& path) {
  if (_type == Type::Path) {
    _path = path;
  } else {
    setType(Type::Path);
    new (&_path) Path(path);
  }
}

}  // namespace tgfx
