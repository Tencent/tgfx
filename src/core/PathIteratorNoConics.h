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

#include "tgfx/core/Path.h"

namespace tgfx {

/**
 * PathIteratorNoConics converts conic curves to quadratic curves during iteration.
 * Each conic is approximated by quadratic Bezier curves (pow2=1, up to 2 quads).
 * This is for internal use where Conic curves need to be converted to Quad curves
 * (e.g., rasterization backends that don't support conics).
 *
 * Supports range-based for loops:
 *   PathIteratorNoConics iterNoConics(path);
 *   for (auto segment : iterNoConics) {
 *       switch (segment.verb) {
 *           case PathVerb::Move: // segment.points[0]
 *           case PathVerb::Line: // segment.points[0-1]
 *           case PathVerb::Quad: // segment.points[0-2]
 *           case PathVerb::Cubic: // segment.points[0-3]
 *           case PathVerb::Close: // no points
 *       }
 *   }
 */
class PathIteratorNoConics {
 public:
  struct Segment {
    PathVerb verb = PathVerb::Done;
    Point points[4] = {};
  };

  class Iterator {
   public:
    ~Iterator();
    Iterator(const Iterator& other);
    Iterator& operator=(const Iterator& other);

    Segment operator*() const {
      return current;
    }

    Iterator& operator++();

    bool operator!=(const Iterator& other) const {
      return isDone != other.isDone;
    }

   private:
    explicit Iterator(const Path* path);
    Iterator();

    void advance();

    friend class PathIteratorNoConics;

    static constexpr size_t kStorageSize = 64;
    alignas(8) uint8_t storage[kStorageSize] = {};
    Segment current = {};
    Point pendingQuad[3] = {};
    bool hasPendingQuad = false;
    bool isDone = true;
  };

  explicit PathIteratorNoConics(const Path& path);

  Iterator begin() const;

  Iterator end() const {
    return Iterator();
  }

 private:
  const Path* path = nullptr;
};

}  // namespace tgfx
