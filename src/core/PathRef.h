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

#include <atomic>
#include "gpu/resources/ResourceKey.h"
#include "tgfx/core/PathTypes.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-conversion"
#pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#include "pathkit.h"
#pragma clang diagnostic pop

namespace tgfx {
class Path;
struct Rect;

class PathRef {
 public:
  static const pk::SkPath& ReadAccess(const Path& path);

  static pk::SkPath& WriteAccess(Path& path);

  static UniqueKey GetUniqueKey(const Path& path);

  /**
   * Returns whether matching non-inverse rules and disjoint bounds allow a union by concatenation.
   */
  static bool UnionsByConcatenation(PathFillType firstFillType, const Rect& firstBounds,
                                    PathFillType secondFillType, const Rect& secondBounds);

  /**
   * Combines paths using the supplied conservative bounds to select the union fast path. Returns
   * false on failure without changing destination. Bounds must be obtained before writing the path.
   */
  static bool AddPath(Path& destination, const Path& source, PathOp op, const Rect& bounds,
                      const Rect& sourceBounds);

  PathRef() = default;

  explicit PathRef(const pk::SkPath& path) : path(path) {
  }

  ~PathRef();

  Rect getBounds();

 private:
  LazyUniqueKey uniqueKey = {};
  std::atomic<Rect*> bounds = {nullptr};
  pk::SkPath path = {};

  friend bool operator==(const Path& a, const Path& b);
  friend bool operator!=(const Path& a, const Path& b);
  friend class Path;
};
}  // namespace tgfx
