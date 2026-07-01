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

#pragma once

#include <type_traits>
#include "core/utils/Log.h"
#include "tgfx/core/Path.h"
#include "tgfx/core/RRect.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * GeometryShape is a tagged union over the geometric primitives used by the clip pipeline: an
 * empty shape, an axis-aligned rect, a rounded rect, or an arbitrary path. It owns the storage for
 * these mutually exclusive members and manages their lifetime. The shape is defined in its own
 * local coordinate space and carries no transform; callers compose any matrix themselves.
 */
class GeometryShape {
 public:
  enum class Type { Empty, Rect, RRect, Path };

  GeometryShape();
  explicit GeometryShape(const Rect& rect);
  explicit GeometryShape(const RRect& rRect);
  explicit GeometryShape(const Path& path);

  GeometryShape(const GeometryShape& other);
  GeometryShape& operator=(const GeometryShape& other);
  ~GeometryShape();

  Type type() const {
    return _type;
  }

  bool isEmpty() const {
    return _type == Type::Empty;
  }

  bool isRect() const {
    return _type == Type::Rect;
  }

  bool isPath() const {
    return _type == Type::Path;
  }

  const Rect& rect() const {
    DEBUG_ASSERT(_type == Type::Rect);
    return _rect;
  }

  const RRect& rRect() const {
    DEBUG_ASSERT(_type == Type::RRect);
    return _rRect;
  }

  const Path& path() const {
    DEBUG_ASSERT(_type == Type::Path);
    return _path;
  }

  /**
   * Returns the local-space bounding rect of the shape. An empty shape returns an empty rect.
   */
  Rect bounds() const;

  /**
   * Returns a local-space Path representing the shape. Rect and RRect shapes are converted on
   * demand; a Path shape is returned directly. No transform is applied.
   */
  Path asPath() const;

  /**
   * Returns true if the shape is convex. Empty, Rect, and RRect shapes are always convex; a Path
   * shape is convex only when it forms a single closed convex contour.
   */
  bool convex() const;

  /**
   * Returns true if the point lies inside the shape, treating the shape as a closed set. The point
   * is given in the shape's local space. An empty shape contains no point.
   */
  bool conservativeContains(const Point& point) const;

  /**
   * Returns true if the rect lies entirely inside the shape, treating the shape as a closed set.
   * The rect is given in the shape's local space. The check is conservative: a true result
   * guarantees containment, while a false result may or may not actually hold. An empty shape
   * contains no rect.
   */
  bool conservativeContains(const Rect& rect) const;

  /**
   * Reduces the shape to the simplest equivalent type: a Path that draws a rect or rrect becomes a
   * Rect or RRect, and an rrect with no real corners becomes a Rect. Inverse-fill paths are left
   * untouched because their keep-region cannot be expressed as a plain rect/rrect.
   */
  void simplify();

  void setRect(const Rect& rect);
  void setRRect(const RRect& rRect);
  void setPath(const Path& path);
  void setEmpty();

 private:
  // Switches the active union member's type tag, destroying the old Path when leaving Path type.
  // Other types are trivially destructible and need no teardown.
  void setType(Type type);

  union {
    Rect _rect;
    RRect _rRect;
    Path _path;
  };
  static_assert(std::is_trivially_destructible_v<Rect>, "Rect must remain trivially destructible");
  static_assert(std::is_trivially_destructible_v<RRect>,
                "RRect must remain trivially destructible");

  Type _type = Type::Empty;
};

}  // namespace tgfx
