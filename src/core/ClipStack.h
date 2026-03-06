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

#include <cfloat>
#include <cmath>
#include <memory>
#include <stack>
#include <vector>
#include "tgfx/core/Path.h"
#include "tgfx/core/Rect.h"

namespace tgfx {

/**
 * ClipState represents the current state of the clip stack.
 */
enum class ClipState {
  Empty,     // The clip region is empty, all drawing will be clipped out.
  WideOpen,  // No clipping, all drawing will be visible.
  Rect,      // A single rectangle clip.
  Complex    // Multiple elements or non-rectangle, need to iterate through elements.
};

/**
 * ClipElement stores a single clip's geometry and AA setting.
 */
class ClipElement {
 public:
  ClipElement() = default;
  ClipElement(const Path& path, bool antiAlias);

  const Path& getPath() const {
    return path;
  }

  bool isAntiAlias() const {
    return antiAlias;
  }

  const Rect& getBound() const {
    return bound;
  }

  bool getIsRect() const {
    return isRect;
  }

  /**
   * Returns true if the element is valid (not invalidated by another element).
   */
  bool isValid() const {
    return invalidatedByIndex < 0;
  }

  /**
   * Returns true if the bound is pixel-aligned (integer coordinates).
   */
  bool isPixelAligned() const {
    return bound.left == floorf(bound.left) && bound.top == floorf(bound.top) &&
           bound.right == floorf(bound.right) && bound.bottom == floorf(bound.bottom);
  }

  /**
   * Tries to combine another element into this one. Returns true if successful.
   * Only combines rectangles with the same AA setting.
   */
  bool tryCombine(const ClipElement& other);

  /**
   * Marks this element as invalidated by an element at the given index.
   */
  void markInvalid(int byIndex) {
    invalidatedByIndex = byIndex;
  }

  /**
   * Returns the index that invalidated this element, or -1 if valid.
   */
  int getInvalidatedByIndex() const {
    return invalidatedByIndex;
  }

 private:
  Path path = {};
  bool antiAlias = false;
  Rect bound = Rect::MakeEmpty();
  bool isRect = false;

  // Index that invalidated this element. -1 means valid.
  int invalidatedByIndex = -1;

  friend class ClipStack;
};

/**
 * ClipRecord manages a single save level's clip state.
 */
struct ClipRecord {
  size_t startIndex = 0;        // First element index owned by this save level.
  size_t oldestValidIndex = 0;  // Iteration start point, oldest valid element index globally.
  Rect bound = Rect::MakeLTRB(-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX);  // Accumulated bounds.
  ClipState state = ClipState::WideOpen;                              // Current clip state.
  uint32_t uniqueID = 0;                                              // Version for caching.

  ClipRecord();
};

/**
 * Internal shared data for ClipStack's Copy-on-Write implementation.
 */
struct ClipData {
  std::vector<ClipElement> elements = {};
  // Never empty, at least one default ClipRecord.
  std::stack<ClipRecord> records = {};

  ClipData();
};

/**
 * ClipStack manages clip state independently, encapsulating element merging and save/restore logic.
 * Uses Copy-on-Write for efficient copying.
 */
class ClipStack {
 public:
  ClipStack();

  /**
   * Adds a clip path with the specified anti-aliasing setting.
   */
  void clip(const Path& path, bool antiAlias);

  /**
   * Saves the current clip state. Push a new ClipRecord.
   */
  void save();

  /**
   * Restores the previous clip state. Pop the current ClipRecord.
   */
  void restore();

  /**
   * Returns the current clip state.
   */
  ClipState state() const {
    return current().state;
  }

  /**
   * Returns the accumulated bounds of all valid elements.
   */
  const Rect& bound() const {
    return current().bound;
  }

  /**
   * Returns all clip elements.
   */
  const std::vector<ClipElement>& elements() const {
    return _data->elements;
  }

  /**
   * Returns the oldest valid element index for iteration.
   */
  size_t oldestValidIndex() const {
    return current().oldestValidIndex;
  }

  /**
   * Returns the unique ID for cache invalidation.
   */
  uint32_t uniqueID() const {
    return current().uniqueID;
  }

  /**
   * Resets the clip stack with the specified record and elements.
   * Used for playback to replace internal state.
   */
  void reset(const ClipRecord& record, const std::vector<ClipElement>& elements);

 private:
  void addElement(const ClipElement& toAdd);
  void replaceWithElement(const ClipElement& toAdd);
  void appendElement(ClipElement toAdd);

  /**
   * Ensures exclusive ownership of internal data before modification.
   * If shared with other ClipStack instances, creates a deep copy.
   */
  void detachIfShared();

  const ClipRecord& current() const;
  ClipRecord& current();

  std::shared_ptr<ClipData> _data;
};

}  // namespace tgfx
